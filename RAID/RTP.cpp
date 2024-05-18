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
    unsigned SymbolID,            /// the first symbol to be processed
    unsigned Symbols2Decode,      /// the number of subsymbols within this symbol to be decoded
    unsigned char* pDest,         /// destination array.
    size_t ThreadID               /// the ID of the calling thread
) {
  if (GetNumOfErasures(ErasureSetID)) {
    return false;
  }
  auto Result = true;
  // read the data as is
  for (unsigned S = SymbolID; S < SymbolID + Symbols2Decode; S++, pDest += SymbolSize()) {
    Result &= ReadSymbol(StripeID, ErasureSetID, S, pDest);
  }
  return Result;
}

bool CRTPProcessor::DecodeDataSubsymbols(unsigned long long int StripeID,
                                         unsigned int ErasureSetID,
                                         unsigned int SymbolID,
                                         unsigned int SubsymbolID,
                                         unsigned int Subsymbols2Decode,
                                         unsigned char* pDest,
                                         size_t ThreadID) {
  if (GetNumOfErasures(ErasureSetID)) {
    return false;
  }
  return ReadStripeUnit(StripeID, ErasureSetID, SymbolID, SubsymbolID, Subsymbols2Decode, pDest);
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
  auto next_symbol = [&buffer, symbolId = 0, pData, symbol_size,
                      &write_symbol]() mutable -> AlignedBuffer const& {
    memmove(buffer.data(), pData + symbolId * symbol_size, symbol_size);
    write_symbol(symbolId, buffer);
    ++symbolId;
    return buffer;
  };
  auto [row, diag, adiag] = row_diag_adiag(next_symbol, symbol_size);
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
  std::size_t symbolId = 0;
  auto next_symbol = [this, StripeID, ErasureSetID, &buffer, &symbolId]() -> AlignedBuffer const& {
    return ReadSymbol(StripeID, ErasureSetID, symbolId++, buffer);
  };
  auto [row, diag, adiag] = row_diag_adiag(next_symbol, symbol_size);
  return row == next_symbol() && diag == next_symbol() && adiag == next_symbol();
}
