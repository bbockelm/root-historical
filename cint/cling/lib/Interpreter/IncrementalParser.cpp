//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id$
// author:  Axel Naumann <axel@cern.ch>
//------------------------------------------------------------------------------

#include "IncrementalParser.h"
#include "ASTNodeEraser.h"

#include "ASTDumper.h"
#include "ChainedConsumer.h"
#include "DeclExtractor.h"
#include "DynamicLookup.h"
#include "ValuePrinterSynthesizer.h"
#include "cling/Interpreter/CIFactory.h"
#include "cling/Interpreter/Interpreter.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Basic/FileManager.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Parse/Parser.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Serialization/ASTWriter.h"

#include "llvm/LLVMContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_os_ostream.h"

#include <ctime>
#include <iostream>
#include <stdio.h>
#include <sstream>

using namespace clang;

namespace cling {
  IncrementalParser::IncrementalParser(Interpreter* interp,
                                       int argc, const char* const *argv,
                                       const char* llvmdir):
    m_Interpreter(interp),
    m_DynamicLookupEnabled(false),
    m_Consumer(0),
    m_SyntaxOnly(false)
  {
    CompilerInstance* CI
      = CIFactory::createCI(llvm::MemoryBuffer::getMemBuffer("", "CLING"),
                            argc, argv, llvmdir);
    assert(CI && "CompilerInstance is (null)!");
    m_CI.reset(CI);
    m_SyntaxOnly
      = CI->getFrontendOpts().ProgramAction == clang::frontend::ParseSyntaxOnly;

    CreateSLocOffsetGenerator();

    m_Consumer = dyn_cast<ChainedConsumer>(&CI->getASTConsumer());
    assert(m_Consumer && "Expected ChainedConsumer!");
    // Add consumers to the ChainedConsumer, which owns them
    EvaluateTSynthesizer* ES = new EvaluateTSynthesizer(interp);
    m_Consumer->Add(ChainedConsumer::kEvaluateTSynthesizer, ES);

    DeclExtractor* DE = new DeclExtractor();
    m_Consumer->Add(ChainedConsumer::kDeclExtractor, DE);

    ValuePrinterSynthesizer* VPS = new ValuePrinterSynthesizer(interp);
    m_Consumer->Add(ChainedConsumer::kValuePrinterSynthesizer, VPS);
    m_Consumer->Add(ChainedConsumer::kASTDumper, new ASTDumper());
    if (!m_SyntaxOnly) {
      CodeGenerator* CG = CreateLLVMCodeGen(CI->getDiagnostics(),
                                            "cling input",
                                            CI->getCodeGenOpts(),
                                            *m_Interpreter->getLLVMContext()
                                            );
      assert(CG && "No CodeGen?!");
      m_Consumer->Add(ChainedConsumer::kCodeGenerator, CG);
    }
    m_Parser.reset(new Parser(CI->getPreprocessor(), CI->getSema(),
                              false /*skipFuncBodies*/));
    CI->getPreprocessor().EnterMainSourceFile();
    // Initialize the parser after we have entered the main source file.
    m_Parser->Initialize();
    // Perform initialization that occurs after the parser has been initialized
    // but before it parses anything. Initializes the consumers too.
    CI->getSema().Initialize();
  }

  void IncrementalParser::beginTransaction() {
    Transaction* NewCurT = new Transaction();
    Transaction* OldCurT = m_Consumer->getTransaction();
    m_Consumer->setTransaction(NewCurT);
    // If we are in the middle of transaction and we see another begin 
    // transaction - it must be nested transaction.
    if (OldCurT && !OldCurT->isCompleted()) {
      OldCurT->addNestedTransaction(NewCurT);
      return;
    }

    m_Transactions.push_back(NewCurT);
  }

  void IncrementalParser::endTransaction() const {
    Transaction* CurT = m_Consumer->getTransaction();
    CurT->setCompleted();
    const DiagnosticsEngine& Diags = getCI()->getSema().getDiagnostics();

    //TODO: Make the enum orable.
    if (Diags.getNumWarnings() > 0)
      CurT->setIssuedDiags(Transaction::kWarnings);

    if (Diags.hasErrorOccurred() || Diags.hasFatalErrorOccurred())
      CurT->setIssuedDiags(Transaction::kErrors);

      
    if (CurT->isNestedTransaction()) {
      assert(!CurT->getParent()->isCompleted() 
             && "Parent transaction completed!?");
      CurT = m_Consumer->getTransaction()->getParent();
    }
  }

