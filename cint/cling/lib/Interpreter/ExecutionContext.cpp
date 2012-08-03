//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// version: $Id$
// author:  Axel Naumann <axel@cern.ch>
//------------------------------------------------------------------------------

#include "ExecutionContext.h"

#include "cling/Interpreter/Value.h"

#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/DynamicLibrary.h"

using namespace cling;

namespace {
  class JITtedFunctionCollector : public llvm::JITEventListener {
  private:
    llvm::SmallVector<llvm::Function*, 24> m_functions;
    llvm::ExecutionEngine *m_engine;

  public:
    JITtedFunctionCollector(): m_functions(), m_engine(0) { }
    virtual ~JITtedFunctionCollector() { }

    virtual void NotifyFunctionEmitted(const llvm::Function&, void *, size_t,
                              const JITEventListener::EmittedFunctionDetails&) {
      m_functions.push_back(const_cast<llvm::Function *>(&F));
    }
    virtual void NotifyFreeingMachineCode(void* /*OldPtr*/) {}

    void UnregisterFunctionMapping(llvm::ExecutionEngine&);
  };
}


void JITtedFunctionCollector::UnregisterFunctionMapping(
                                                  llvm::ExecutionEngine &engine)
{
  for (std::vector<llvm::Function *>::reverse_iterator
         it = m_functions.rbegin(), et = m_functions.rend();
       it != et; ++it) {
    llvm::Function *ff = *it;
    engine.freeMachineCodeForFunction(ff);
    engine.updateGlobalMapping(ff, 0);
  }
  m_functions.clear();
}


std::set<std::string> ExecutionContext::m_unresolvedSymbols;
std::vector<ExecutionContext::LazyFunctionCreatorFunc_t>
  ExecutionContext::m_lazyFuncCreator;

ExecutionContext::ExecutionContext():
  m_engine(0),
  m_posInitGlobals(0),
  m_RunningStaticInits(false)
{
}

void
ExecutionContext::InitializeBuilder(llvm::Module* m)
{
  //
  //  Create an execution engine to use.
  //
  // Note: Engine takes ownership of the module.
  assert(m && "Module cannot be null");

  llvm::EngineBuilder builder(m);
  builder.setOptLevel(llvm::CodeGenOpt::Less);
  std::string errMsg;
  builder.setErrorStr(&errMsg);
  builder.setEngineKind(llvm::EngineKind::JIT);
  builder.setAllocateGVsWithCode(false);
  m_engine = builder.create();
  assert(m_engine && "Cannot initialize builder without module!");

  //m_engine->addModule(m); // Note: The engine takes ownership of the module.

  // install lazy function
  m_engine->InstallLazyFunctionCreator(NotifyLazyFunctionCreators);
}

ExecutionContext::~ExecutionContext()
{
}

void unresolvedSymbol()
{
  // throw exception?
  llvm::errs() << "ExecutionContext: calling unresolved symbol (should never happen)!\n";
}

void* ExecutionContext::HandleMissingFunction(const std::string& mangled_name)
{
  // Not found in the map, add the symbol in the list of unresolved symbols
  m_unresolvedSymbols.insert(mangled_name);

  // Avoid "ISO C++ forbids casting between pointer-to-function and
  // pointer-to-object":
  return (void*)reinterpret_cast<size_t>(unresolvedSymbol);
}

void ExecutionContext::ResetUnresolved()
{
    m_unresolvedSymbols.clear();
}

void*
ExecutionContext::NotifyLazyFunctionCreators(const std::string& mangled_name)
{
  for (std::vector<LazyFunctionCreatorFunc_t>::iterator it
         = m_lazyFuncCreator.begin(), et = m_lazyFuncCreator.end();
       it != et; ++it) {
    void* ret = (void*)((LazyFunctionCreatorFunc_t)*it)(mangled_name);
    if (ret) return ret;
  }
  return HandleMissingFunction(mangled_name);
}

