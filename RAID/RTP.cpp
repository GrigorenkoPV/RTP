#include "RTP.h"

CRTPProcessor::CRTPProcessor(RTPParams* P) : CRAIDProcessor(0, 0, nullptr, 0) {
  // TODO
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