  void IncrementalParser::commitTransaction(const Transaction* T) const {
    assert(T->isCompleted() && "Transaction not ended!?");
    const Transaction* OldT = m_Consumer->getTransaction();
    m_Consumer->setTransaction(T);
    commitCurrentTransaction();
    m_Consumer->setTransaction(OldT);
  }

  void IncrementalParser::commitCurrentTransaction() const {
    Transaction* CurT = m_Consumer->getTransaction();
    assert(CurT->isCompleted() && "Transaction not ended!?");

    // Check for errors...
    if (CurT->getIssuedDiags() == Transaction::kErrors) {
      rollbackTransaction(CurT);
      return;
    }

    // We are sure it's safe to pipe it through
    for (Transaction::const_iterator I = CurT->decls_begin(), 
           E = CurT->decls_end(); I != E; ++I) {
      // if an error was seen somewhere in the consumer chain rollback 
      if (!m_Consumer->HandleTopLevelDecl(*I)) {
        rollbackTransaction(CurT);
        return;
      }      
    }

    // Pull all template instantiations in that came from the consumers.
    getCI()->getSema().PerformPendingInstantiations();

    m_Consumer->HandleTranslationUnit(getCI()->getASTContext());
    CurT->setState(Transaction::kCommitted);
  }

  void IncrementalParser::rollbackTransaction(Transaction* T) const {
    ASTNodeEraser NodeEraser(&getCI()->getSema());

    for (Transaction::const_reverse_iterator I = T->rdecls_begin(),
           E = T->rdecls_end(); I != E; ++I) {
      const DeclGroupRef& DGR = (*I);

      for (DeclGroupRef::const_iterator
             Di = DGR.end() - 1, E = DGR.begin() - 1; Di != E; --Di) {
        DeclContext* DC = (*Di)->getDeclContext();
        assert(DC == (*Di)->getLexicalDeclContext() && \
               "Cannot handle that yet");

        // Get rid of the declaration. If the declaration has name we should
        // heal the lookup tables as well
        bool Successful = NodeEraser.RevertDecl(*Di);
        assert(Successful && "Cannot handle that yet!");
      }
    }

    T->setState(Transaction::kRolledBack);
  }
  

  // Each input line is contained in separate memory buffer. The SourceManager
  // assigns sort-of invalid FileID for each buffer, i.e there is no FileEntry
  // for the MemoryBuffer's FileID. That in turn is problem because invalid
  // SourceLocations are given to the diagnostics. Thus the diagnostics cannot
  // order the overloads, for example
  //
  // Our work-around is creating a virtual file, which doesn't exist on the disk
  // with enormous size (no allocation is done). That file has valid FileEntry
  // and so on... We use it for generating valid SourceLocations with valid
  // offsets so that it doesn't cause any troubles to the diagnostics.
  //
  // +---------------------+
  // | Main memory buffer  |
  // +---------------------+
  // |  Virtual file SLoc  |
  // |    address space    |<-----------------+
  // |         ...         |<------------+    |
  // |         ...         |             |    |
  // |         ...         |<----+       |    |
  // |         ...         |     |       |    |
  // +~~~~~~~~~~~~~~~~~~~~~+     |       |    |
  // |     input_line_1    | ....+.......+..--+
  // +---------------------+     |       |
  // |     input_line_2    | ....+.....--+
  // +---------------------+     |
  // |          ...        |     |
  // +---------------------+     |
  // |     input_line_N    | ..--+
  // +---------------------+
  //
  void IncrementalParser::CreateSLocOffsetGenerator() {
    SourceManager& SM = getCI()->getSourceManager();
    FileManager& FM = SM.getFileManager();
    const FileEntry* FE
      = FM.getVirtualFile("Interactrive/InputLineIncluder", 1U << 15U, time(0));
    m_VirtualFileID = SM.createFileID(FE, SourceLocation(), SrcMgr::C_User);

    assert(!m_VirtualFileID.isInvalid() && "No VirtualFileID created?");
  }

  IncrementalParser::~IncrementalParser() {
     if (GetCodeGenerator()) {
       GetCodeGenerator()->ReleaseModule();
     }
     for (unsigned i = 0; i < m_Transactions.size(); ++i)
       delete m_Transactions[i];
  }

  void IncrementalParser::Initialize() {
    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
  }

  IncrementalParser::EParseResult
  IncrementalParser::Compile(llvm::StringRef input,
                             const CompilationOptions& Opts) {

    m_Consumer->pushCompilationOpts(Opts);
    EParseResult Result = Compile(input);
    m_Consumer->popCompilationOpts();

    return Result;
  }

  void IncrementalParser::Parse(llvm::StringRef input,
                                llvm::SmallVector<DeclGroupRef, 4>& DGRs) {

    beginTransaction();
    Parse(input);
    endTransaction();
    const Transaction* T = m_Consumer->getTransaction();
    DGRs.append(T->decls_begin(), T->decls_end());
  }

