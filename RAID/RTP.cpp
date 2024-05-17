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

AlignedBuffer CRTPProcessor::read_symbol(unsigned long long int StripeID,
                                         unsigned int ErasureSetID,
                                         unsigned int SymbolID) {
  auto result = AlignedBuffer(SymbolSize());
  read_symbol(StripeID, ErasureSetID, SymbolID, result);
  return result;
}

AlignedBuffer& CRTPProcessor::read_symbol(unsigned long long int StripeID,
                                          unsigned int ErasureSetID,
                                          unsigned int SymbolID,
                                          AlignedBuffer& out) {
  assert(out.size() == SymbolSize());
  ReadStripeUnit(StripeID, ErasureSetID, SymbolID, 0, m_StripeUnitsPerSymbol, out.data());
  return out;
}

/// decode a number of payload subsymbols from a given symbol
///@return true on success
bool CRTPProcessor::DecodeDataSymbols(
    unsigned long long StripeID,  /// the stripe to be processed
    unsigned ErasureSetID,        /// identifies the load balancing offset
    unsigned SymbolID,            /// the first symbol to be processed
    unsigned Symbols2Decode,      /// the number of subsymbols within this symbol to be decoded
    unsigned char*
        pDest,  /// destination array. Must have size at least Subsymbols2Decode*m_StripeUnitSize
    size_t ThreadID  /// the ID of the calling thread
) {
  // TODO
  return false;
}

/// encode and write the whole CRAIDProcessorstripe
///@return true on success
bool CRTPProcessor::EncodeStripe(unsigned long long StripeID,  /// the stripe to be encoded
                                 unsigned ErasureSetID,  /// identifies the load balancing offset
                                 const unsigned char* pData,  /// the data to be envoced
                                 size_t ThreadID              /// the ID of the calling thread
) {
  // TODO
  return false;
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
  auto row = AlignedBuffer(symbol_size, true);
  auto diag = AlignedBuffer(symbol_size, true);
  auto adiag = AlignedBuffer(symbol_size, true);
  for (std::size_t symbolId = 0; symbolId < p; ++symbolId) {
    auto const& symbol = read_symbol(StripeID, ErasureSetID, symbolId, buffer);
    XOR(row.data(), symbol.data(), symbol_size);
    for (std::size_t subsymbolID = 0; subsymbolID < m_StripeUnitsPerSymbol; ++subsymbolID) {
      auto const d = DiagNum(symbolId, subsymbolID);
      if (d != p - 1) {
        XOR(&diag[d * m_StripeUnitSize], &symbol[subsymbolID * m_StripeUnitsPerSymbol],
            m_StripeUnitsPerSymbol);
      }
      auto const ad = ADiagNum(symbolId, subsymbolID);
      if (ad != p - 1) {
        XOR(&adiag[ad * m_StripeUnitSize], &symbol[subsymbolID * m_StripeUnitsPerSymbol],
            m_StripeUnitsPerSymbol);
      }
    }
  }

  return row.isZero() && diag == read_symbol(StripeID, ErasureSetID, p, buffer) &&
         adiag == read_symbol(StripeID, ErasureSetID, p + 1, buffer);
}
