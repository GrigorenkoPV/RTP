#include "RTP.h"
#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::string_literals;

#define TODO(msg) \
  throw std::runtime_error(""s + __FILE__ + ":" + std::to_string(__LINE__) + " TODO: " + msg);

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
  auto const LastSymbolID = FirstSymbolID + Symbols2Decode;
  auto const inRange = [FirstSymbolID, LastSymbolID](int s) -> bool {
    if (s < 0) {
      return false;
    } else {
      std::size_t symbolId = s;
      return FirstSymbolID <= symbolId && symbolId < LastSymbolID;
    }
  };
  auto const X = GetErasedPosition(ErasureSetID, 0);
  auto const Y = GetErasedPosition(ErasureSetID, 1);
  auto const Z = GetErasedPosition(ErasureSetID, 2);
  auto const is_ordered = [](int a, int b) -> bool { return b < 0 || (0 <= a && a < b); };
  assert(is_ordered(X, Y) && is_ordered(Y, Z));
  if (!inRange(X) && !inRange(Y) && !inRange(Z)) {
    // All the symbols are intact, read as is.
    auto result = true;
    for (auto symbolId = FirstSymbolID; symbolId < LastSymbolID; symbolId++) {
      result &= ReadSymbol(StripeID, ErasureSetID, symbolId,
                           pDest + (symbolId - FirstSymbolID) * SymbolSize());
    }
    return result;
  } else {
    bool ok = true;
    auto const symbolSize = SymbolSize();
    auto symbols = std::vector<AlignedBuffer>();
    for (std::size_t s = 0; s < p; ++s) {
      if (IsErased(ErasureSetID, s)) {
        symbols.emplace_back(symbolSize, true);
      } else {
        auto symbol = AlignedBuffer(symbolSize, false);
        ok &= ReadSymbol(StripeID, ErasureSetID, s, symbol.data());
        symbols.push_back(std::move(symbol));
      }
    }

    auto const NumErasedRaid4Symbols = GetNumErasedRaid4Symbols(ErasureSetID);
    bool isAnti;
    if (NumErasedRaid4Symbols > 1) {
      auto diag = AlignedBuffer(symbolSize, false);
      isAnti = IsErased(ErasureSetID, p);
      if (isAnti) {
        assert(!IsErased(ErasureSetID, p + 1));
      }
      ok &= ReadSymbol(StripeID, ErasureSetID, isAnti ? p + 1 : p, diag.data());
      symbols.push_back(std::move(diag));
    }

    switch (NumErasedRaid4Symbols) {
      case 3: {
        TODO("RTP");
      }
        [[fallthrough]];
      case 2: {
        auto r = p - 1;
        for (std::size_t i = 0; i < p - 1; ++i) {
          auto const d = DiagNum(isAnti, Y, r);
          r = (p + d - X) % p;
          assert(r < m_Dimension);
          TODO("A[r, X] := DIAGSUM(d) ^ diag[d]");
          TODO("A[r, Y] := ROWSUM(r)");
        }
      } break;
      default: {
        assert(NumErasedRaid4Symbols == 1);
        for (std::size_t s = 0; s < p; ++s) {
          if (s != X) {
            symbols[X] ^= symbols[s];
          }
        }
      } break;
    }

    for (auto symbolId = FirstSymbolID; symbolId < LastSymbolID; symbolId++) {
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
      for (std::size_t s = 0; s < p; ++s) {
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
  for (std::size_t symbolId = 0; symbolId < m_Dimension; ++symbolId) {
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
  for (std::size_t symbolId = 0; symbolId < p; ++symbolId) {
    auto const& symbol = read_symbol(symbolId);
    row ^= symbol;
    AddToDiags(diag, adiag, symbolId, symbol);
  }
  return row.isZero() && diag == read_symbol(p) && adiag == read_symbol(p + 1);
}
