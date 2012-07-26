//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id$
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//------------------------------------------------------------------------------

#ifndef CLING_DECL_COLLECTOR_H
#define CLING_DECL_COLLECTOR_H

#include "clang/AST/ASTConsumer.h"

namespace clang {
  class ASTContext;
  class DeclGroupRef;
}

namespace cling {

  class Transaction;

  class DeclCollector: public clang::ASTConsumer {
  private:
    Transaction* m_CurTransaction;

  public:
    DeclCollector() : m_CurTransaction(0) {}
    virtual ~DeclCollector();

    /// \{
    /// \name ASTConsumer overrides

    virtual bool HandleTopLevelDecl(clang::DeclGroupRef DGR);
    virtual void HandleInterestingDecl(clang::DeclGroupRef DGR);
    virtual void HandleTagDeclDefinition(clang::TagDecl* TD);
    virtual void HandleVTable(clang::CXXRecordDecl* RD,
                              bool DefinitionRequired);
    virtual void CompleteTentativeDefinition(clang::VarDecl* VD);
    virtual void HandleTranslationUnit(clang::ASTContext& Ctx);

    /// \}

    /// \{
    /// \name Transaction Support

    Transaction* getTransaction() { return m_CurTransaction; }
    const Transaction* getTransaction() const { return m_CurTransaction; }
    void setTransaction(Transaction* curT) { m_CurTransaction = curT; }
    void setTransaction(const Transaction* curT) { 
      m_CurTransaction = const_cast<Transaction*>(curT); 
    }

    /// \}
  };
} // namespace cling

#endif // CLING_DECL_COLLECTOR_H