  IncrementalParser::EParseResult
  IncrementalParser::Compile(llvm::StringRef input) {
    // Just in case when Parse is called, we want to complete the transaction
    // coming from parse and then start new one.
    //m_Consumer->HandleTranslationUnit(getCI()->getASTContext());

    // Reset the module builder to clean up global initializers, c'tors, d'tors:
    if (GetCodeGenerator()) {
      GetCodeGenerator()->Initialize(getCI()->getASTContext());
    }

    beginTransaction();
    EParseResult Result = Parse(input);
    endTransaction();

    // Check for errors coming from our custom consumers.
    DiagnosticConsumer& DClient = m_CI->getDiagnosticClient();
    DClient.BeginSourceFile(getCI()->getLangOpts(),
                            &getCI()->getPreprocessor());
    commitCurrentTransaction();

    DClient.EndSourceFile();
    m_CI->getDiagnostics().Reset();

    if (!m_SyntaxOnly) {
      m_Interpreter->runStaticInitializersOnce();
    }

    return Result;
  }

  // Add the input to the memory buffer, parse it, and add it to the AST.
  IncrementalParser::EParseResult
  IncrementalParser::Parse(llvm::StringRef input) {
    if (input.empty()) return IncrementalParser::kSuccess;

    Preprocessor& PP = m_CI->getPreprocessor();
    DiagnosticConsumer& DClient = m_CI->getDiagnosticClient();

    if (!PP.getCurrentLexer()) {
       PP.EnterSourceFile(m_CI->getSourceManager().getMainFileID(),
                          0, SourceLocation());
    }
    PP.enableIncrementalProcessing();

    DClient.BeginSourceFile(m_CI->getLangOpts(), &PP);

    std::ostringstream source_name;
    source_name << "input_line_" << (m_MemoryBuffer.size() + 1);
    llvm::MemoryBuffer* MB
       = llvm::MemoryBuffer::getMemBufferCopy(input, source_name.str());
    m_MemoryBuffer.push_back(MB);
    SourceManager& SM = getCI()->getSourceManager();

    // Create SourceLocation, which will allow clang to order the overload
    // candidates for example
    SourceLocation NewLoc = SM.getLocForStartOfFile(m_VirtualFileID);
    NewLoc = NewLoc.getLocWithOffset(m_MemoryBuffer.size() + 1);

    // Create FileID for the current buffer
    FileID FID = SM.createFileIDForMemBuffer(m_MemoryBuffer.back(),
                                             /*LoadedID*/0,
                                             /*LoadedOffset*/0, NewLoc);

    PP.EnterSourceFile(FID, 0, NewLoc);

    Parser::DeclGroupPtrTy ADecl;

    while (!m_Parser->ParseTopLevelDecl(ADecl)) {
      // If we got a null return and something *was* parsed, ignore it.  This
      // is due to a top-level semicolon, an action override, or a parse error
      // skipping something.
      if (ADecl)
        m_Consumer->HandleTopLevelDecl(ADecl.getAsVal<DeclGroupRef>());
    };

    // Process any TopLevelDecls generated by #pragma weak.
    for (llvm::SmallVector<Decl*,2>::iterator
           I = getCI()->getSema().WeakTopLevelDecls().begin(),
           E = getCI()->getSema().WeakTopLevelDecls().end(); I != E; ++I) {
      m_Consumer->HandleTopLevelDecl(DeclGroupRef(*I));
    }

    getCI()->getSema().PerformPendingInstantiations();

    DClient.EndSourceFile();

    PP.enableIncrementalProcessing(false);

    DiagnosticsEngine& Diag = getCI()->getSema().getDiagnostics();
    if (Diag.hasErrorOccurred())
      return IncrementalParser::kFailed;
    else if (Diag.getNumWarnings())
      return IncrementalParser::kSuccessWithWarnings;

    return IncrementalParser::kSuccess;
  }

  void IncrementalParser::enableDynamicLookup(bool value) {
    m_DynamicLookupEnabled = value;
    Sema& S = m_CI->getSema();
    if (isDynamicLookupEnabled()) {
      assert(!S.ExternalSource && "Already set Sema ExternalSource");
      S.ExternalSource = new DynamicIDHandler(&S);
    }
    else {
      delete S.ExternalSource;
      S.ExternalSource = 0;
    }
  }

  CodeGenerator* IncrementalParser::GetCodeGenerator() const {
    return
      (CodeGenerator*)m_Consumer->getConsumer(ChainedConsumer::kCodeGenerator);
  }

} // namespace cling
