#pragma once

#include <array>
#include "AlignedBuffer.h"
#include "RAIDProcessor.h"

class CRTPProcessor : public CRAIDProcessor {
 public:
  /// initialize coding-related parameters
  explicit CRTPProcessor(RTPParams* P  /// the configuration file
  );

  ~CRTPProcessor() override = default;

 protected:
  unsigned const p;

  /// Check if it is possible to correct a given combination of erasures
  /// If yes, the method should initialize the internal data structures
  /// and be ready to do the actual erasure correction. This combination of erasures
  ///  is uniquely identified by ErasureID
  ///@return true if the specified combination of erasures is correctable
  inline bool IsCorrectable(
      unsigned ErasureSetID  /// identifies the erasure combination. This will not exceed m_Length-1
      ) override {
    return GetNumOfErasures(ErasureSetID) <= 3;
  }

  /// decode a number of payload subsymbols from a given symbol
  ///@return true on success
  bool DecodeDataSubsymbols(
      unsigned long long StripeID,  /// the stripe to be processed
      unsigned ErasureSetID,        /// identifies the load balancing offset
      unsigned SymbolID,            /// the symbol to be processed
      unsigned SubsymbolID,         /// the first subsymbol to be processed
      unsigned Subsymbols2Decode,   /// the number of subsymbols within this symbol to be decoded
      unsigned char*
          pDest,  /// destination array. Must have size at least Subsymbols2Decode*m_StripeUnitSize
      size_t ThreadID  /// the ID of the calling thread
      ) override;

 protected:
  /// decode a number of payload subsymbols from a given symbol
  ///@return true on success
  bool DecodeDataSymbols(
      unsigned long long StripeID,  /// the stripe to be processed
      unsigned ErasureSetID,        /// identifies the load balancing offset
      unsigned SymbolID,            /// the first symbol to be processed
      unsigned Symbols2Decode,      /// the number of subsymbols within this symbol to be decoded
      unsigned char*
          pDest,  /// destination array. Must have size at least Subsymbols2Decode*m_StripeUnitSize
      size_t ThreadID  /// the ID of the calling thread
      ) override;

  /// encode and write the whole CRAIDProcessorstripe
  ///@return true on success
  bool EncodeStripe(unsigned long long StripeID,  /// the stripe to be encoded
                    unsigned ErasureSetID,        /// identifies the load balancing offset
                    const unsigned char* pData,   /// the data to be envoced
                    size_t ThreadID               /// the ID of the calling thread
                    ) override;

  /// update some information symbols and the corresponding check symbols
  ///@return true on success
  bool UpdateInformationSymbols(unsigned long long StripeID,  /// the stripe to be updated,
                                unsigned ErasureSetID,  /// identifies the load balancing offset
                                unsigned StripeUnitID,  /// the first stripe unit to be updated
                                unsigned Units2Update,  /// the number of units to be updated
                                const unsigned char* pData,  /// new payload data symbols
                                size_t ThreadID              /// the ID of the calling thread
                                ) override;

  /// check if the codeword is consistent
  bool CheckCodeword(unsigned long long StripeID,  /// the stripe to be checked
                     unsigned ErasureSetID,        /// identifies the load balancing offset
                     size_t ThreadID               /// identifies the calling thread
                     ) override;

  // TODO: remove this once UpdateInformationSymbols is implemented
  inline bool GetEncodingStrategy(unsigned int ErasureSetID,
                                  unsigned int StripeUnitID,
                                  unsigned int Subsymbols2Encode) override {
    return true;
  }

 private:
  [[nodiscard]] inline size_t SymbolSize() const noexcept {
    return m_StripeUnitsPerSymbol * m_StripeUnitSize;
  }

  [[nodiscard]] bool ReadSymbol(unsigned long long StripeID,  /// the stripe to be checked
                                unsigned ErasureSetID,  /// identifies the load balancing offset,
                                unsigned SymbolID,      /// identifies the disk to be accessed
                                AlignedBuffer& out);

  [[nodiscard]] bool ReadSymbol(unsigned long long StripeID,  /// the stripe to be checked
                                unsigned ErasureSetID,  /// identifies the load balancing offset,
                                unsigned SymbolID,      /// identifies the disk to be accessed
                                unsigned char* out);

  [[nodiscard]] bool WriteSymbol(unsigned long long StripeID,  /// the stripe to be checked
                                 unsigned ErasureSetID,  /// identifies the load balancing offset,
                                 unsigned SymbolID,      /// identifies the disk to be accessed
                                 AlignedBuffer const& symbol);

  [[nodiscard]] inline std::size_t DiagNum(bool isAnti,
                                           std::size_t symbolId,
                                           std::size_t subsymbolId) const noexcept {
    if (isAnti) {
      return (p + symbolId - subsymbolId) % p;
    } else {
      return (symbolId + subsymbolId) % p;
    }
  }

  inline void AddToDiag(AlignedBuffer& diag,
                        bool isAnti,
                        std::size_t symbolId,
                        AlignedBuffer const& symbol) const {
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

  inline void AddToDiags(AlignedBuffer& diag,
                         AlignedBuffer& adiag,
                         std::size_t symbolId,
                         AlignedBuffer const& symbol) const {
    AddToDiag(diag, false, symbolId, symbol);
    AddToDiag(adiag, true, symbolId, symbol);
  }

  [[nodiscard]] unsigned int GetNumErasedRaid4Symbols(unsigned int ErasureSetID) const;
  [[nodiscard]] std::array<int, 3> GetErasedSymbols(unsigned int ErasureSetID) const;
};
