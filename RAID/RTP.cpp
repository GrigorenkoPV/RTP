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

CRTPProcessor::CRTPProcessor(RTPParams* P)
    : CRAIDProcessor(P->CodeDimension + 3, P->CodeDimension, P, sizeof(*P)),
      p(P->CodeDimension + 1) {
  if (!isPrime(p)) {
    throw std::invalid_argument("Dimension+1 should be prime");
  }
}

bool CRTPProcessor::IsCorrectable(unsigned int ErasureSetID) {
  // TODO
  return false;
}

bool CRTPProcessor::DecodeDataSubsymbols(unsigned long long int StripeID,
                                         unsigned int ErasureSetID,
                                         unsigned int SymbolID,
                                         unsigned int SubsymbolID,
                                         unsigned int Subsymbols2Decode,
                                         unsigned char* pDest,
                                         size_t ThreadID) {
  // TODO
  return false;
}

bool CRTPProcessor::DecodeDataSymbols(unsigned long long int StripeID,
                                      unsigned int ErasureSetID,
                                      unsigned int SymbolID,
                                      unsigned int Symbols2Decode,
                                      unsigned char* pDest,
                                      size_t ThreadID) {
  // TODO
  return false;
}

bool CRTPProcessor::EncodeStripe(unsigned long long int StripeID,
                                 unsigned int ErasureSetID,
                                 const unsigned char* pData,
                                 size_t ThreadID) {
  // TODO
  return false;
}

bool CRTPProcessor::UpdateInformationSymbols(unsigned long long int StripeID,
                                             unsigned int ErasureSetID,
                                             unsigned int StripeUnitID,
                                             unsigned int Units2Update,
                                             const unsigned char* pData,
                                             size_t ThreadID) {
  // TODO
  return false;
}

bool CRTPProcessor::CheckCodeword(unsigned long long int StripeID,
                                  unsigned int ErasureSetID,
                                  size_t ThreadID) {
  // TODO
  return false;
}
