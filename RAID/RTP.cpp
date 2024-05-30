#include "RTP.h"
#include <algorithm>
#include <cassert>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

#define TODO(msg)                                                                   \
  throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                           " TODO: " + msg);

namespace {
bool isPrime(unsigned n) {
  switch (n) {
    case 0:
    case 1:
      return false;
    case 2:
    case 3:
      return true;
    default:
      if (n % 2 == 0) {
        return false;
      }
      for (unsigned i = 3; (i * i) <= n; i += 2) {
        if (n % i == 0) {
          return false;
        }
      }
      return true;
  }
}

template <typename T>
auto iota(T from, T to) {
  return std::views::iota(from, to);
}

template <typename T>
auto iota(T to) {
  return iota(static_cast<T>(0), to);
}
}  // namespace

/// initialize coding-related parameters
CRTPProcessor::CRTPProcessor(RTPParams* P  /// the configuration file
                             )
    : CRAIDProcessor(P->CodeDimension + 3, P->CodeDimension, P, sizeof(*P)),
      p(P->CodeDimension + 1) {
  if (!isPrime(p)) {
    throw std::invalid_argument("Dimension+1 should be prime");
  }
}

bool CRTPProcessor::ReadSymbol(unsigned long long int StripeID,
                               unsigned int ErasureSetID,
                               unsigned int SymbolID,
                               AlignedBuffer& out) {
  assert(out.size() == SymbolSize());
  return ReadSymbol(StripeID, ErasureSetID, SymbolID, out.data());
}

bool CRTPProcessor::ReadSymbol(unsigned long long int StripeID,
                               unsigned int ErasureSetID,
                               unsigned int SymbolID,
                               unsigned char* out) {
  return ReadStripeUnit(StripeID, ErasureSetID, SymbolID, 0, m_StripeUnitsPerSymbol, out);
}

bool CRTPProcessor::WriteSymbol(unsigned long long int StripeID,
                                unsigned int ErasureSetID,
                                unsigned int SymbolID,
                                AlignedBuffer const& symbol) {
  assert(symbol.size() == SymbolSize());
  return WriteStripeUnit(StripeID, ErasureSetID, SymbolID, 0, m_StripeUnitsPerSymbol,
                         symbol.data());
}

