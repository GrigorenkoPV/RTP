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
  assert(out.size() >= SymbolSize());
  return ReadSymbol(StripeID, ErasureSetID, SymbolID, out.data());
}

bool CRTPProcessor::ReadSymbol(unsigned long long int StripeID,
                               unsigned int ErasureSetID,
                               unsigned int SymbolID,
                               unsigned char* out) {
  return ReadSubsymbols(StripeID, ErasureSetID, SymbolID, out, 0, m_StripeUnitsPerSymbol);
}

bool CRTPProcessor::ReadSubsymbols(unsigned long long int StripeID,
                                   unsigned int ErasureSetID,
                                   unsigned int SymbolID,
                                   unsigned char* out,
                                   unsigned int start,
                                   unsigned int count) {
  assert(!IsErased(ErasureSetID, SymbolID));
  return ReadStripeUnit(StripeID, ErasureSetID, SymbolID, start, count, out);
}

bool CRTPProcessor::WriteSymbol(unsigned long long int StripeID,
                                unsigned int ErasureSetID,
                                unsigned int SymbolID,
                                AlignedBuffer const& symbol) {
  assert(symbol.size() == SymbolSize());
  return WriteSubsymbols(StripeID, ErasureSetID, SymbolID, symbol.data(), 0,
                         m_StripeUnitsPerSymbol);
}

bool CRTPProcessor::WriteSubsymbols(unsigned long long int StripeID,
                                    unsigned int ErasureSetID,
                                    unsigned int SymbolID,
                                    unsigned char const* data,
                                    unsigned start,
                                    unsigned count) {
  return WriteStripeUnit(StripeID, ErasureSetID, SymbolID, start, count, data);
}

