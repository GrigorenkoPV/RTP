#pragma once

#include "RAIDProcessor.h"

class CRTPProcessor : public CRAIDProcessor {
 public:
  /// initialize coding-related parameters
  CRTPProcessor(RTPParams* P  /// the configuration file
  );

  ~CRTPProcessor() override = default;

 protected:
  bool IsCorrectable(unsigned int ErasureSetID) override;

  bool DecodeDataSubsymbols(unsigned long long int StripeID,
                            unsigned int ErasureSetID,
                            unsigned int SymbolID,
                            unsigned int SubsymbolID,
                            unsigned int Subsymbols2Decode,
                            unsigned char* pDest,
                            size_t ThreadID) override;

  bool DecodeDataSymbols(unsigned long long int StripeID,
                         unsigned int ErasureSetID,
                         unsigned int SymbolID,
                         unsigned int Symbols2Decode,
                         unsigned char* pDest,
                         size_t ThreadID) override;

  bool EncodeStripe(unsigned long long int StripeID,
                    unsigned int ErasureSetID,
                    const unsigned char* pData,
                    size_t ThreadID) override;

  bool UpdateInformationSymbols(unsigned long long int StripeID,
                                unsigned int ErasureSetID,
                                unsigned int StripeUnitID,
                                unsigned int Units2Update,
                                const unsigned char* pData,
                                size_t ThreadID) override;

  bool CheckCodeword(unsigned long long int StripeID,
                     unsigned int ErasureSetID,
                     size_t ThreadID) override;
};