/// decode a number of payload subsymbols from a given symbol
///@return true on success
bool CRTPProcessor::DecodeDataSymbols(
    unsigned long long StripeID,  /// the stripe to be processed
    unsigned ErasureSetID,        /// identifies the load balancing offset
    unsigned FirstSymbolID,       /// the first symbol to be processed
    unsigned Symbols2Decode,      /// the number of subsymbols within this symbol to be decoded
    unsigned char* pDest,         /// destination array.
    size_t ThreadID               /// the ID of the calling thread
) {
  assert(this->IsCorrectable(ErasureSetID));
  assert(FirstSymbolID + Symbols2Decode <= m_Dimension);
  auto const symbolSize = SymbolSize();
  auto const LastSymbolID = FirstSymbolID + Symbols2Decode;
  auto const wasRequested = [FirstSymbolID, LastSymbolID](int s) -> bool {
    if (s < 0) {
      return false;
    } else {
      std::size_t symbolId = s;
      return FirstSymbolID <= symbolId && symbolId < LastSymbolID;
    }
  };
  auto const [X, Y, Z] = GetErasedSymbols(ErasureSetID);
  auto const is_ordered = [](int a, int b) -> bool { return b < 0 || (0 <= a && a < b); };
  assert(is_ordered(X, Y) && is_ordered(Y, Z));
  if (!wasRequested(X) && !wasRequested(Y) && !wasRequested(Z)) {
    // All the symbols that we need are intact, read as is.
    auto result = true;
    for (unsigned const symbolId : iota(FirstSymbolID, LastSymbolID)) {
      result &= ReadSymbol(StripeID, ErasureSetID, symbolId,
                           pDest + (symbolId - FirstSymbolID) * symbolSize);
    }
    return result;
  } else {
    auto const NumErasedRaid4Symbols = GetNumErasedRaid4Symbols(ErasureSetID);

    bool ok = true;

    auto symbols = std::vector<AlignedBuffer>();
    symbols.reserve(p);

    bool const needDiag = NumErasedRaid4Symbols > 1;
    auto diag = AlignedBuffer();
    bool isAnti;
    if (needDiag) {
      isAnti = IsErased(ErasureSetID, p);
      if (isAnti) {
        assert(!IsErased(ErasureSetID, p + 1));
      }
      diag = AlignedBuffer(symbolSize);
      ok &= ReadSymbol(StripeID, ErasureSetID, isAnti ? p + 1 : p, diag.data());
    }

    for (std::size_t const s : iota(p)) {
      if (IsErased(ErasureSetID, s)) {
        symbols.emplace_back(symbolSize, true);
      } else {
        auto symbol = AlignedBuffer(symbolSize, false);
        ok &= ReadSymbol(StripeID, ErasureSetID, s, symbol.data());
        if (needDiag) {
          AddToDiag(diag, isAnti, s, symbol);
        }
        symbols.push_back(std::move(symbol));
      }
    }

    switch (NumErasedRaid4Symbols) {
      case 3: {  // RTP
        TODO("RTP");
      }
        [[fallthrough]];
      case 2: {  // RDP
        auto r = p - 1;
        for (unsigned const _ : iota(p - 1)) {
          auto const d = DiagNum(isAnti, Y, r);
          if (r != p - 1 && d != p - 1) {
            // Update the diagonal checksum after restoring Y[r] on the previous iteration
            XOR(diag.data() + d * m_StripeUnitSize, symbols[Y].data() + r * m_StripeUnitSize,
                m_StripeUnitSize);
          }
          r = (p + d - X) % p;  // TODO: this should take isAnti in consideration
          assert(DiagNum(isAnti, X, r) == d);
          assert(r < m_StripeUnitsPerSymbol);
          // Restore X[r] using a diagonal sum
          {
            auto const ax = symbols[X].data() + r * m_StripeUnitSize;
            if (d != m_StripeUnitsPerSymbol) {
              // ax is zeroed at this point, so we can memcpy instead of XORing
              assert(d < m_StripeUnitsPerSymbol);
              auto const diag_sum = diag.data() + d * m_StripeUnitSize;
              memcpy(ax, diag_sum, m_StripeUnitSize);
              // We don't actually need to update the diagonal sum,
              // because we aren't going to read it again.
              // But if we had to, we could just zero it, because at this point the
              // diagonal d should be completely restored.
              // memset(diag_sum, 0, m_StripeUnitSize);
            } else {
              // This is a diag that we didn't store.
              // We can recompute it as a XOR of all other diagonal subsymbols.
              for (unsigned const i : iota(m_StripeUnitsPerSymbol)) {
                XOR(ax, diag.data() + i * m_StripeUnitSize, m_StripeUnitSize);
              }
            }
          }
          // Restore Y's row r with a row sum
          {
            auto const ay = symbols[Y].data() + r * m_StripeUnitSize;
            for (std::size_t const s : iota(p)) {
              if (s != Y) {
                auto const as = symbols[s].data() + r * m_StripeUnitSize;
                XOR(ay, as, m_StripeUnitSize);
              }
            }
            // We will update the diagonal checksum at the start of the next iteration.
          }
        }
      } break;
      default: {  // RAID4
        assert(NumErasedRaid4Symbols == 1);
        for (std::size_t const s : iota(p)) {
          if (s != X) {
            symbols[X] ^= symbols[s];
          }
        }
      } break;
    }

    for (unsigned const symbolId : iota(FirstSymbolID, LastSymbolID)) {
      memcpy(pDest, symbols[symbolId].data(), symbolSize);
      pDest += symbolSize;
    }

    return ok;
  }
}

bool CRTPProcessor::DecodeDataSubsymbols(unsigned long long int StripeID,
                                         unsigned int ErasureSetID,
                                         unsigned int SymbolID,
                                         unsigned int SubsymbolID,
                                         unsigned int Subsymbols2Decode,
                                         unsigned char* pDest,
                                         size_t ThreadID) {
  assert(SymbolID < m_Dimension);
  // If the symbol is OK, just read from it.
  if (!IsErased(ErasureSetID, SymbolID)) {
    return ReadStripeUnit(StripeID, ErasureSetID, SymbolID, SubsymbolID, Subsymbols2Decode, pDest);
  }
  auto const NumErasedRAID4Symbols = GetNumErasedRaid4Symbols(ErasureSetID);

  if (SymbolID < p) {
    // We have a RAID4 disk...
    if (NumErasedRAID4Symbols == 1) {
      // ...and we can use row parity
      auto const size = Subsymbols2Decode * m_StripeUnitSize;
      auto read_buf = AlignedBuffer(size);
      auto xor_buf = AlignedBuffer(size, true);
      auto ok = true;
      for (std::size_t const s : iota(p)) {
        if (s == SymbolID) {
          continue;
        }
        assert(!IsErased(ErasureSetID, s));
        ok &= ReadStripeUnit(StripeID, ErasureSetID, s, SubsymbolID, Subsymbols2Decode,
                             read_buf.data());
        xor_buf ^= read_buf;
      }
      memcpy(pDest, xor_buf.data(), size);
      return ok;
    }
  } else {
    // We have an (anti)diagonal disk...
    if (NumErasedRAID4Symbols <= 1) {
      // ...and we can recompute it
      TODO("restore RAID4 and recompute (anti)diagonal");
    }
  }

  // No luck, we have to restore the entire symbol
  auto symbol = AlignedBuffer(SymbolSize());
  auto const ok = DecodeDataSymbols(StripeID, ErasureSetID, SymbolID, 1, symbol.data(), ThreadID);
  if (!ok) {
    return false;
  }
  memcpy(pDest, symbol.data() + SubsymbolID * m_StripeUnitSize,
         Subsymbols2Decode * m_StripeUnitSize);
  return true;
}

