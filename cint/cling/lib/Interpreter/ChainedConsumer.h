//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id$
// author:  Axel Naumann <axel@cern.ch>
//------------------------------------------------------------------------------

#ifndef CLING_CHAINED_CONSUMER_H
#define CLING_CHAINED_CONSUMER_H

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include "clang/AST/DeclGroup.h"
#include "clang/Sema/SemaConsumer.h"

#include <bitset>

namespace clang {
  class ASTContext;
  class DeclContext;
  class FunctionDecl;
  class NamedDecl;
  class NamespaceDecl;
}

namespace cling {

  class ChainedMutationListener;
  class ChainedDeserializationListener;
  class VerifyingSemaConsumer;

  class ChainedConsumer: public clang::SemaConsumer {

  public:
    enum EConsumerIndex {
      kEvaluateTSynthesizer,
      kDeclExtractor,
      kValuePrinterSynthesizer,
      kASTDumper,
      kPCHGenerator,
      kCodeGenerator,
      kConsumersCount
    };
    ChainedConsumer();
    virtual ~ChainedConsumer();

    // ASTConsumer
    virtual void Initialize(clang::ASTContext& Context);
    virtual void HandleTopLevelDecl(clang::DeclGroupRef D);
    virtual void HandleInterestingDecl(clang::DeclGroupRef D);
    virtual void HandleTagDeclDefinition(clang::TagDecl* D);
    virtual void HandleVTable(clang::CXXRecordDecl* RD, bool DefinitionRequired);
    virtual void CompleteTentativeDefinition(clang::VarDecl* D);
    virtual void HandleTranslationUnit(clang::ASTContext& Ctx);

    virtual clang::ASTMutationListener* GetASTMutationListener();
    virtual clang::ASTDeserializationListener* GetASTDeserializationListener();
    virtual void PrintStats();
    
    // SemaConsumer
    virtual void InitializeSema(clang::Sema& S);
    virtual void ForgetSema();

    // Transaction Support
    bool IsInTransaction() { return m_InTransaction; }
    
    void Add(EConsumerIndex I, clang::ASTConsumer* C);
    void RecoverFromError();
    clang::ASTConsumer** getConsumers() { 
      return Consumers; 
    }

    bool Exists(EConsumerIndex I) {
      return Consumers[I] != 0;
    }

    clang::ASTConsumer* getConsumer(EConsumerIndex I) {
      return Consumers[I];
    }

    bool EnableConsumer(EConsumerIndex I) {
      assert(Exists(I) && "Cannot disable. Consumer not set!");
      bool PrevousState = Enabled[I];
      Enabled.set(I);
      return PrevousState;
    }

    bool DisableConsumer(EConsumerIndex I) {
      assert(Exists(I) && "Cannot disable. Consumer not set!");
      //assert(I != kCodeGenerator && "You shouldn't disable codegen!");
      bool PrevousState = Enabled[I];
      Enabled.reset(I);
      return PrevousState;
    }

    void RestorePreviousState(EConsumerIndex I, bool Previous) {
      assert(Exists(I) && "Cannot disable. Consumer not set!");
      Enabled.set(I, Previous);
    }

    bool IsConsumerEnabled(EConsumerIndex I) {
      return Enabled[I];
    }

    bool IsQueueing() { return m_Queueing; }

    void DumpQueue();
    void Update(VerifyingSemaConsumer* ESSC);
  private:
    clang::ASTConsumer* Consumers[kConsumersCount]; // owns them
    std::bitset<kConsumersCount> Enabled;
    llvm::OwningPtr<ChainedMutationListener> MutationListener;
    llvm::OwningPtr<ChainedDeserializationListener> DeserializationListener;
    bool m_InTransaction;
    clang::ASTContext* m_Context;
    clang::Sema* m_Sema;
    enum HandlerIndex {
      kTopLevelDecl,
      kInterestingDecl,
      kTagDeclDefinition,
      kVTable,      
      kCompleteTentativeDefinition
    };
    struct DGRInfo {
      clang::DeclGroupRef D;
      HandlerIndex I;
      DGRInfo(clang::DeclGroupRef d, HandlerIndex i) : D(d), I(i){}
    };
    llvm::SmallVector<DGRInfo, 64> DeclsQueue;
    bool m_Queueing;

    bool isOnScopeChains(clang::NamedDecl* D);
    void RevertVarDecl(clang::VarDecl* VD);
    void RevertFunctionDecl(clang::FunctionDecl* FD);
    void RevertNamespaceDecl(clang::NamespaceDecl* NSD);

    friend class IncrementalParser;
  };
} // namespace cling

#endif
