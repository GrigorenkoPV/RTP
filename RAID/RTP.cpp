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

bool CRTPProcessor::Attach(CDiskArray* pArray, unsigned int ConcurrentThreads) {
  xor_buffer = AlignedBuffer(ConcurrentThreads * m_StripeUnitSize * 2);
  return CRAIDProcessor::Attach(pArray, ConcurrentThreads);
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
  // TODO
  return false;
}