void
ExecutionContext::executeFunction(llvm::StringRef funcname,
                                  llvm::GenericValue* returnValue)
{
  // Call a function without arguments, or with an SRet argument, see SRet below

  // Rewire atexit:
  llvm::Function* atExit = m_engine->FindFunctionNamed("__cxa_atexit");
  llvm::Function* clingAtExit = m_engine->FindFunctionNamed("cling_cxa_atexit");
  if (atExit && clingAtExit) {
    void* clingAtExitAddr = m_engine->getPointerToFunction(clingAtExit);
    if (clingAtExitAddr) {
      m_engine->updateGlobalMapping(atExit, clingAtExitAddr);
    }
  }

  llvm::Function* f = m_engine->FindFunctionNamed(funcname.data());
  if (!f) {
    llvm::errs() << "ExecutionContext::executeFunction: could not find function named " << funcname << '\n';
    return;
  }
  JITtedFunctionCollector listener;
  // register the listener
  m_engine->RegisterJITEventListener(&listener);
  m_engine->getPointerToFunction(f);
  // check if there is any unresolved symbol in the list
  if (!m_unresolvedSymbols.empty()) {
    for (std::set<std::string>::const_iterator i = m_unresolvedSymbols.begin(),
           e = m_unresolvedSymbols.end(); i != e; ++i) {
      llvm::errs() << "ExecutionContext::executeFunction: symbol \'" << *i << "\' unresolved!\n";
      llvm::Function *ff = m_engine->FindFunctionNamed(i->c_str());
      assert(ff && "cannot find function to free");
      m_engine->updateGlobalMapping(ff, 0);
      m_engine->freeMachineCodeForFunction(ff);
    }
    m_unresolvedSymbols.clear();
    // cleanup functions
    listener.UnregisterFunctionMapping(*m_engine);
    m_engine->UnregisterJITEventListener(&listener);
    return;
  }
  // cleanup list and unregister our listener
  m_engine->UnregisterJITEventListener(&listener);

  std::vector<llvm::GenericValue> args;
  bool wantReturn = (returnValue);

  if (f->hasStructRetAttr()) {
    // Function expects to receive the storage for the returned aggregate as
    // first argument. Make sure returnValue is able to receive it, i.e.
    // that it has the pointer set:
    assert(returnValue && GVTOP(*returnValue) && \
           "must call function returning aggregate with returnValue pointing " \
           "to the storage space for return value!");

    args.push_back(*returnValue);
    // will get set as arg0, must not assign.
    wantReturn = false;
  }

  if (wantReturn) {
    *returnValue = m_engine->runFunction(f, args);
  } else {
    m_engine->runFunction(f, args);
  }

  m_engine->freeMachineCodeForFunction(f);
}


void
ExecutionContext::runStaticInitializersOnce(llvm::Module* m) {
  assert(m && "Module must not be null");

  if (!m_engine)
    InitializeBuilder(m);

  assert(m_engine && "Code generation did not create an engine!");

  if (!m_RunningStaticInits) {
    m_RunningStaticInits = true;

    llvm::GlobalVariable* gctors
      = m->getGlobalVariable("llvm.global_ctors", true);
    if (gctors) {
      m_engine->runStaticConstructorsDestructors(false);
      gctors->eraseFromParent();
    }

    m_RunningStaticInits = false;
  }
}

void
ExecutionContext::runStaticDestructorsOnce(llvm::Module* m) {
  assert(m && "Module must not be null");
  assert(m_engine && "Code generation did not create an engine!");

  llvm::GlobalVariable* gdtors
    = m->getGlobalVariable("llvm.global_dtors", true);
  if (gdtors) {
    m_engine->runStaticConstructorsDestructors(true);
  }

}

int
ExecutionContext::verifyModule(llvm::Module* m)
{
  //
  //  Verify generated module.
  //
  bool mod_has_errs = llvm::verifyModule(*m, llvm::PrintMessageAction);
  if (mod_has_errs) {
    return 1;
  }
  return 0;
}

void
ExecutionContext::printModule(llvm::Module* m)
{
  //
  //  Print module LLVM code in human-readable form.
  //
  llvm::PassManager PM;
  PM.add(llvm::createPrintModulePass(&llvm::outs()));
  PM.run(*m);
}

void
ExecutionContext::installLazyFunctionCreator(LazyFunctionCreatorFunc_t fp)
{
  m_lazyFuncCreator.push_back(fp);
}

bool ExecutionContext::addSymbol(const char* symbolName,  void* symbolAddress){

  void* actualAdress
    = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(symbolName);
  if (actualAdress)
    return false;

  llvm::sys::DynamicLibrary::AddSymbol(symbolName, symbolAddress);
  return true;
}
