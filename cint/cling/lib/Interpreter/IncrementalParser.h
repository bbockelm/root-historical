//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id$
// author:  Axel Naumann <axel@cern.ch>
//------------------------------------------------------------------------------

#ifndef CLING_INCREMENTAL_PARSER_H
#define CLING_INCREMENTAL_PARSER_H

#include "DeclCollector.h"
#include "CompilationOptions.h"
#include "Transaction.h"
#include "TransactionTransformer.h"

#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Basic/SourceLocation.h"

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringRef.h"

#include <vector>

namespace llvm {
  struct GenericValue;
  class MemoryBuffer;
}

namespace clang {
  class ASTConsumer;
  class CodeGenerator;
  class CompilerInstance;
  class Decl;
  class FileID;
  class FunctionDecl;
  class Parser;
  class Sema;
  class SourceLocation;
}


namespace cling {
  class CIFactory;
  class DeclCollector;
  class ExecutionContext;
  class Interpreter;

  ///\brief Responsible for the incremental parsing and compilation of input.
  ///
  /// The class manages the entire process of compilation line-by-line by 
  /// appending the compiled delta to clang'a AST. It provides basic operations
  /// on the already compiled code. See cling::Transaction class.
  ///
  class IncrementalParser {
  private:
    // our interpreter context
    Interpreter* m_Interpreter;

    // compiler instance.
    llvm::OwningPtr<clang::CompilerInstance> m_CI;

    // parser (incremental)
    llvm::OwningPtr<clang::Parser> m_Parser;

    // enable/disable dynamic scope
    bool m_DynamicLookupEnabled;

    // One buffer for each command line, owner by the source file manager
    std::vector<llvm::MemoryBuffer*> m_MemoryBuffer;

    // file ID of the memory buffer
    clang::FileID m_VirtualFileID;

    // CI owns it
    DeclCollector* m_Consumer;

    ///\brief Holds information for the all transactions.
    ///
    llvm::SmallVector<Transaction*, 64> m_Transactions;

    ///\brief Code generator
    ///
    llvm::OwningPtr<clang::CodeGenerator> m_CodeGen;

    ///\brief Contains the transaction transformers.
    ///
    llvm::SmallVector<TransactionTransformer*, 4> m_TTransformers;

  public:
    enum EParseResult {
      kSuccess,
      kSuccessWithWarnings,
      kFailed
    };
    IncrementalParser(Interpreter* interp, int argc, const char* const *argv,
                      const char* llvmdir);
    ~IncrementalParser();

    clang::CompilerInstance* getCI() const { return m_CI.get(); }
    clang::Parser* getParser() const { return m_Parser.get(); }
    clang::CodeGenerator* getCodeGenerator() const { return m_CodeGen.get(); }
    bool hasCodeGenerator() const { return m_CodeGen.get(); }
    

    /// \{
    /// \name Transaction Support

    ///\brief Starts a transaction.
    ///
    void beginTransaction(const CompilationOptions& Opts);

    ///\brief Finishes a transaction.
    ///
    void endTransaction() const;

    ///\brief Commits the current transaction if it was compete. I.e pipes it 
    /// through the consumer chain, including codegen.
    ///
    void commitCurrentTransaction();

    ///\brief Reverts the AST into its previous state.
    ///
    /// If one of the declarations caused error in clang it is rolled back from
    /// the AST. This is essential feature for the error recovery subsystem.
    ///
    ///\param[in] T - The transaction to be reverted from the AST
    ///
    void rollbackTransaction(Transaction* T) const; 

    ///\brief Returns the last transaction the incremental parser saw.
    ///
    Transaction* getLastTransaction() { 
      return m_Transactions.back(); 
    }

    ///\brief Returns the last transaction the incremental parser saw.
    ///
    const Transaction* getLastTransaction() const { 
      return m_Transactions.back(); 
    }

    /// \}

    ///\brief Compiles the given input with the given compilation options.
    ///
    EParseResult Compile(llvm::StringRef input, const CompilationOptions& Opts);

    ///\brief Parses the given input without calling the custom consumers and 
    /// code generation.
    ///
    /// I.e changes to the decls in the transaction commiting it will cause 
    /// different executable code.
    ///
    ///\param[in] input - The code to parse.
    ///\returns The transaction coresponding to the input.
    ///
    Transaction* Parse(llvm::StringRef input);

    void enablePrintAST(bool print /*=true*/) {
      //m_Consumer->getCompilationOpts().Debug = print;
    }
    void enableDynamicLookup(bool value = true);
    bool isDynamicLookupEnabled() const { return m_DynamicLookupEnabled; }

  private:
    void CreateSLocOffsetGenerator();
    EParseResult ParseInternal(llvm::StringRef input);
  };
} // end namespace cling
#endif // CLING_INCREMENTAL_PARSER_H