unsigned int CRTPProcessor::GetNumErasedRaid4Symbols(unsigned int ErasureSetID) const {
  return GetNumOfErasures(ErasureSetID) - IsErased(ErasureSetID, p) - IsErased(ErasureSetID, p + 1);
}

std::array<int, 3> CRTPProcessor::GetErasedSymbols(unsigned int ErasureSetID) const {
  auto result = std::array{GetErasedPosition(ErasureSetID, 0), GetErasedPosition(ErasureSetID, 1),
                           GetErasedPosition(ErasureSetID, 2)};
  auto const n = GetNumOfErasures(ErasureSetID);
  for (unsigned const i : iota(3)) {
    if (i < n) {
      assert(result[i] >= 0);
    } else {
      assert(result[i] == -1);
    }
  }
  std::sort(result.begin(), result.begin() + n);
  return result;
}

/// encode and write the whole CRAIDProcessorstripe
///@return true on success
bool CRTPProcessor::EncodeStripe(unsigned long long StripeID,  /// the stripe to be encoded
                                 unsigned ErasureSetID,  /// identifies the load balancing offset
                                 const unsigned char* pData,  /// the data to be envoced
                                 size_t ThreadID              /// the ID of the calling thread
) {
  assert(IsCorrectable(ErasureSetID));
  auto ok = true;
  auto const write_symbol = [=, this, &ok](std::size_t symbolId, AlignedBuffer const& symbol) {
    if (!IsErased(ErasureSetID, symbolId)) {
      ok &= WriteSymbol(StripeID, ErasureSetID, symbolId, symbol);
    }
  };
  auto const symbol_size = SymbolSize();
  auto buffer = AlignedBuffer(symbol_size);
  auto row = AlignedBuffer(symbol_size, true);
  auto diag = AlignedBuffer(symbol_size, true);
  auto adiag = AlignedBuffer(symbol_size, true);
  for (std::size_t const symbolId : iota(m_Dimension)) {
    memcpy(buffer.data(), pData + symbolId * symbol_size, symbol_size);
    auto const& symbol = buffer;
    write_symbol(symbolId, symbol);
    row ^= symbol;
    AddToDiags(diag, adiag, symbolId, symbol);
  }
  AddToDiags(diag, adiag, p - 1, row);
  write_symbol(p - 1, row);
  write_symbol(p, diag);
  write_symbol(p + 1, adiag);
  return ok;
}

/// update some information symbols and the corresponding check symbols
///@return true on success
bool CRTPProcessor::UpdateInformationSymbols(
    unsigned long long StripeID,  /// the stripe to be updated,
    unsigned ErasureSetID,        /// identifies the load balancing offset
    unsigned StripeUnitID,        /// the first stripe unit to be updated
    unsigned Units2Update,        /// the number of units to be updated
    const unsigned char* pData,   /// new payload data symbols
    size_t ThreadID               /// the ID of the calling thread
) {
  TODO("UpdateInformationSymbols")
  return false;
}

/// check if the codeword is consistent
bool CRTPProcessor::CheckCodeword(unsigned long long StripeID,  /// the stripe to be checked
                                  unsigned ErasureSetID,  /// identifies the load balancing offset
                                  size_t ThreadID         /// identifies the calling thread
) {
  if (GetNumOfErasures(ErasureSetID)) {
    return true;
  }
  auto const symbol_size = SymbolSize();
  auto buffer = AlignedBuffer(symbol_size);
  auto read_symbol = [this, StripeID, ErasureSetID,
                      &buffer](std::size_t symbolId) -> AlignedBuffer const& {
    auto ok = ReadSymbol(StripeID, ErasureSetID, symbolId, buffer);
    if (!ok) {
      throw std::runtime_error("Error reading data");
    }
    return buffer;
  };
  auto row = AlignedBuffer(symbol_size, true);
  auto diag = AlignedBuffer(symbol_size, true);
  auto adiag = AlignedBuffer(symbol_size, true);

  for (std::size_t const symbolId : iota(p)) {
    auto const& symbol = read_symbol(symbolId);
    row ^= symbol;
    AddToDiags(diag, adiag, symbolId, symbol);
  }
  return row.isZero() && diag == read_symbol(p) && adiag == read_symbol(p + 1);
}
