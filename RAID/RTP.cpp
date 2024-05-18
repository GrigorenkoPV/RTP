#include "RTP.h"
#include <stdexcept>

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

AlignedBuffer CRTPProcessor::ReadSymbol(unsigned long long int StripeID,
                                        unsigned int ErasureSetID,
                                        unsigned int SymbolID) {
  auto result = AlignedBuffer(SymbolSize());
  ReadSymbol(StripeID, ErasureSetID, SymbolID, result);
  return result;
}

AlignedBuffer& CRTPProcessor::ReadSymbol(unsigned long long int StripeID,
                                         unsigned int ErasureSetID,
                                         unsigned int SymbolID,
                                         AlignedBuffer& out) {
  assert(out.size() == SymbolSize());
  auto ok = ReadSymbol(StripeID, ErasureSetID, SymbolID, out.data());
  if (!ok) {
    throw std::runtime_error("Error reading data");
  }
  return out;
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
    // TODO
    return false;
  }
}

bool CRTPProcessor::DecodeDataSubsymbols(unsigned long long int StripeID,
                                         unsigned int ErasureSetID,
                                         unsigned int SymbolID,
                                         unsigned int SubsymbolID,
                                         unsigned int Subsymbols2Decode,
                                         unsigned char* pDest,
                                         size_t ThreadID) {
  // If the symbol is OK, just read from it.
  if (!IsErased(ErasureSetID, SymbolID)) {
    return ReadStripeUnit(StripeID, ErasureSetID, SymbolID, SubsymbolID, Subsymbols2Decode, pDest);
  }
  auto const NumErasedRAID4Symbols =
      GetNumOfErasures(ErasureSetID) - IsErased(ErasureSetID, p) - IsErased(ErasureSetID, p + 1);

  if (SymbolID < p) {
    // We have a RAID4 disk...
    if (NumErasedRAID4Symbols == 1) {
      // ...and we can use row parity
      // TODO: restore using row parity
    }
  } else {
    // We have an (anti)diagonal disk...
    if (NumErasedRAID4Symbols <= 1) {
      // ...and we can recompute it
      // TODO: restore RAID4 and recompute (anti)diagonal
    }
  }

  // No luck, we have to do
  auto symbol = AlignedBuffer(SymbolSize());
  auto const ok = DecodeDataSymbols(StripeID, ErasureSetID, SymbolID, 1, symbol.data(), ThreadID);
  if (!ok) {
    return false;
  }
  memcpy(pDest, symbol.data() + SubsymbolID * m_StripeUnitSize,
         Subsymbols2Decode * m_StripeUnitSize);
  return true;
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
  // TODO
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
    return ReadSymbol(StripeID, ErasureSetID, symbolId, buffer);
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