void operator^=(std::vector<bool>& lhs, std::vector<bool> const& rhs) {
  assert(lhs.size() == rhs.size());
  for (std::size_t const i : iota(lhs.size())) {
    lhs[i] = lhs[i] ^ rhs[i];
  }
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

  auto [X, Y, Z] = GetErasedSymbols(ErasureSetID);
  {
    [[maybe_unused]] auto const is_ordered = [](int a, int b) -> bool {
      return b < 0 || (0 <= a && a < b);
    };
    assert(is_ordered(X, Y) && is_ordered(Y, Z));
  }

  bool ok = true;

  auto const wasRequested = [FirstSymbolID, LastSymbolID](int s) -> bool {
    if (s < 0) {
      return false;
    } else {
      std::size_t symbolId = s;
      return FirstSymbolID <= symbolId && symbolId < LastSymbolID;
    }
  };
  if (!wasRequested(X) && !wasRequested(Y) && !wasRequested(Z)) {
    // All the symbols that we need are intact, read as is.
    for (unsigned const symbolId : iota(FirstSymbolID, LastSymbolID)) {
      ok &= ReadSymbol(StripeID, ErasureSetID, symbolId,
                       pDest + (symbolId - FirstSymbolID) * symbolSize);
    }
    return ok;
  }

  auto const NumErasedRaid4Symbols = GetNumErasedRaid4Symbols(ErasureSetID);

  auto symbols = std::vector<AlignedBuffer>();
  symbols.reserve(p);

  auto diag = AlignedBuffer();
  bool const isAnti = IsErased(ErasureSetID, p);
  if (NumErasedRaid4Symbols > 1) {
    diag = AlignedBuffer(symbolSize + m_StripeUnitSize);
    auto const d = isAnti ? p + 1 : p;
    assert(!IsErased(ErasureSetID, d));
    ok &= ReadSymbol(StripeID, ErasureSetID, d, diag);
    auto const missing = &diag[symbolSize];
    memcpy(missing, diag.data(), m_StripeUnitSize);
    for (unsigned const i : iota(1u, m_StripeUnitsPerSymbol)) {
      XOR(missing, diag.data() + i * m_StripeUnitSize, m_StripeUnitSize);
    }
  }

  for (std::size_t const s : iota(p)) {
    if (IsErased(ErasureSetID, s)) {
      symbols.emplace_back(symbolSize, true);
    } else {
      auto symbol = AlignedBuffer(symbolSize, false);
      ok &= ReadSymbol(StripeID, ErasureSetID, s, symbol);
      if (diag.data() != nullptr) {
        AddToDiag(diag, isAnti, s, symbol);
      }
      symbols.push_back(std::move(symbol));
    }
  }
  assert(symbols.size() == p);

  switch (NumErasedRaid4Symbols) {
    case 3: {  // RTP
      assert(!IsErased(ErasureSetID, p));
      assert(!IsErased(ErasureSetID, p + 1));
      // diag is the non-anti diagonal
      assert(!isAnti);

      auto adiag = AlignedBuffer(symbolSize + m_StripeUnitSize, false);
      {
        // Read the p-1 stored subsymbols
        ok &= ReadSymbol(StripeID, ErasureSetID, p + 1, adiag);

        {  // Restore the missing subsymbol using XOR
          auto const missing = &adiag[symbolSize];
          memcpy(missing, adiag.data() + 0 * m_StripeUnitSize, m_StripeUnitSize);
          for (unsigned const i : iota(1u, m_StripeUnitsPerSymbol)) {
            XOR(missing, adiag.data() + i * m_StripeUnitSize, m_StripeUnitSize);
          }
        }
      }

      auto row = AlignedBuffer(symbolSize + m_StripeUnitSize, true);

      // Add the RAID4 symbols to anti-diag & row
      for (std::size_t const s : iota(p)) {
        [[maybe_unused]] auto const& symbol = symbols[s];
        if (IsErased(ErasureSetID, s)) {
          assert(symbol.isZero());
        } else {
          row ^= symbol;
          AddToDiag(adiag, true, s, symbol);
        }
      }

      auto lhs = std::vector(p, std::vector(p - 1, false));
      assert(X < Y && Y < Z);
      for (unsigned const k : iota(p)) {
        for (unsigned const c : {
                 k,
                 k + (Z - Y),
                 k + (Y - X),
                 k + (Z - X),
             }) {
          auto const i = c % p;
          if (i != p - 1) {
            lhs[k][i].flip();
          }
        }
      }

      auto rhs = row.clone();
      for (unsigned const k : iota(p)) {
        auto const d = DiagNum(false, Z, k);
        auto const ad = DiagNum(true, X, k);
        auto const q = (k + Z - X) % p;
        assert(DiagNum(false, Z, k) == DiagNum(false, X, q));
        auto pos = &rhs[k * m_StripeUnitSize];
        XOR(pos, &diag[d * m_StripeUnitSize], m_StripeUnitSize);
        XOR(pos, &adiag[ad * m_StripeUnitSize], m_StripeUnitSize);
        XOR(pos, &row[q * m_StripeUnitSize], m_StripeUnitSize);
      }


      // Linear equations
      for (unsigned const r : iota(p - 1)) {
        if (!lhs[r][r]) {
          for (unsigned const other : iota(r + 1, p)) {
            if (lhs[other][r]) {
              assert(other != r);
              std::swap(lhs[r], lhs[other]);
              auto* a = &rhs[r * m_StripeUnitSize];
              auto* b = &rhs[other * m_StripeUnitSize];
              std::swap_ranges(a, a + m_StripeUnitSize, b);
              break;
            }
          }
        }

        assert(lhs[r][r]);
        for (unsigned const other : iota(p)) {
          if (r != other && lhs[other][r]) {
            lhs[other] ^= lhs[r];
            XOR(&rhs[other * m_StripeUnitSize], &rhs[r * m_StripeUnitSize], m_StripeUnitSize);
          }
        }
      }

      for ([[maybe_unused]] unsigned const r : iota(p)) {
        for ([[maybe_unused]] unsigned const c : iota(p - 1)) {
          assert((r == c) == lhs[r][c]);
        }
      }

      for ([[maybe_unused]] auto const b : lhs.back()) {
        assert(!b);
      }
      for ([[maybe_unused]] unsigned const i : iota(m_StripeUnitSize)) {
        assert(rhs[symbolSize + i] == 0);
      }

      std::memcpy(symbols[Y].data(), rhs.data(), symbolSize);
      AddToDiag(diag, isAnti, Y, symbols[Y]);
      // We're about to do RDP, and it's going to restore X and Y.
      // We've just restored Y ourselves though.
      // So let's swap Y & Z and pretend we've restored Z instead.
      std::swap(Y, Z);
    }
      if (!wasRequested(X) && !wasRequested(Y)) {
        break;
      } else {
        [[fallthrough]];
      }
    case 2: {  // RDP
      assert(X < Y);
      auto r = p - 1;
      for (unsigned const _ : iota(p - 1)) {
        auto const d = DiagNum(isAnti, Y, r);
        if (r != m_StripeUnitsPerSymbol) {
          // Update the diagonal checksum after restoring Y[r] on the previous iteration
          XOR(diag.data() + d * m_StripeUnitSize, symbols[Y].data() + r * m_StripeUnitSize,
              m_StripeUnitSize);
        }
        r = (isAnti ? (p + X - d) : (p + d - X)) % p;
        assert(DiagNum(isAnti, X, r) == d);
        assert(r < m_StripeUnitsPerSymbol);
        // Restore X[r] using a diagonal sum
        {
          auto const ax = symbols[X].data() + r * m_StripeUnitSize;
          // ax is zeroed at this point, so we can memcpy instead of XORing
          assert(d <= m_StripeUnitsPerSymbol);
          auto const diag_sum = diag.data() + d * m_StripeUnitSize;
          memcpy(ax, diag_sum, m_StripeUnitSize);
          // We don't actually need to update the diagonal sum,
          // because we aren't going to read it again.
          // But if we had to, we could just zero it, because at this point the
          // diagonal d should be completely restored.
          // memset(diag_sum, 0, m_StripeUnitSize);
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

  if (NumErasedRAID4Symbols == 1) {
      // We can use row parity
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
  for ([[maybe_unused]] unsigned const i : iota(3)) {
    assert((i < n) ? (result[i] >= 0) : (result[i] == -1));
  }
  switch (n) {
    case 3:
      for (std::size_t const i : iota(2)) {
        if (result[i] > result[2]) {
          std::swap(result[i], result[2]);
        }
      }
      [[fallthrough]];
    case 2:
      if (result[0] > result[1]) {
        std::swap(result[0], result[1]);
      }
      break;
    default:
      assert(n <= 1);
      break;
  }
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

bool CRTPProcessor::GetEncodingStrategy(unsigned int ErasureSetID,
                                        unsigned int StripeUnitID,
                                        unsigned int Subsymbols2Encode) {
  assert(StripeUnitID + Subsymbols2Encode <= m_Dimension * m_StripeUnitsPerSymbol);
  constexpr auto READ_WRITE = true;
  constexpr auto UPDATE = false;
  // If all the checksum disks are erased, there's nothing to talk about.
  // Just update the symbols.
  if (IsErased(ErasureSetID, p - 1) && IsErased(ErasureSetID, p) && IsErased(ErasureSetID, p + 1)) {
    return UPDATE;
  }
  auto const from_subsymbol = StripeUnitID;
  auto const from_symbol = from_subsymbol / m_StripeUnitsPerSymbol;
  auto const to_subsymbol = StripeUnitID + Subsymbols2Encode;
  assert(Subsymbols2Encode != 0);
  auto const to_symbol = (to_subsymbol - 1) / m_StripeUnitsPerSymbol + 1;
  assert(to_symbol * m_StripeUnitSize >= to_subsymbol);
  for (unsigned const disk : iota(from_symbol, to_symbol)) {
    // If any of the target disks is erased, we would have to restore original contents,
    // which is likely to cause the entire codeword to be read.
    if (IsErased(ErasureSetID, disk)) {
      return READ_WRITE;
    }
  }
  for (unsigned const i : iota(from_subsymbol, to_subsymbol)) {
    [[maybe_unused]] auto const symbol = i / m_StripeUnitsPerSymbol;
    assert(from_symbol <= symbol && symbol < to_symbol);
  }
  if (4 * Subsymbols2Encode < 3 * m_StripeUnitsPerSymbol * m_Dimension) {
    return UPDATE;
  } else {
    return READ_WRITE;
  }
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
  auto const symbolSize = SymbolSize();
  bool ok = true;

  // If all the checksum disks are erased, there's nothing to talk about.
  // Just update the symbols.
  if (IsErased(ErasureSetID, p - 1) && IsErased(ErasureSetID, p) && IsErased(ErasureSetID, p + 1)) {
    for (unsigned const offset : iota(Units2Update)) {
      auto const i = StripeUnitID + offset;
      auto const symbol = i / m_StripeUnitsPerSymbol;
      auto const subSymbol = i % m_StripeUnitsPerSymbol;
      assert(!IsErased(ErasureSetID, symbol));
      assert(symbol < m_Dimension);
      ok &= WriteSubsymbols(StripeID, ErasureSetID, symbol, pData, subSymbol, 1);
      pData += m_StripeUnitSize;
    }
    return ok;
  }

  struct LazyChecksum {
    AlignedBuffer checksum;
    std::vector<bool> initialized;
    unsigned disk;
  };

  auto const init_lazy_checksum = [this, symbolSize](unsigned const pos) -> LazyChecksum {
    return LazyChecksum{.checksum = AlignedBuffer(symbolSize),
                        .initialized = std::vector<bool>(m_StripeUnitsPerSymbol),
                        .disk = pos};
  };
  auto const maybe_init_lazy_checksum = [this, ErasureSetID,
                                         &init_lazy_checksum](unsigned const pos) -> LazyChecksum {
    return IsErased(ErasureSetID, pos) ? LazyChecksum{.checksum = AlignedBuffer(),
                                                      .initialized = std::vector<bool>(),
                                                      .disk = pos}
                                       : init_lazy_checksum(pos);
  };

  auto row = init_lazy_checksum(p - 1);
  auto diag = maybe_init_lazy_checksum(p);
  auto adiag = maybe_init_lazy_checksum(p + 1);

  auto const add_to_diag = [this, &ok, symbolSize, StripeID, ErasureSetID](
                               LazyChecksum& lazyChecksum, unsigned pos, unsigned char const* src) {
    auto& [checksum, initialized, checksumDisk] = lazyChecksum;
    if (!checksum.size()) {
      assert(initialized.empty());
      return;
    }
    assert(checksum.size() == symbolSize);
    assert(initialized.size() == m_StripeUnitsPerSymbol);
    if (pos >= initialized.size()) {
      assert(pos == m_StripeUnitsPerSymbol);
      return;
    }
    auto const dst = checksum.data() + pos * m_StripeUnitSize;
    if (!initialized[pos]) {
      ok &= ReadSubsymbols(StripeID, ErasureSetID, checksumDisk, dst, pos, 1);
      initialized[pos] = true;
    }
    XOR(dst, src, m_StripeUnitSize);
  };

  auto buf = AlignedBuffer(m_StripeUnitSize);
  for (unsigned const offset : iota(Units2Update)) {
    auto const i = StripeUnitID + offset;
    auto const symbol = i / m_StripeUnitsPerSymbol;
    auto const subSymbol = i % m_StripeUnitsPerSymbol;
    assert(!IsErased(ErasureSetID, symbol));
    assert(symbol < m_Dimension);
    auto const d = DiagNum(false, symbol, subSymbol);
    auto const ad = DiagNum(true, symbol, subSymbol);
    ok &= ReadSubsymbols(StripeID, ErasureSetID, symbol, buf.data(), subSymbol, 1);
    XOR(buf.data(), pData, m_StripeUnitSize);
    auto const row_dst = row.checksum.data() + subSymbol * m_StripeUnitSize;
    if (row.initialized[subSymbol]) {
      XOR(row_dst, buf.data(), m_StripeUnitSize);
    } else {
      memcpy(row_dst, buf.data(), m_StripeUnitSize);
      row.initialized[subSymbol] = true;
    }
    add_to_diag(diag, d, buf.data());
    add_to_diag(adiag, ad, buf.data());
    ok &= WriteSubsymbols(StripeID, ErasureSetID, symbol, pData, subSymbol, 1);
    pData += m_StripeUnitSize;
  }

  for (unsigned const i : iota(m_StripeUnitsPerSymbol)) {
    if (row.initialized[i]) {
      auto const d = DiagNum(false, row.disk, i);
      auto const ad = DiagNum(true, row.disk, i);
      auto const src = row.checksum.data() + i * m_StripeUnitSize;
      add_to_diag(diag, d, src);
      add_to_diag(adiag, ad, src);
    }
  }

  auto const write_diag = [this, &ok, symbolSize, StripeID,
                           ErasureSetID](LazyChecksum const& lazyChecksum) {
    auto& [checksum, initialized, checksumDisk] = lazyChecksum;
    if (!checksum.size()) {
      assert(initialized.empty());
      return;
    }
    assert(!IsErased(ErasureSetID, checksumDisk));
    assert(checksum.size() == symbolSize);
    assert(initialized.size() == m_StripeUnitsPerSymbol);
    for (unsigned const i : iota(m_StripeUnitsPerSymbol)) {
      if (initialized[i]) {
        ok &= WriteSubsymbols(StripeID, ErasureSetID, checksumDisk,
                              checksum.data() + i * m_StripeUnitSize, i, 1);
      }
    }
  };

  if (!IsErased(ErasureSetID, row.disk)) {
    for (unsigned const i : iota(m_StripeUnitsPerSymbol)) {
      if (row.initialized[i]) {
        ok &= ReadSubsymbols(StripeID, ErasureSetID, row.disk, buf.data(), i, 1);
        XOR(buf.data(), row.checksum.data() + i * m_StripeUnitSize, m_StripeUnitSize);
        ok &= WriteSubsymbols(StripeID, ErasureSetID, row.disk, buf.data(), i, 1);
      }
    }
  }
  write_diag(diag);
  write_diag(adiag);

  return ok;
}

/// check if the codeword is consistent
bool CRTPProcessor::CheckCodeword(unsigned long long StripeID,  /// the stripe to be checked
                                  unsigned ErasureSetID,  /// identifies the load balancing offset
                                  size_t ThreadID         /// identifies the calling thread
) {
  // TODO: it is actually possible to do *some* verification if at most 1 RAID4 symbol is erased
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

void CRTPProcessor::AddToDiag(AlignedBuffer& diag,
                              bool isAnti,
                              std::size_t symbolId,
                              const AlignedBuffer& symbol) const {
  assert(diag.size() == SymbolSize() || diag.size() == SymbolSize() + m_StripeUnitSize);
  for (std::size_t subsymbolID = 0; subsymbolID < m_StripeUnitsPerSymbol; ++subsymbolID) {
    auto const d = DiagNum(isAnti, symbolId, subsymbolID);
    assert(d <= m_StripeUnitsPerSymbol);
    auto const offset = d * m_StripeUnitSize;
    if (offset < diag.size()) {
      XOR(&diag[offset], &symbol[subsymbolID * m_StripeUnitSize], m_StripeUnitSize);
    }
  }
}

void CRTPProcessor::AddToDiags(AlignedBuffer& diag,
                               AlignedBuffer& adiag,
                               std::size_t symbolId,
                               const AlignedBuffer& symbol) const {
  AddToDiag(diag, false, symbolId, symbol);
  AddToDiag(adiag, true, symbolId, symbol);
}
