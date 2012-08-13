// @(#)root/meta:$Id$
// vim: sw=3 ts=3 expandtab foldmethod=indent

/*************************************************************************
 * Copyright (C) 1995-2012, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// This class defines an interface to the CINT C/C++ interpreter made   //
// by Masaharu Goto from HP Japan, using cling as interpreter backend.  //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TCintWithCling.h"

#include "TClingBaseClassInfo.h"
#include "TClingCallFunc.h"
#include "TClingClassInfo.h"
#include "TClingDataMemberInfo.h"
#include "TClingMethodArgInfo.h"
#include "TClingMethodInfo.h"
#include "TClingTypedefInfo.h"
#include "TClingTypeInfo.h"

#include "TROOT.h"
#include "TApplication.h"
#include "TGlobal.h"
#include "TDataType.h"
#include "TClass.h"
#include "TClassEdit.h"
#include "TClassTable.h"
#include "TBaseClass.h"
#include "TDataMember.h"
#include "TMemberInspector.h"
#include "TMethod.h"
#include "TMethodArg.h"
#include "TObjArray.h"
#include "TObjString.h"
#include "TString.h"
#include "THashList.h"
#include "TOrdCollection.h"
#include "TVirtualPad.h"
#include "TSystem.h"
#include "TVirtualMutex.h"
#include "TError.h"
#include "TEnv.h"
#include "THashTable.h"
#include "RConfigure.h"
#include "compiledata.h"
#include "TMetaUtils.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/HeaderSearchOptions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <cxxabi.h>
#include <limits.h>

#ifdef __APPLE__
#include <dlfcn.h>
#endif // __APPLE__

#define R__CINTWITHCLING_MODULES

using namespace std;

R__EXTERN int optind;

//______________________________________________________________________________
namespace {
   // A module and its headers. Intentionally not a copy:
   // If these strings end up in this struct they are
   // long lived by definition because they get passed in
   // before initialization of TCintWithCling.
   struct ModuleHeaderInfo_t {
      ModuleHeaderInfo_t(const char* moduleName, const char** headers):
         fModuleName(moduleName), fHeaders(headers) {}
      const char* fModuleName; // module name
      const char** fHeaders; // 0-terminated array of header files
   };

   llvm::SmallVector<ModuleHeaderInfo_t, 10> gModuleHeaderInfoBuffer;
}

//______________________________________________________________________________
extern "C"
void TCintWithCling__RegisterModule(const char* modulename,
                                    const char** headers)
{
   // Called by static dictionary initialization to register clang modules
   // for headers. Calls TCintWithCling::RegisterModule() unless gCling
   // is NULL, i.e. during startup, where the information is buffered in
   // the global gModuleHeaderInfoBuffer.

   if (gCint) {
      ((TCintWithCling*)gCint)->RegisterModule(modulename, headers);
   } else {
      gModuleHeaderInfoBuffer.push_back(ModuleHeaderInfo_t(modulename, headers));
   }
}



//______________________________________________________________________________
//
//
//

void* autoloadCallback(const std::string& mangled_name)
{
   // Autoload a library. Given a mangled function name find the
   // library which provides the function and load it.
   //--
   //
   //  Use the C++ ABI provided function to demangle the function name.
   //
   int err = 0;
   char* demangled_name = abi::__cxa_demangle(mangled_name.c_str(), 0, 0, &err);
   if (err) {
      return 0;
   }
   //fprintf(stderr, "demangled name: '%s'\n", demangled_name);
   //
   //  Separate out the class or namespace part of the
   //  function name.
   //
   std::string name(demangled_name);
   // Remove the function arguments.
   std::string::size_type pos = name.rfind('(');
   if (pos != std::string::npos) {
      name.erase(pos);
   }
   // Remove the function name.
   pos = name.rfind(':');
   if (pos != std::string::npos) {
      if ((pos != 0) && (name[pos-1] == ':')) {
         name.erase(pos-1);
      }
   }
   //fprintf(stderr, "name: '%s'\n", name.c_str());
   // Now we have the class or namespace name, so do the lookup.
   TString libs = gCint->GetClassSharedLibs(name.c_str());
   if (libs.IsNull()) {
      // Not found in the map, all done.
      return 0;
   }
   //fprintf(stderr, "library: %s\n", iter->second.c_str());
   // Now we have the name of the libraries to load, so load them.
   
   TString lib;
   Ssiz_t posLib = 0;
   while (libs.Tokenize(lib, posLib)) {
      std::string errmsg;
      bool load_failed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(lib, &errmsg);
      if (load_failed) {
         // The library load failed, all done.
         //fprintf(stderr, "load failed: %s\n", errmsg.c_str());
         return 0;
      }
   }
   //fprintf(stderr, "load succeeded.\n");
   // Get the address of the function being called.
   void* addr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(mangled_name.c_str());
   //fprintf(stderr, "addr: %016lx\n", reinterpret_cast<unsigned long>(addr));
   return addr;
}



//______________________________________________________________________________
//
//
//

extern "C" int ScriptCompiler(const char* filename, const char* opt)
{
   return gSystem->CompileMacro(filename, opt);
}

extern "C" int IgnoreInclude(const char* fname, const char* expandedfname)
{
   return gROOT->IgnoreInclude(fname, expandedfname);
}

extern "C" void TCint_UpdateClassInfo(char* c, Long_t l)
{
   TCintWithCling::UpdateClassInfo(c, l);
}

extern "C" int TCint_AutoLoadCallback(char* c, char* l)
{
   ULong_t varp = G__getgvp();
   G__setgvp((Long_t)G__PVOID);
   string cls(c);
   int result =  TCintWithCling::AutoLoadCallback(cls.c_str(), l);
   G__setgvp(varp);
   return result;
}

extern "C" void* TCint_FindSpecialObject(char* c, G__ClassInfo* ci, void** p1, void** p2)
{
   return TCintWithCling::FindSpecialObject(c, ci, p1, p2);
}

//______________________________________________________________________________
//
//
//

#if 0
//______________________________________________________________________________
static void collect_comment(Preprocessor& PP, ExpectedData& ED)
{
   // Create a raw lexer to pull all the comments out of the main file.
   // We don't want to look in #include'd headers for expected-error strings.
   SourceManager& SM = PP.getSourceManager();
   FileID FID = SM.getMainFileID();
   if (SM.getMainFileID().isInvalid()) {
      return;
   }
   // Create a lexer to lex all the tokens of the main file in raw mode.
   const llvm::MemoryBuffer* FromFile = SM.getBuffer(FID);
   Lexer RawLex(FID, FromFile, SM, PP.getLangOptions());
   // Return comments as tokens, this is how we find expected diagnostics.
   RawLex.SetCommentRetentionState(true);
   Token Tok;
   Tok.setKind(tok::comment);
   while (Tok.isNot(tok::eof)) {
      RawLex.Lex(Tok);
      if (!Tok.is(tok::comment)) {
         continue;
      }
      std::string Comment = PP.getSpelling(Tok);
      if (Comment.empty()) {
         continue;
      }
      // Find all expected errors/warnings/notes.
      ParseDirective(&Comment[0], Comment.size(), ED, PP, Tok.getLocation());
   };
}
#endif // 0

//______________________________________________________________________________
//
//
//

int TCint_GenerateDictionary(const std::vector<std::string> &classes,
                             const std::vector<std::string> &headers,
                             const std::vector<std::string> &fwdDecls,
                             const std::vector<std::string> &unknown)
{
   //This function automatically creates the "LinkDef.h" file for templated
   //classes then executes CompileMacro on it.
   //The name of the file depends on the class name, and it's not generated again
   //if the file exist.
   if (classes.empty()) {
      return 0;
   }
   // Use the name of the first class as the main name.
   const std::string& className = classes[0];
   //(0) prepare file name
   TString fileName = "AutoDict_";
   std::string::const_iterator sIt;
   for (sIt = className.begin(); sIt != className.end(); sIt++) {
      if (*sIt == '<' || *sIt == '>' ||
            *sIt == ' ' || *sIt == '*' ||
            *sIt == ',' || *sIt == '&' ||
            *sIt == ':') {
         fileName += '_';
      }
      else {
         fileName += *sIt;
      }
   }
   if (classes.size() > 1) {
      Int_t chk = 0;
      std::vector<std::string>::const_iterator it = classes.begin();
      while ((++it) != classes.end()) {
         for (UInt_t cursor = 0; cursor != it->length(); ++cursor) {
            chk = chk * 3 + it->at(cursor);
         }
      }
      fileName += TString::Format("_%u", chk);
   }
   fileName += ".cxx";
   if (gSystem->AccessPathName(fileName) != 0) {
      //file does not exist
      //(1) prepare file data
      // If STL, also request iterators' operators.
      // vector is special: we need to check whether
      // vector::iterator is a typedef to pointer or a
      // class.
      static std::set<std::string> sSTLTypes;
      if (sSTLTypes.empty()) {
         sSTLTypes.insert("vector");
         sSTLTypes.insert("list");
         sSTLTypes.insert("deque");
         sSTLTypes.insert("map");
         sSTLTypes.insert("multimap");
         sSTLTypes.insert("set");
         sSTLTypes.insert("multiset");
         sSTLTypes.insert("queue");
         sSTLTypes.insert("priority_queue");
         sSTLTypes.insert("stack");
         sSTLTypes.insert("iterator");
      }
      std::vector<std::string>::const_iterator it;
      std::string fileContent("");
      for (it = headers.begin(); it != headers.end(); ++it) {
         fileContent += "#include \"" + *it + "\"\n";
      }
      for (it = unknown.begin(); it != unknown.end(); ++it) {
         TClass* cl = TClass::GetClass(it->c_str());
         if (cl && cl->GetDeclFileName()) {
            TString header(gSystem->BaseName(cl->GetDeclFileName()));
            TString dir(gSystem->DirName(cl->GetDeclFileName()));
            TString dirbase(gSystem->BaseName(dir));
            while (dirbase.Length() && dirbase != "."
                   && dirbase != "include" && dirbase != "inc"
                   && dirbase != "prec_stl") {
               gSystem->PrependPathName(dirbase, header);
               dir = gSystem->DirName(dir);
            }
            fileContent += TString("#include \"") + header + "\"\n";
         }
      }
      for (it = fwdDecls.begin(); it != fwdDecls.end(); ++it) {
         fileContent += "class " + *it + ";\n";
      }
      fileContent += "#ifdef __CINT__ \n";
      fileContent += "#pragma link C++ nestedclasses;\n";
      fileContent += "#pragma link C++ nestedtypedefs;\n";
      for (it = classes.begin(); it != classes.end(); ++it) {
         std::string n(*it);
         size_t posTemplate = n.find('<');
         std::set<std::string>::const_iterator iSTLType = sSTLTypes.end();
         if (posTemplate != std::string::npos) {
            n.erase(posTemplate, std::string::npos);
            if (n.compare(0, 5, "std::") == 0) {
               n.erase(0, 5);
            }
            iSTLType = sSTLTypes.find(n);
         }
         fileContent += "#pragma link C++ class ";
         fileContent +=    *it + "+;\n" ;
         fileContent += "#pragma link C++ class ";
         if (iSTLType != sSTLTypes.end()) {
            // STL class; we cannot (and don't need to) store iterators;
            // their shadow and the compiler's version don't agree. So
            // don't ask for the '+'
            fileContent +=    *it + "::*;\n" ;
         }
         else {
            // Not an STL class; we need to allow the I/O of contained
            // classes (now that we have a dictionary for them).
            fileContent +=    *it + "::*+;\n" ;
         }
         std::string oprLink("#pragma link C++ operators ");
         oprLink += *it;
         // Don't! Requests e.g. op<(const vector<T>&, const vector<T>&):
         // fileContent += oprLink + ";\n";
         if (iSTLType != sSTLTypes.end()) {
            if (n == "vector") {
               fileContent += "#ifdef G__VECTOR_HAS_CLASS_ITERATOR\n";
            }
            fileContent += oprLink + "::iterator;\n";
            fileContent += oprLink + "::const_iterator;\n";
            fileContent += oprLink + "::reverse_iterator;\n";
            if (n == "vector") {
               fileContent += "#endif\n";
            }
         }
      }
      fileContent += "#endif\n";
      //end(1)
      //(2) prepare the file
      FILE* filePointer;
      filePointer = fopen(fileName, "w");
      if (filePointer == NULL) {
         //can't open a file
         return 1;
      }
      //end(2)
      //write data into the file
      fprintf(filePointer, "%s", fileContent.c_str());
      fclose(filePointer);
   }
   //(3) checking if we can compile a macro, if not then cleaning
   Int_t oldErrorIgnoreLevel = gErrorIgnoreLevel;
   gErrorIgnoreLevel = kWarning; // no "Info: creating library..."
   Int_t ret = gSystem->CompileMacro(fileName, "k");
   gErrorIgnoreLevel = oldErrorIgnoreLevel;
   if (ret == 0) { //can't compile a macro
      return 2;
   }
   //end(3)
   return 0;
}

int TCint_GenerateDictionary(const std::string& className,
                             const std::vector<std::string> &headers,
                             const std::vector<std::string> &fwdDecls,
                             const std::vector<std::string> &unknown)
{
   //This function automatically creates the "LinkDef.h" file for templated
   //classes then executes CompileMacro on it.
   //The name of the file depends on the class name, and it's not generated again
   //if the file exist.
   std::vector<std::string> classes;
   classes.push_back(className);
   return TCint_GenerateDictionary(classes, headers, fwdDecls, unknown);
}

//______________________________________________________________________________
//
//
//

// It is a "fantom" method to synchronize user keyboard input
// and ROOT prompt line (for WIN32)
const char* fantomline = "TRint::EndOfLineAction();";

//______________________________________________________________________________
//
//
//

void* TCintWithCling::fgSetOfSpecials = 0;

//______________________________________________________________________________
//
//
//

ClassImp(TCintWithCling)

//______________________________________________________________________________
TCintWithCling::TCintWithCling(const char *name, const char *title)
   : TInterpreter(name, title)
   , fSharedLibs("")
   , fSharedLibsSerial(-1)
   , fGlobalsListSerial(-1)
   , fInterpreter(0)
   , fMetaProcessor(0)
{
   // Initialize the CINT+cling interpreter interface.

   TString interpInclude;
#ifndef ROOTINCDIR
   TString rootsys = gSystem->Getenv("ROOTSYS");
   interpInclude = rootsys + "/etc";
#else // ROOTINCDIR
   interpInclude = ROOTETCDIR;
#endif // ROOTINCDIR
   interpInclude.Prepend("-I");
   const char* interpArgs[] = {"cling4root", interpInclude.Data(), "-Xclang", "-fmodules"};

   TString llvmDir;
   if (gSystem->Getenv("$(LLVMDIR)")) {
      llvmDir = gSystem->ExpandPathName("$(LLVMDIR)");
   }
#ifdef R__LLVMDIR
   if (llvmDir.IsNull()) {
      llvmDir = R__LLVMDIR;
   }
#endif // R__LLVMDIR

   fInterpreter = new cling::Interpreter(sizeof(interpArgs) / sizeof(char*),
                                         interpArgs, llvmDir); 
   fInterpreter->installLazyFunctionCreator(autoloadCallback);

   // Add the current path to the include path
   TCintWithCling::AddIncludePath(".");

   // Add the root include directory and etc/ to list searched by default.
   // Use explicit TCintWithCling::AddIncludePath() to avoid vtable: we're in the c'tor!
#ifndef ROOTINCDIR
   TCintWithCling::AddIncludePath(rootsys + "/include");
   TString dictDir = rootsys + "/lib";
#else // ROOTINCDIR
   TCintWithCling::AddIncludePath(ROOTINCDIR);
   TString dictDir = ROOTLIBDIR;
#endif // ROOTINCDIR

   clang::HeaderSearch& HS = fInterpreter->getCI()->getPreprocessor().getHeaderSearchInfo();
   HS.setModuleCachePath(dictDir.Data());

   for (size_t i = 0, e = gModuleHeaderInfoBuffer.size(); i < e ; ++i) {
      // process buffered module registrations
      ((TCintWithCling*)gCint)->RegisterModule(gModuleHeaderInfoBuffer[i].fModuleName,
                                               gModuleHeaderInfoBuffer[i].fHeaders);
   }
   gModuleHeaderInfoBuffer.clear();

   fMetaProcessor = new cling::MetaProcessor(*fInterpreter);

   fInterpreter->declare("namespace std {} using namespace std;");
   // to pull in gPluginManager
#ifndef R__CINTWITHCLING_MODULES
   fInterpreter->parse("#include \"TPluginManager.h\"");
   fInterpreter->parse("#include \"TGenericClassInfo.h\"");
   fInterpreter->parse("#include \"Rtypes.h\"");
#else
   // Already done through modules
#endif // R__CINTWITHCLING_MODULES

   // Initialize the CINT interpreter interface.
   fMore      = 0;
   fPrompt[0] = 0;
   fMapfile   = 0;
   fRootmapFiles = 0;
   fLockProcessLine = kTRUE;
   // Disable the autoloader until it is explicitly enabled.
   G__set_class_autoloading(0);
   G__RegisterScriptCompiler(&ScriptCompiler);
   G__set_ignoreinclude(&IgnoreInclude);
   G__InitUpdateClassInfo(&TCint_UpdateClassInfo);
   G__InitGetSpecialObject(&TCint_FindSpecialObject);
   // check whether the compiler is available:
   char* path = gSystem->Which(gSystem->Getenv("PATH"), gSystem->BaseName(COMPILER));
   if (path && path[0]) {
      G__InitGenerateDictionary(&TCint_GenerateDictionary);
   }
   delete[] path;
   ResetAll();
#ifndef R__WIN32
   optind = 1;  // make sure getopt() works in the main program
#endif // R__WIN32
   // Make sure that ALL macros are seen as C++.
   G__LockCpp();
   // Initialize for ROOT:
   // Disallow the interpretation of Rtypes.h, TError.h and TGenericClassInfo.h
   ProcessLineCintOnly("#define ROOT_Rtypes 0");
   ProcessLineCintOnly("#define ROOT_TError 0");
   ProcessLineCintOnly("#define ROOT_TGenericClassInfo 0");
   TString include;
   // Add the root include directory to list searched by default
#ifndef ROOTINCDIR
   include = gSystem->Getenv("ROOTSYS");
   include.Append("/include");
#else // ROOTINCDIR
   include = ROOTINCDIR;
#endif // ROOTINCDIR
   TCintWithCling::AddIncludePath(include);
   // Allow the usage of ClassDef and ClassImp in interpreted macros
   // if RtypesCint.h can be found (think of static executable without include/)
   char* whichTypesCint = gSystem->Which(include, "RtypesCint.h");
   if (whichTypesCint) {
      ProcessLineCintOnly("#include <RtypesCint.h>");
      delete[] whichTypesCint;
   }
}

//______________________________________________________________________________
TCintWithCling::~TCintWithCling()
{
   // Destroy the CINT interpreter interface.
   if (fMore != -1) {
      // only close the opened files do not free memory:
      // G__scratch_all();
      G__close_inputfiles();
   }
   delete fMapfile;
   delete fRootmapFiles;
   gCint = 0;
#ifdef R__COMPLETE_MEM_TERMINATION
   G__scratch_all();
#endif // R__COMPLETE_MEM_TERMINATION
   //--
}

//______________________________________________________________________________
void TCintWithCling::RegisterModule(const char* modulename, const char** headers)
{
   // Inject the module named "modulename" into cling; load all headers.
   // headers is a 0-terminated array of header files to #include after
   // loading the module. The module is searched for in all $LD_LIBRARY_PATH
   // entries (or %PATH% on Windows).
   // This function gets called by the static initialization of dictionary
   // libraries.

   TString pcmFileName(modulename);
   pcmFileName += "_dict.pcm";

   // Assemble search path:
   
#ifdef R__WIN32
   TString searchPath = "$(PATH);";
#else
   TString searchPath = "$(LD_LIBRARY_PATH):";
#endif
#ifndef ROOTLIBDIR
   TString rootsys = gSystem->Getenv("ROOTSYS");
# ifdef R__WIN32
   searchPath += rootsys + "/bin";
# else
   searchPath += rootsys + "/lib";
# endif
#else // ROOTLIBDIR
# ifdef R__WIN32
   searchPath += ROOTBINDIR;
# else
   searchPath += ROOTLIBDIR;
# endif
#endif // ROOTLIBDIR
   gSystem->ExpandPathName(searchPath);

   Bool_t haveModule = kTRUE;
   if (!gSystem->FindFile(searchPath, pcmFileName)) {
      Error("RegisterModule()", "Cannot find dictionary module %s_dict.pcm in %s",
            modulename, searchPath.Data());
      haveModule = kFALSE;
   }

   clang::CompilerInstance * CI = fInterpreter->getCI ();
   clang::Preprocessor& PP = CI->getPreprocessor();
   clang::ModuleMap& ModuleMap = PP.getHeaderSearchInfo().getModuleMap();

   std::pair<clang::Module*, bool> modCreation;
   if (haveModule) {
      TCintWithCling::Info("RegisterModule", "Loading PCM %s", pcmFileName.Data());
      TString modulename_dict = modulename;
      modulename_dict += "_dict";

      modCreation
         = ModuleMap.findOrCreateModule(modulename_dict.Data(), 0 /*ActiveModule*/,
                                        false /*Framework*/, false /*Explicit*/);
      if (!modCreation.second) {
         Error("RegisterModule()",
               "Duplicate deficition of dictionary module %s in %s.",
               /*"\nOriginal module was found in %s.", - if only we could...*/
               pcmFileName.Data(), searchPath.Data());
         // Go on, add new headers nonetheless.
      }
   }

   clang::HeaderSearch& HdrSearch = PP.getHeaderSearchInfo();
   for (const char** hdr = headers; *hdr; ++hdr) {
      if (haveModule) {
         const clang::DirectoryLookup* CurDir;
         const clang::FileEntry* hdrFileEntry
            =  HdrSearch.LookupFile(*hdr, false /*isAngled*/, 0 /*FromDir*/,
                                    CurDir, 0 /*CurFileEnt*/, 0 /*SearchPath*/,
                                    0 /*RelativePath*/, 0 /*SuggestedModule*/);
         if (!hdrFileEntry) {
            Warning("RegisterModule()",
                    "Cannot find header file %s included in dictionary module %s"
                    " in include search path!",
                    *hdr, modulename);
            hdrFileEntry = PP.getFileManager().getFile(*hdr, /*OpenFile=*/false,
                                                       /*CacheFailure=*/false);
         } else {
            // Tell HeaderSearch that the header's directory has a module.map
            llvm::StringRef srHdrDir(hdrFileEntry->getName());
            srHdrDir = llvm::sys::path::parent_path(srHdrDir);
            const clang::DirectoryEntry* Dir
               = PP.getFileManager().getDirectory(srHdrDir);
            if (Dir) {
#ifdef R__CINTWITHCLING_MODULES
               HdrSearch.setDirectoryHasModuleMap(Dir);
#endif
            }
         }

#ifdef R__CINTWITHCLING_MODULES
         ModuleMap.addHeader(modCreation.first, hdrFileEntry);
#endif
      }
      Info("RegisterModule()", "   #including %s...", *hdr);
      fInterpreter->parse(TString::Format("#include \"%s\"", *hdr).Data());
   }   
}

//______________________________________________________________________________
Long_t TCintWithCling::ProcessLineCintOnly(const char* line, EErrorCode* error /*=0*/)
{
   // Let CINT process a command line.
   // If the command is executed and the result of G__process_cmd is 0,
   // the return value is the int value corresponding to the result of the command
   // (float and double return values will be truncated).

   Long_t ret = 0;
   if (gApplication) {
      if (gApplication->IsCmdThread()) {
         if (gGlobalMutex && !gCINTMutex && fLockProcessLine) {
            gGlobalMutex->Lock();
            if (!gCINTMutex)
               gCINTMutex = gGlobalMutex->Factory(kTRUE);
            gGlobalMutex->UnLock();
         }
         R__LOCKGUARD(fLockProcessLine ? gCINTMutex : 0);
         gROOT->SetLineIsProcessing();

         G__value local_res;
         G__setnull(&local_res);

         // It checks whether the input line contains the "fantom" method
         // to synchronize user keyboard input and ROOT prompt line
         if (strstr(line,fantomline)) {
            G__free_tempobject();
            TCintWithCling::UpdateAllCanvases();
         } else {
            int local_error = 0;

            int prerun = G__getPrerun();
            G__setPrerun(0);
            ret = G__process_cmd(const_cast<char*>(line), fPrompt, &fMore, &local_error, &local_res);
            G__setPrerun(prerun);
            if (local_error == 0 && G__get_return(&fExitCode) == G__RETURN_EXIT2) {
               ResetGlobals();
               gApplication->Terminate(fExitCode);
            }
            if (error)
               *error = (EErrorCode)local_error;
         }

         if (ret == 0) {
            // prevent overflow signal
            double resd = G__double(local_res);
            if (resd > LONG_MAX) ret = LONG_MAX;
            else if (resd < LONG_MIN) ret = LONG_MIN;
            else ret = G__int_cast(local_res);
         }

         gROOT->SetLineHasBeenProcessed();
      } else {
         ret = ProcessLineCintOnly(line, error);
      }
   } else {
      if (gGlobalMutex && !gCINTMutex && fLockProcessLine) {
         gGlobalMutex->Lock();
         if (!gCINTMutex)
            gCINTMutex = gGlobalMutex->Factory(kTRUE);
         gGlobalMutex->UnLock();
      }
      R__LOCKGUARD(fLockProcessLine ? gCINTMutex : 0);
      gROOT->SetLineIsProcessing();

      G__value local_res;
      G__setnull(&local_res);

      int local_error = 0;

      int prerun = G__getPrerun();
      G__setPrerun(0);
      ret = G__process_cmd(const_cast<char*>(line), fPrompt, &fMore, &local_error, &local_res);
      G__setPrerun(prerun);
      if (local_error == 0 && G__get_return(&fExitCode) == G__RETURN_EXIT2) {
         ResetGlobals();
         exit(fExitCode);
      }
      if (error)
         *error = (EErrorCode)local_error;

      if (ret == 0) {
         // prevent overflow signal
         double resd = G__double(local_res);
         if (resd > LONG_MAX) ret = LONG_MAX;
         else if (resd < LONG_MIN) ret = LONG_MIN;
         else ret = G__int_cast(local_res);
      }

      gROOT->SetLineHasBeenProcessed();
   }
   return ret;
}

//______________________________________________________________________________
Long_t TCintWithCling::ProcessLine(const char* line, EErrorCode* error/*=0*/)
{
   // Let CINT process a command line.
   //
   // If the command is executed and the result is 0, then the return value
   // is the int value corresponding to the result of the executed command
   // (float and double return values will be truncated).
   //
   //
   // Copy the passed line, it comes from a static buffer in TApplication
   // and we can be reentered through the CINT routine G__process_cmd,
   // which would overwrite the static buffer and we would forget what we
   // were doing.
   //
   TString sLine(line);
   if (strstr(line,fantomline)) {
      // End-Of-Line action, CINT-only.
      return TCintWithCling::ProcessLineCintOnly(sLine, error);
   }
   Long_t ret = 0L;
   (void)ret;
   if (!strncmp(sLine.Data(), ".L", 2) || !strncmp(sLine.Data(), ".x", 2) ||
       !strncmp(sLine.Data(), ".X", 2)) {
      TString mod_line(sLine);
      if ((mod_line[1] == 'x') || (mod_line[1] == 'X')) {
         // Let CINT load the file, but have only cling execute it.
         mod_line[1] = 'L';
      }
      Ssiz_t paren_pos = mod_line.Last('(');
      if ((paren_pos != kNPOS) && mod_line.EndsWith(")")) {
         mod_line.Remove(paren_pos, mod_line.Length() - paren_pos);
      }
      ret = ProcessLineCintOnly(mod_line, error);
   }
   // A non-zero returned value means the given line was
   // not a complete statement.
   int indent = 0;
   // This will hold the resulting value of the evaluation the given line.
   cling::Value result;
   if (!strncmp(sLine.Data(), ".L", 2) || !strncmp(sLine.Data(), ".x", 2) ||
       !strncmp(sLine.Data(), ".X", 2)) {
      // If there was a trailing "+", then CINT compiled the code above,
      // and we will need to strip the "+" before passing the line to cling.
      TString mod_line(sLine);
      TString aclicMode;
      TString arguments;
      TString io;
      TString fname = gSystem->SplitAclicMode(sLine.Data() + 3,
         aclicMode, arguments, io);
      if (aclicMode.Length()) {
         // There was a "+" at the end, strip it now.
         mod_line = mod_line(0, 2) + " " + fname;
      }
      indent = fMetaProcessor->process(mod_line, &result);
   }
   else {
      indent = fMetaProcessor->process(sLine, &result);
   }
   if ((sLine[0] == '#') || (sLine[0] == '.')) {
      // Let CINT see preprocessor and meta commands, but only
      // after cling has seen them first, otherwise any dictionary
      // loading triggered by CINT will cause TClass constructors
      // to not be able to load clang declarations.
      Char_t c = sLine[1];
      if ((c != 'I') && (c != 'L') && (c != 'x') && (c != 'X')) {
         // But not .I which is cling-only, and the .L, .x,
         // and .X commands were handled above.
         ret = ProcessLineCintOnly(sLine, error);
      }
   }
   if (indent) {
      // incomplete expression, needs something like:
      /// fMetaProcessor->abortEvaluation();
      return 0;
   }
   Bool_t resultHasValue = result.hasValue(
      fInterpreter->getCI()->getASTContext());
   if (resultHasValue) {
      return result.simplisticCastAs<long>();
   }
   return 0;
}

//______________________________________________________________________________
void TCintWithCling::PrintIntro()
{
   // Print CINT introduction and help message.

   Printf("\nCINT/ROOT C/C++ Interpreter version %s", G__cint_version());
   Printf(">>>>>>>>> cling-ified version <<<<<<<<<<");
   Printf("Type ? for help. Commands must be C++ statements.");
   Printf("Enclose multiple statements between { }.");
}

//______________________________________________________________________________
void TCintWithCling::AddIncludePath(const char *path)
{
   // Add the given path to the list of directories in which the interpreter
   // looks for include files. Only one path item can be specified at a
   // time, i.e. "path1:path2" is not supported.

   fInterpreter->AddIncludePath(path);
   //TCintWithCling::AddIncludePath(path);
   R__LOCKGUARD(gCINTMutex);
   char* incpath = gSystem->ExpandPathName(path);
   G__add_ipath(incpath);
   delete[] incpath;
}

//______________________________________________________________________________
void TCintWithCling::InspectMembers(TMemberInspector& insp, void* obj,
                                    TClass* cl)
{
   // Visit all members over members, recursing over base classes.
   
   char* cobj = (char*) obj; // for ptr arithmetics

   static clang::PrintingPolicy
      printPol(fInterpreter->getCI()->getLangOpts());
   if (printPol.Indentation) {
      // not yet inialized
      printPol.Indentation = 0;
      printPol.SuppressInitializers = true;
   }
   
   const char* clname = cl->GetName();
   Printf("Inspecting class %s\n", clname);

   const clang::ASTContext& astContext = fInterpreter->getCI()->getASTContext();
   const clang::CXXRecordDecl* recordDecl 
     = llvm::dyn_cast<const clang::CXXRecordDecl>(fInterpreter->lookupScope(clname));
   if (!recordDecl) {
      Error("InspectMembers", "Cannot find RecordDecl for class %s", clname);
      return;
   }
   
   const clang::ASTRecordLayout& recLayout
      = astContext.getASTRecordLayout(recordDecl);

   unsigned iNField = 0;
   // iterate over fields
   // FieldDecls are non-static, else it would be a VarDecl.
   for (clang::RecordDecl::field_iterator iField = recordDecl->field_begin(),
        eField = recordDecl->field_end(); iField != eField;
        ++iField, ++iNField) {

      clang::QualType memberQT = iField->getType().getDesugaredType(astContext);
      if (memberQT.isNull()) {
         std::string memberName;
         iField->getNameForDiagnostic(memberName, printPol, true /*fqi*/);
         Error("InspectMembers",
               "Cannot retrieve QualType for member %s while inspecting class %s",
               memberName.c_str(), clname);
         continue; // skip member
      }
      const clang::Type* memType = memberQT.getTypePtr();
      if (!memType) {
         std::string memberName;
         iField->getNameForDiagnostic(memberName, printPol, true /*fqi*/);
         Error("InspectMembers",
               "Cannot retrieve Type for member %s while inspecting class %s",
               memberName.c_str(), clname);
         continue; // skip member
      }
      
      const clang::Type* memNonPtrType = memType;
      if (memNonPtrType->isPointerType()) {
         clang::QualType ptrQT
            = memNonPtrType->getAs<clang::PointerType>()->getPointeeType();
         ptrQT = ptrQT.getDesugaredType(astContext);
         if (ptrQT.isNull()) {
            std::string memberName;
            iField->getNameForDiagnostic(memberName, printPol, true /*fqi*/);
            Error("InspectMembers",
                  "Cannot retrieve pointee Type for member %s while inspecting class %s",
                  memberName.c_str(), clname);
            continue; // skip member
         }
         memNonPtrType = ptrQT.getTypePtr();
      }

      // assemble array size(s): "[12][4][]"
      llvm::SmallString<8> arraySize;
      const clang::ArrayType* arrType = memNonPtrType->getAsArrayTypeUnsafe();
      unsigned arrLevel = 0;
      bool haveErrorDueToArray = false;
      while (arrType) {
         ++arrLevel;
         arraySize += '[';
         const clang::ConstantArrayType* constArrType =
         clang::dyn_cast<clang::ConstantArrayType>(arrType);
         if (constArrType) {
            constArrType->getSize().toStringUnsigned(arraySize);
         }
         arraySize += ']';
         clang::QualType subArrQT = arrType->getElementType();
         if (subArrQT.isNull()) {
            std::string memberName;
            iField->getNameForDiagnostic(memberName, printPol, true /*fqi*/);
            Error("InspectMembers",
                  "Cannot retrieve QualType for array level %d (i.e. element type of %s) for member %s while inspecting class %s",
                  arrLevel, subArrQT.getAsString(printPol).c_str(),
                  memberName.c_str(), clname);
            haveErrorDueToArray = true;
            break;
         }
         arrType = subArrQT.getTypePtr()->getAsArrayTypeUnsafe();
      }
      if (haveErrorDueToArray) {
         continue; // skip member
      }

      // construct member name
      std::string fieldName;
      if (memType->isPointerType()) {
         fieldName = "*";
      }
      fieldName += iField->getName();
      fieldName += arraySize;
      
      // get member offset
      ptrdiff_t fieldOffset = recLayout.getFieldOffset(iNField);

      // R__insp.Inspect(R__cl, R__insp.GetParent(), "fBits[2]", fBits);
      // R__insp.Inspect(R__cl, R__insp.GetParent(), "fName", &fName);
      // R__insp.InspectMember(fName, "fName.");
      // R__insp.Inspect(R__cl, R__insp.GetParent(), "*fClass", &fClass);
      insp.Inspect(cl, insp.GetParent(), fieldName.c_str(), cobj + fieldOffset);

      const clang::CXXRecordDecl* fieldRecDecl = memNonPtrType->getAsCXXRecordDecl();
      if (fieldRecDecl) {
         // nested objects get an extra call to InspectMember
         // R__insp.InspectMember("FileStat_t", (void*)&fFileStat, "fFileStat.", false);
         std::string sFieldRecName;
         fieldRecDecl->getNameForDiagnostic(sFieldRecName,
                                            printPol, true /*fqi*/);
         bool transient = false;
         insp.InspectMember(sFieldRecName.c_str(), cobj + fieldOffset,
                            (fieldName + '.').c_str(), transient);
      }
   } // loop over fields
   
   // inspect bases
   // TNamed::ShowMembers(R__insp);
   unsigned iNBase = 0;
   for (clang::CXXRecordDecl::base_class_const_iterator iBase
        = recordDecl->bases_begin(), eBase = recordDecl->bases_end();
        iBase != eBase; ++iBase, ++iNBase) {
      clang::QualType baseQT = iBase->getType();
      if (baseQT.isNull()) {
         Error("InspectMembers",
               "Cannot find QualType for base number %d while inspecting class %s",
               iNBase, clname);
         continue;
      }
      const clang::CXXRecordDecl* baseDecl
         = baseQT->getAsCXXRecordDecl();
      if (!baseDecl) {
         Error("InspectMembers",
               "Cannot find CXXRecordDecl for base number %d while inspecting class %s",
               iNBase, clname);
         continue;
      }
      std::string sBaseName;
      baseDecl->getNameForDiagnostic(sBaseName, printPol, true /*fqi*/);
      TClass* baseCl = TClass::GetClass(sBaseName.c_str());
      if (!baseCl) {
         Error("InspectMembers",
               "Cannot find TClass for base %s while inspecting class %s",
               sBaseName.c_str(), clname);
         continue;
      }
      int64_t baseOffset = recLayout.getBaseClassOffset(baseDecl).getQuantity();      
      baseCl->CallShowMembers(cobj + baseOffset,
                              insp, -1 /*don't know whether TObject*/);
   } // loop over bases
}

//______________________________________________________________________________
void TCintWithCling::ClearFileBusy()
{
   // Reset CINT internal state in case a previous action was not correctly
   // terminated by G__init_cint() and G__dlmod().
   R__LOCKGUARD(gCINTMutex);
   G__clearfilebusy(0);
}

//______________________________________________________________________________
void TCintWithCling::ClearStack()
{
   // Delete existing temporary values
   R__LOCKGUARD(gCINTMutex);
   G__clearstack();
}

//______________________________________________________________________________
Int_t TCintWithCling::InitializeDictionaries()
{
   // Initialize all registered dictionaries. Normally this is already done
   // by G__init_cint() and G__dlmod().
   R__LOCKGUARD(gCINTMutex);
   return G__call_setup_funcs();
}

//______________________________________________________________________________
void TCintWithCling::EnableAutoLoading()
{
   // Enable the automatic loading of shared libraries when a class
   // is used that is stored in a not yet loaded library. Uses the
   // information stored in the class/library map (typically
   // $ROOTSYS/etc/system.rootmap).
   R__LOCKGUARD(gCINTMutex);
   G__set_class_autoloading_callback(&TCint_AutoLoadCallback);
   G__set_class_autoloading(1);
   LoadLibraryMap();
}

//______________________________________________________________________________
void TCintWithCling::EndOfLineAction()
{
   // It calls a "fantom" method to synchronize user keyboard input
   // and ROOT prompt line.
   ProcessLineSynch(fantomline);
}

//______________________________________________________________________________
Bool_t TCintWithCling::IsLoaded(const char* filename) const
{
   // Return true if the file has already been loaded by cint.
   // We will try in this order:
   //   actual filename
   //   filename as a path relative to
   //            the include path
   //            the shared library path
   R__LOCKGUARD(gCINTMutex);
   G__SourceFileInfo file(filename);
   if (file.IsValid()) {
      return kTRUE;
   };
   char* next = gSystem->Which(TROOT::GetMacroPath(), filename, kReadPermission);
   if (next) {
      file.Init(next);
      delete[] next;
      if (file.IsValid()) {
         return kTRUE;
      };
   }
   TString incPath = gSystem->GetIncludePath(); // of the form -Idir1  -Idir2 -Idir3
   incPath.Append(":").Prepend(" ");
   incPath.ReplaceAll(" -I", ":");      // of form :dir1 :dir2:dir3
   while (incPath.Index(" :") != -1) {
      incPath.ReplaceAll(" :", ":");
   }
   incPath.Prepend(".:");
# ifdef CINTINCDIR
   TString cintdir = CINTINCDIR;
# else
   TString cintdir = "$(ROOTSYS)/cint";
# endif
   incPath.Append(":");
   incPath.Append(cintdir);
   incPath.Append("/include:");
   incPath.Append(cintdir);
   incPath.Append("/stl");
   next = gSystem->Which(incPath, filename, kReadPermission);
   if (next) {
      file.Init(next);
      delete[] next;
      if (file.IsValid()) {
         return kTRUE;
      };
   }
   next = gSystem->DynamicPathName(filename, kTRUE);
   if (next) {
      file.Init(next);
      delete[] next;
      if (file.IsValid()) {
         return kTRUE;
      };
   }
   return kFALSE;
}

//______________________________________________________________________________
Int_t TCintWithCling::Load(const char* filename, Bool_t system)
{
   // Load a library file in CINT's memory.
   // if 'system' is true, the library is never unloaded.
   R__LOCKGUARD2(gCINTMutex);
   int i;
   if (!system) {
      i = G__loadfile(filename);
   }
   else {
      i = G__loadsystemfile(filename);
   }
   UpdateListOfTypes();
   return i;
}

//______________________________________________________________________________
void TCintWithCling::LoadMacro(const char* filename, EErrorCode* error)
{
   // Load a macro file in CINT's memory.
   ProcessLine(Form(".L %s", filename), error);
   UpdateListOfTypes();
   UpdateListOfGlobals();
   UpdateListOfGlobalFunctions();
}

//______________________________________________________________________________
Long_t TCintWithCling::ProcessLineAsynch(const char* line, EErrorCode* error)
{
   // Let CINT process a command line asynch.
   return ProcessLine(line, error);
}

//______________________________________________________________________________
Long_t TCintWithCling::ProcessLineSynch(const char* line, EErrorCode* error)
{
   // Let CINT process a command line synchronously, i.e we are waiting
   // it will be finished.
   R__LOCKGUARD(fLockProcessLine ? gCINTMutex : 0);
   if (gApplication) {
      if (gApplication->IsCmdThread()) {
         return ProcessLine(line, error);
      }
      return 0;
   }
   return ProcessLine(line, error);
}

//______________________________________________________________________________
Long_t TCintWithCling::Calc(const char* line, EErrorCode* error)
{
   // Directly execute an executable statement (e.g. "func()", "3+5", etc.
   // however not declarations, like "Int_t x;").
   Long_t result;
#ifdef R__WIN32
   // Test on ApplicationImp not being 0 is needed because only at end of
   // TApplication ctor the IsLineProcessing flag is set to 0, so before
   // we can not use it.
   if (gApplication && gApplication->GetApplicationImp()) {
      while (gROOT->IsLineProcessing() && !gApplication) {
         Warning("Calc", "waiting for CINT thread to free");
         gSystem->Sleep(500);
      }
      gROOT->SetLineIsProcessing();
   }
#endif
   R__LOCKGUARD2(gCINTMutex);
   result = (Long_t) G__int_cast(G__calc(const_cast<char*>(line)));
   if (error) {
      *error = (EErrorCode)G__lasterror();
   }
#ifdef R__WIN32
   if (gApplication && gApplication->GetApplicationImp()) {
      gROOT->SetLineHasBeenProcessed();
   }
#endif
   return result;
}

//______________________________________________________________________________
void TCintWithCling::SetGetline(const char * (*getlineFunc)(const char* prompt),
                       void (*histaddFunc)(const char* line))
{
   // Set a getline function to call when input is needed.
   G__SetGetlineFunc(getlineFunc, histaddFunc);
}

//______________________________________________________________________________
void TCintWithCling::RecursiveRemove(TObject* obj)
{
   // Delete object from CINT symbol table so it can not be used anymore.
   // CINT objects are always on the heap.
   R__LOCKGUARD(gCINTMutex);
   if (obj->IsOnHeap() && fgSetOfSpecials && !((std::set<TObject*>*)fgSetOfSpecials)->empty()) {
      std::set<TObject*>::iterator iSpecial = ((std::set<TObject*>*)fgSetOfSpecials)->find(obj);
      if (iSpecial != ((std::set<TObject*>*)fgSetOfSpecials)->end()) {
         DeleteGlobal(obj);
         ((std::set<TObject*>*)fgSetOfSpecials)->erase(iSpecial);
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::Reset()
{
   // Reset the CINT state to the state saved by the last call to
   // TCintWithCling::SaveContext().
   R__LOCKGUARD(gCINTMutex);
   G__scratch_upto(&fDictPos);
}

//______________________________________________________________________________
void TCintWithCling::ResetAll()
{
   // Reset the CINT state to its initial state.
   R__LOCKGUARD(gCINTMutex);
   G__init_cint("cint +V");
   G__init_process_cmd();
}

//______________________________________________________________________________
void TCintWithCling::ResetGlobals()
{
   // Reset the CINT global object state to the state saved by the last
   // call to TCintWithCling::SaveGlobalsContext().
   R__LOCKGUARD(gCINTMutex);
   G__scratch_globals_upto(&fDictPosGlobals);
}

//______________________________________________________________________________
void TCintWithCling::ResetGlobalVar(void* obj)
{
   // Reset the CINT global object state to the state saved by the last
   // call to TCintWithCling::SaveGlobalsContext().
   R__LOCKGUARD(gCINTMutex);
   G__resetglobalvar(obj);
}

//______________________________________________________________________________
void TCintWithCling::RewindDictionary()
{
   // Rewind CINT dictionary to the point where it was before executing
   // the current macro. This function is typically called after SEGV or
   // ctlr-C after doing a longjmp back to the prompt.
   R__LOCKGUARD(gCINTMutex);
   G__rewinddictionary();
}

//______________________________________________________________________________
Int_t TCintWithCling::DeleteGlobal(void* obj)
{
   // Delete obj from CINT symbol table so it cannot be accessed anymore.
   // Returns 1 in case of success and 0 in case object was not in table.
   R__LOCKGUARD(gCINTMutex);
   return G__deleteglobal(obj);
}

//______________________________________________________________________________
void TCintWithCling::SaveContext()
{
   // Save the current CINT state.
   R__LOCKGUARD(gCINTMutex);
   G__store_dictposition(&fDictPos);
}

//______________________________________________________________________________
void TCintWithCling::SaveGlobalsContext()
{
   // Save the current CINT state of global objects.
   R__LOCKGUARD(gCINTMutex);
   G__store_dictposition(&fDictPosGlobals);
}

//______________________________________________________________________________
void TCintWithCling::UpdateListOfGlobals()
{
   // Update the list of pointers to global variables. This function
   // is called by TROOT::GetListOfGlobals().
   if (!gROOT->fGlobals) {
      // No globals registered yet, trigger it:
      gROOT->GetListOfGlobals();
      // It already called us again.
      return;
   }
   if (fGlobalsListSerial == G__DataMemberInfo::SerialNumber()) {
      return;
   }
   fGlobalsListSerial = G__DataMemberInfo::SerialNumber();
   R__LOCKGUARD2(gCINTMutex);
   TClingDataMemberInfo* t = (TClingDataMemberInfo*) DataMemberInfo_Factory();
   // Loop over all global vars known to cint.
   while (t->Next()) {
      if (t->IsValid() && t->Name()) {
         // Remove any old version in the list.
         {
            TGlobal* g = (TGlobal*) gROOT->fGlobals->FindObject(t->Name());
            if (g) {
               gROOT->fGlobals->Remove(g);
               delete g;
            }
         }
         gROOT->fGlobals->Add(new TGlobal((DataMemberInfo_t*) new TClingDataMemberInfo(*t)));
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::UpdateListOfGlobalFunctions()
{
   // Update the list of pointers to global functions. This function
   // is called by TROOT::GetListOfGlobalFunctions().
   if (!gROOT->fGlobalFunctions) {
      // No global functions registered yet, trigger it:
      gROOT->GetListOfGlobalFunctions();
      // We were already called by TROOT::GetListOfGlobalFunctions()
      return;
   }
   R__LOCKGUARD2(gCINTMutex);
   TClingMethodInfo t(fInterpreter);
   while (t.Next()) {
      // if name cannot be obtained no use to put in list
      if (t.IsValid() && t.Name()) {
         Bool_t needToAdd = kTRUE;
         // first remove if already in list
         TList* listFuncs = ((THashTable*)(gROOT->fGlobalFunctions))->
            GetListForObject(t.Name());
         if (listFuncs && t.InterfaceMethod()) {
            Long_t prop = -1;
            TIter iFunc(listFuncs);
            Bool_t foundStart = kFALSE;
            TFunction* f = 0;
            while (needToAdd && (f = (TFunction*)iFunc())) {
               if (strcmp(f->GetName(), t.Name())) {
                  if (foundStart) {
                     break;
                  }
                  continue;
               }
               foundStart = kTRUE;
               if (f->InterfaceMethod()) {
                  if (prop == -1) {
                     prop = t.Property();
                  }
                  needToAdd = !(prop & G__BIT_ISCOMPILED) &&
                     (t.GetMangledName() != f->GetMangledName());
               }
            }
         }
         if (needToAdd) {
            gROOT->fGlobalFunctions->Add(new TFunction(new TClingMethodInfo(t)));
         }
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::UpdateListOfTypes()
{
   // Update the list of pointers to Datatype (typedef) definitions. This
   // function is called by TROOT::GetListOfTypes().
   R__LOCKGUARD2(gCINTMutex);
   //////// Remember the index of the last type that we looked at,
   //////// so that we don't keep reprocessing the same types.
   //////static int last_typenum = -1;
   //////// Also remember the count from the last time the dictionary
   //////// was rewound.  If it's been rewound since the last time we've
   //////// been called, then we recan everything.
   //////static int last_scratch_count = 0;
   //////int this_scratch_count = G__scratch_upto(0);
   //////if (this_scratch_count != last_scratch_count) {
   //////   last_scratch_count = this_scratch_count;
   //////   last_typenum = -1;
   //////}
   //////// Scan from where we left off last time.
   TClingTypedefInfo* t = (TClingTypedefInfo*) TypedefInfo_Factory();
   while (t->Next()) {
      const char* name = t->Name();
      if (gROOT && gROOT->fTypes && t->IsValid() && name) {
         TDataType* d = (TDataType*) gROOT->fTypes->FindObject(name);
         // only add new types, don't delete old ones with the same name
         // (as is done in UpdateListOfGlobals()),
         // this 'feature' is being used in TROOT::GetType().
         if (!d) {
            gROOT->fTypes->Add(new TDataType(new TClingTypedefInfo(*t)));
         }
         //////last_typenum = t->Typenum();
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::SetClassInfo(TClass* cl, Bool_t reload)
{
   // Set pointer to the TClingClassInfo in TClass.
   R__LOCKGUARD2(gCINTMutex);
   if (cl->fClassInfo && !reload) {
      return;
   }
   delete (TClingClassInfo*) cl->fClassInfo;
   cl->fClassInfo = 0;
   std::string name(cl->GetName());
   TClingClassInfo* info = new TClingClassInfo(fInterpreter, name.c_str());
   if (!info->IsValid()) {
      bool cint_class_exists = CheckClassInfo(name.c_str());
      if (!cint_class_exists) {
         // Try resolving all the typedefs (even Float_t and Long64_t).
         name = TClassEdit::ResolveTypedef(name.c_str(), kTRUE);
         if (name == cl->GetName()) {
            // No typedefs found, all done.
            return;
         }
         // Try the new name.
         cint_class_exists = CheckClassInfo(name.c_str());
         if (!cint_class_exists) {
            // Nothing found, nothing to do.
            return;
         }
      }
      info = new TClingClassInfo(fInterpreter, name.c_str());
      if (!info->IsValid()) {
         // Failed, done.
         return;
      }
   }
   cl->fClassInfo = info; // Note: We are transfering ownership here.
   // In case a class contains an external enum, the enum will be seen as a
   // class. We must detect this special case and make the class a Zombie.
   // Here we assume that a class has at least one method.
   // We can NOT call TClass::Property from here, because this method
   // assumes that the TClass is well formed to do a lot of information
   // caching. The method SetClassInfo (i.e. here) is usually called during
   // the building phase of the TClass, hence it is NOT well formed yet.
   Bool_t zombieCandidate = kFALSE;
   if (
      info->IsValid() &&
      !(info->Property() & (kIsClass | kIsStruct | kIsNamespace))
   ) {
      zombieCandidate = kTRUE;
   }
   if (!info->IsLoaded()) {
      if (info->Property() & (kIsNamespace)) {
         // Namespaces can have info but no corresponding CINT dictionary
         // because they are auto-created if one of their contained
         // classes has a dictionary.
         zombieCandidate = kTRUE;
      }
      // this happens when no CINT dictionary is available
      delete info;
      cl->fClassInfo = 0;
   }
   if (zombieCandidate && !TClassEdit::IsSTLCont(cl->GetName())) {
      cl->MakeZombie();
   }
}

//______________________________________________________________________________
Bool_t TCintWithCling::CheckClassInfo(const char* name, Bool_t autoload /*= kTRUE*/)
{
   // Checks if a class with the specified name is defined in CINT.
   // Returns kFALSE is class is not defined.
   // In the case where the class is not loaded and belongs to a namespace
   // or is nested, looking for the full class name is outputing a lots of
   // (expected) error messages.  Currently the only way to avoid this is to
   // specifically check that each level of nesting is already loaded.
   // In case of templates the idea is that everything between the outer
   // '<' and '>' has to be skipped, e.g.: aap<pipo<noot>::klaas>::a_class
   R__LOCKGUARD(gCINTMutex);
   Int_t nch = strlen(name) * 2;
   char* classname = new char[nch];
   strlcpy(classname, name, nch);
   char* current = classname;
   while (*current) {
      while (*current && *current != ':' && *current != '<') {
         current++;
      }
      if (!*current) {
         break;
      }
      if (*current == '<') {
         int level = 1;
         current++;
         while (*current && level > 0) {
            if (*current == '<') {
               level++;
            }
            if (*current == '>') {
               level--;
            }
            current++;
         }
         continue;
      }
      // *current == ':', must be a "::"
      if (*(current + 1) != ':') {
         Error("CheckClassInfo", "unexpected token : in %s", classname);
         delete[] classname;
         return kFALSE;
      }
      *current = '\0';
      TClingClassInfo info(fInterpreter, classname);
      if (!info.IsValid()) {
         delete[] classname;
         return kFALSE;
      }
      *current = ':';
      current += 2;
   }
   strlcpy(classname, name, nch);
   int flag = 2;
   if (!autoload) {
      flag = 3;
   }
   Int_t tagnum = G__defined_tagname(classname, flag); // This function might modify the name (to add space between >>).
   if (tagnum >= 0) {
      TClingClassInfo tci(fInterpreter, classname);
      if (!tci.IsValid()) {
         Warning("CheckClassInfo", "class '%s' exists in CINT, but not in the AST!\n", classname);
         delete[] classname;
         return kFALSE;
      }
      G__ClassInfo info(tagnum);
      // If autoloading is off then Property() == 0 for autoload entries.
      if (!autoload && !info.Property()) {
         return kTRUE;
      }
      if (info.Property() & (G__BIT_ISENUM | G__BIT_ISCLASS | G__BIT_ISSTRUCT | G__BIT_ISUNION | G__BIT_ISNAMESPACE)) {
         // We are now sure that the entry is not in fact an autoload entry.
         delete[] classname;
         return kTRUE;
      }
   }
   TClingTypedefInfo t(fInterpreter, name);
   if (t.IsValid() && !(t.Property() & G__BIT_ISFUNDAMENTAL)) {
      delete[] classname;
      return kTRUE;
   }
   delete[] classname;
   return kFALSE;
}

//______________________________________________________________________________
void TCintWithCling::CreateListOfBaseClasses(TClass* cl)
{
   // Create list of pointers to base class(es) for TClass cl.
   R__LOCKGUARD2(gCINTMutex);
   if (cl->fBase) {
      return;
   }
   cl->fBase = new TList;
   TClingClassInfo tci(fInterpreter, cl->GetName());
   TClingBaseClassInfo t(fInterpreter, &tci);
   while (t.Next()) {
      // if name cannot be obtained no use to put in list
      if (t.IsValid() && t.Name()) {
         TClingBaseClassInfo* a = new TClingBaseClassInfo(t);
         cl->fBase->Add(new TBaseClass(a, cl));
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::CreateListOfDataMembers(TClass* cl)
{
   // Create list of pointers to data members for TClass cl.
   R__LOCKGUARD2(gCINTMutex);
   if (cl->fData) {
      return;
   }
   cl->fData = new TList;
   TClingDataMemberInfo t(fInterpreter, (TClingClassInfo*)cl->GetClassInfo());
   while (t.Next()) {
      // if name cannot be obtained no use to put in list
      if (t.IsValid() && t.Name() && strcmp(t.Name(), "G__virtualinfo")) {
         TClingDataMemberInfo* a = new TClingDataMemberInfo(t);
         cl->fData->Add(new TDataMember(a, cl));
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::CreateListOfMethods(TClass* cl)
{
   // Create list of pointers to methods for TClass cl.
   R__LOCKGUARD2(gCINTMutex);
   if (cl->fMethod) {
      return;
   }
   cl->fMethod = new THashList;
   TClingMethodInfo t(fInterpreter, (TClingClassInfo*)cl->GetClassInfo());
   while (t.Next()) {
      // if name cannot be obtained no use to put in list
      if (t.IsValid() && t.Name()) {
         TClingMethodInfo* a = new TClingMethodInfo(t);
         cl->fMethod->Add(new TMethod(a, cl));
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::UpdateListOfMethods(TClass* cl)
{
   // Update the list of pointers to method for TClass cl, if necessary
   delete cl->fMethod;
   cl->fMethod = 0;
   CreateListOfMethods(cl);
}

//______________________________________________________________________________
void TCintWithCling::CreateListOfMethodArgs(TFunction* m)
{
   // Create list of pointers to method arguments for TMethod m.
   R__LOCKGUARD2(gCINTMutex);
   if (m->fMethodArgs) {
      return;
   }
   m->fMethodArgs = new TList;
   TClingMethodArgInfo t(fInterpreter, (TClingMethodInfo*)m->fInfo);
   while (t.Next()) {
      if (t.IsValid()) {
         TClingMethodArgInfo* a = new TClingMethodArgInfo(t);
         m->fMethodArgs->Add(new TMethodArg(a, m));
      }
   }
}

//______________________________________________________________________________
Int_t TCintWithCling::GenerateDictionary(const char* classes, const char* includes /* = 0 */, const char* /* options  = 0 */)
{
   // Generate the dictionary for the C++ classes listed in the first
   // argmument (in a semi-colon separated list).
   // 'includes' contains a semi-colon separated list of file to
   // #include in the dictionary.
   // For example:
   //    gInterpreter->GenerateDictionary("vector<vector<float> >;list<vector<float> >","list;vector");
   // or
   //    gInterpreter->GenerateDictionary("myclass","myclass.h;myhelper.h");
   if (classes == 0 || classes[0] == 0) {
      return 0;
   }
   // Split the input list
   std::vector<std::string> listClasses;
   for (
      const char* current = classes, *prev = classes;
      *current != 0;
      ++current
   ) {
      if (*current == ';') {
         listClasses.push_back(std::string(prev, current - prev));
         prev = current + 1;
      }
      else if (*(current + 1) == 0) {
         listClasses.push_back(std::string(prev, current + 1 - prev));
         prev = current + 1;
      }
   }
   std::vector<std::string> listIncludes;
   for (
      const char* current = includes, *prev = includes;
      *current != 0;
      ++current
   ) {
      if (*current == ';') {
         listIncludes.push_back(std::string(prev, current - prev));
         prev = current + 1;
      }
      else if (*(current + 1) == 0) {
         listIncludes.push_back(std::string(prev, current + 1 - prev));
         prev = current + 1;
      }
   }
   // Generate the temporary dictionary file
   return TCint_GenerateDictionary(listClasses, listIncludes,
      std::vector<std::string>(), std::vector<std::string>());
}


//______________________________________________________________________________
TString TCintWithCling::GetMangledName(TClass* cl, const char* method,
                              const char* params)
{
   // Return the CINT mangled name for a method of a class with parameters
   // params (params is a string of actual arguments, not formal ones). If the
   // class is 0 the global function list will be searched.
   R__LOCKGUARD2(gCINTMutex);
   TClingCallFunc func(fInterpreter);
   if (cl) {
      Long_t offset;
      func.SetFunc((TClingClassInfo*)cl->GetClassInfo(), method, params,
         &offset);
   }
   else {
      TClingClassInfo gcl(fInterpreter);
      Long_t offset;
      func.SetFunc(&gcl, method, params, &offset);
   }
   TClingMethodInfo* mi = (TClingMethodInfo*) func.FactoryMethod();
   const char* mangled_name = mi->GetMangledName();
   delete mi;
   mi = 0;
   return mangled_name;
}

//______________________________________________________________________________
TString TCintWithCling::GetMangledNameWithPrototype(TClass* cl, const char* method,
      const char* proto)
{
   // Return the CINT mangled name for a method of a class with a certain
   // prototype, i.e. "char*,int,float". If the class is 0 the global function
   // list will be searched.
   R__LOCKGUARD2(gCINTMutex);
   Long_t offset;
   if (cl) {
      return ((TClingClassInfo*)cl->GetClassInfo())->
             GetMethod(method, proto, &offset).GetMangledName();
   }
   TClingClassInfo gcl(fInterpreter);
   return gcl.GetMethod(method, proto, &offset).GetMangledName();
}

//______________________________________________________________________________
void* TCintWithCling::GetInterfaceMethod(TClass* cl, const char* method,
                                const char* params)
{
   // Return pointer to CINT interface function for a method of a class with
   // parameters params (params is a string of actual arguments, not formal
   // ones). If the class is 0 the global function list will be searched.
   R__LOCKGUARD2(gCINTMutex);
   TClingCallFunc func(fInterpreter);
   if (cl) {
      Long_t offset;
      func.SetFunc((TClingClassInfo*)cl->GetClassInfo(), method, params,
         &offset);
   }
   else {
      TClingClassInfo gcl(fInterpreter);
      Long_t offset;
      func.SetFunc(&gcl, method, params, &offset);
   }
   return (void*) func.InterfaceMethod();
}

//______________________________________________________________________________
void* TCintWithCling::GetInterfaceMethodWithPrototype(TClass* cl, const char* method,
      const char* proto)
{
   // Return pointer to CINT interface function for a method of a class with
   // a certain prototype, i.e. "char*,int,float". If the class is 0 the global
   // function list will be searched.
   R__LOCKGUARD2(gCINTMutex);
   void* f;
   if (cl) {
      Long_t offset;
      f = ((TClingClassInfo*)cl->GetClassInfo())->
          GetMethod(method, proto, &offset).InterfaceMethod();
   }
   else {
      Long_t offset;
      TClingClassInfo gcl(fInterpreter);
      f = gcl.GetMethod(method, proto, &offset).InterfaceMethod();
   }
   return f;
}

//______________________________________________________________________________
const char* TCintWithCling::GetInterpreterTypeName(const char* name, Bool_t full)
{
   // The 'name' is known to the interpreter, this function returns
   // the internal version of this name (usually just resolving typedefs)
   // This is used in particular to synchronize between the name used
   // by rootcint and by the run-time enviroment (TClass)
   // Return 0 if the name is not known.
   R__LOCKGUARD(gCINTMutex);
   if (!gInterpreter->CheckClassInfo(name)) {
      return 0;
   }
   TClingClassInfo cl(fInterpreter, name);
   if (!cl.IsValid()) {
      return 0;
   }
   if (full) {
      return cl.FullName();
   }
   // Well well well, for backward compatibility we need to act a bit too
   // much like CINT.
   TClassEdit::TSplitType splitname( cl.Name(), TClassEdit::kDropStd );
   static std::string result;
   splitname.ShortType(result, TClassEdit::kDropStd );
   return result.c_str();
}

//______________________________________________________________________________
void TCintWithCling::Execute(const char* function, const char* params, int* error)
{
   // Execute a global function with arguments params.
   R__LOCKGUARD2(gCINTMutex);
   TClingClassInfo cl(fInterpreter);
   Long_t offset;
   TClingCallFunc func(fInterpreter);
   func.SetFunc(&cl, function, params, &offset);
   func.Exec(0);
   if (error) {
      *error = G__lasterror();
   }
}

//______________________________________________________________________________
void TCintWithCling::Execute(TObject* obj, TClass* cl, const char* method,
                    const char* params, int* error)
{
   // Execute a method from class cl with arguments params.
   R__LOCKGUARD2(gCINTMutex);
   // If the actual class of this object inherits 2nd (or more) from TObject,
   // 'obj' is unlikely to be the start of the object (as described by IsA()),
   // hence gInterpreter->Execute will improperly correct the offset.
   void* addr = cl->DynamicCast(TObject::Class(), obj, kFALSE);
   Long_t offset = 0L;
   TClingCallFunc func(fInterpreter);
   func.SetFunc((TClingClassInfo*)cl->GetClassInfo(), method, params, &offset);
   void* address = (void*)((Long_t)addr + offset);
   func.Exec(address);
   if (error) {
      *error = G__lasterror();
   }
}

//______________________________________________________________________________
void TCintWithCling::Execute(TObject* obj, TClass* cl, TMethod* method,
      TObjArray* params, int* error)
{
   // Execute a method from class cl with the arguments in array params
   // (params[0] ... params[n] = array of TObjString parameters).
   // Convert the TObjArray array of TObjString parameters to a character
   // string of comma separated parameters.
   // The parameters of type 'char' are enclosed in double quotes and all
   // internal quotes are escaped.
   if (!method) {
      Error("Execute", "No method was defined");
      return;
   }
   TList* argList = method->GetListOfMethodArgs();
   // Check number of actual parameters against of expected formal ones
   Int_t nparms = argList->LastIndex() + 1;
   Int_t argc   = params ? params->LastIndex() + 1 : 0;
   if (nparms != argc) {
      Error("Execute", "Wrong number of the parameters");
      return;
   }
   const char* listpar = "";
   TString complete(10);
   if (params) {
      // Create a character string of parameters from TObjArray
      TIter next(params);
      for (Int_t i = 0; i < argc; i ++) {
         TMethodArg* arg = (TMethodArg*) argList->At(i);
         TClingTypeInfo type(fInterpreter, arg->GetFullTypeName());
         TObjString* nxtpar = (TObjString*) next();
         if (i) {
            complete += ',';
         }
         if (strstr(type.TrueName(), "char")) {
            TString chpar('\"');
            chpar += (nxtpar->String()).ReplaceAll("\"", "\\\"");
            // At this point we have to check if string contains \\"
            // and apply some more sophisticated parser. Not implemented yet!
            complete += chpar;
            complete += '\"';
         }
         else {
            complete += nxtpar->String();
         }
      }
      listpar = complete.Data();
   }
   Execute(obj, cl, const_cast<char*>(method->GetName()), const_cast<char*>(listpar), error);
}

//______________________________________________________________________________
Long_t TCintWithCling::ExecuteMacro(const char* filename, EErrorCode* error)
{
   // Execute a CINT macro.
   R__LOCKGUARD(gCINTMutex);
   return TApplication::ExecuteFile(filename, (int*)error);
}

//______________________________________________________________________________
const char* TCintWithCling::GetTopLevelMacroName() const
{
   // Return the file name of the current un-included interpreted file.
   // See the documentation for GetCurrentMacroName().
   G__SourceFileInfo srcfile(G__get_ifile()->filenum);
   while (srcfile.IncludedFrom().IsValid()) {
      srcfile = srcfile.IncludedFrom();
   }
   return srcfile.Name();
}

//______________________________________________________________________________
const char* TCintWithCling::GetCurrentMacroName() const
{
   // Return the file name of the currently interpreted file,
   // included or not. Example to illustrate the difference between
   // GetCurrentMacroName() and GetTopLevelMacroName():
   // BEGIN_HTML <!--
   /* -->
      <span style="color:#ffffff;background-color:#7777ff;padding-left:0.3em;padding-right:0.3em">inclfile.h</span>
      <!--div style="border:solid 1px #ffff77;background-color: #ffffdd;float:left;padding:0.5em;margin-bottom:0.7em;"-->
      <div class="code">
      <pre style="margin:0pt">#include &lt;iostream&gt;
   void inclfunc() {
   std::cout &lt;&lt; "In inclfile.h" &lt;&lt; std::endl;
   std::cout &lt;&lt; "  TCintWithCling::GetCurrentMacroName() returns  " &lt;&lt;
      TCintWithCling::GetCurrentMacroName() &lt;&lt; std::endl;
   std::cout &lt;&lt; "  TCintWithCling::GetTopLevelMacroName() returns " &lt;&lt;
      TCintWithCling::GetTopLevelMacroName() &lt;&lt; std::endl;
   }</pre></div>
      <div style="clear:both"></div>
      <span style="color:#ffffff;background-color:#7777ff;padding-left:0.3em;padding-right:0.3em">mymacro.C</span>
      <div style="border:solid 1px #ffff77;background-color: #ffffdd;float:left;padding:0.5em;margin-bottom:0.7em;">
      <pre style="margin:0pt">#include &lt;iostream&gt;
   #include "inclfile.h"
   void mymacro() {
   std::cout &lt;&lt; "In mymacro.C" &lt;&lt; std::endl;
   std::cout &lt;&lt; "  TCintWithCling::GetCurrentMacroName() returns  " &lt;&lt;
      TCintWithCling::GetCurrentMacroName() &lt;&lt; std::endl;
   std::cout &lt;&lt; "  TCintWithCling::GetTopLevelMacroName() returns " &lt;&lt;
      TCintWithCling::GetTopLevelMacroName() &lt;&lt; std::endl;
   std::cout &lt;&lt; "  Now calling inclfunc..." &lt;&lt; std::endl;
   inclfunc();
   }</pre></div>
   <div style="clear:both"></div>
   <!-- */
   // --> END_HTML
   // Running mymacro.C will print:
   //
   // root [0] .x mymacro.C
   // In mymacro.C
   //   TCintWithCling::GetCurrentMacroName() returns  ./mymacro.C
   //   TCintWithCling::GetTopLevelMacroName() returns ./mymacro.C
   //   Now calling inclfunc...
   // In inclfile.h
   //   TCintWithCling::GetCurrentMacroName() returns  inclfile.h
   //   TCintWithCling::GetTopLevelMacroName() returns ./mymacro.C
   return G__get_ifile()->name;
}

//______________________________________________________________________________
const char* TCintWithCling::TypeName(const char* typeDesc)
{
   // Return the absolute type of typeDesc.
   // E.g.: typeDesc = "class TNamed**", returns "TNamed".
   // You need to use the result immediately before it is being overwritten.
   static char* t = 0;
   static unsigned int tlen = 0;
   R__LOCKGUARD(gCINTMutex); // Because of the static array.
   unsigned int dlen = strlen(typeDesc);
   if (dlen > tlen) {
      delete[] t;
      t = new char[dlen + 1];
      tlen = dlen;
   }
   char* s, *template_start;
   if (!strstr(typeDesc, "(*)(")) {
      s = const_cast<char*>(strchr(typeDesc, ' '));
      template_start = const_cast<char*>(strchr(typeDesc, '<'));
      if (!strcmp(typeDesc, "long long")) {
         strlcpy(t, typeDesc, dlen + 1);
      }
      else if (!strncmp(typeDesc, "unsigned ", s + 1 - typeDesc)) {
         strlcpy(t, typeDesc, dlen + 1);
      }
      // s is the position of the second 'word' (if any)
      // except in the case of templates where there will be a space
      // just before any closing '>': eg.
      //    TObj<std::vector<UShort_t,__malloc_alloc_template<0> > >*
      else if (s && (template_start == 0 || (s < template_start))) {
         strlcpy(t, s + 1, dlen + 1);
      }
      else {
         strlcpy(t, typeDesc, dlen + 1);
      }
   }
   else {
      strlcpy(t, typeDesc, dlen + 1);
   }
   int l = strlen(t);
   while (l > 0 && (t[l - 1] == '*' || t[l - 1] == '&')) {
      t[--l] = 0;
   }
   return t;
}

//______________________________________________________________________________
Int_t TCintWithCling::LoadLibraryMap(const char* rootmapfile)
{
   // Load map between class and library. If rootmapfile is specified a
   // specific rootmap file can be added (typically used by ACLiC).
   // In case of error -1 is returned, 0 otherwise.
   // Cint uses this information to automatically load the shared library
   // for a class (autoload mechanism).
   // See also the AutoLoadCallback() method below.
   R__LOCKGUARD(gCINTMutex);
   // open the [system].rootmap files
   if (!fMapfile) {
      fMapfile = new TEnv(".rootmap");
      fMapfile->IgnoreDuplicates(kTRUE);
      fRootmapFiles = new TObjArray;
      fRootmapFiles->SetOwner();
      // Make sure that this information will be useable by inserting our
      // autoload call back!
      G__set_class_autoloading_callback(&TCint_AutoLoadCallback);
   }
   // Load all rootmap files in the dynamic load path ((DY)LD_LIBRARY_PATH, etc.).
   // A rootmap file must end with the string ".rootmap".
   TString ldpath = gSystem->GetDynamicPath();
   if (ldpath != fRootmapLoadPath) {
      fRootmapLoadPath = ldpath;
#ifdef WIN32
      TObjArray* paths = ldpath.Tokenize(";");
#else
      TObjArray* paths = ldpath.Tokenize(":");
#endif
      TString d;
      for (Int_t i = 0; i < paths->GetEntriesFast(); i++) {
         d = ((TObjString*)paths->At(i))->GetString();
         // check if directory already scanned
         Int_t skip = 0;
         for (Int_t j = 0; j < i; j++) {
            TString pd = ((TObjString*)paths->At(j))->GetString();
            if (pd == d) {
               skip++;
               break;
            }
         }
         if (!skip) {
            void* dirp = gSystem->OpenDirectory(d);
            if (dirp) {
               if (gDebug > 3) {
                  Info("LoadLibraryMap", "%s", d.Data());
               }
               const char* f1;
               while ((f1 = gSystem->GetDirEntry(dirp))) {
                  TString f = f1;
                  if (f.EndsWith(".rootmap")) {
                     TString p;
                     p = d + "/" + f;
                     if (!gSystem->AccessPathName(p, kReadPermission)) {
                        if (!fRootmapFiles->FindObject(f) && f != ".rootmap") {
                           if (gDebug > 4) {
                              Info("LoadLibraryMap", "   rootmap file: %s", p.Data());
                           }
                           fMapfile->ReadFile(p, kEnvGlobal);
                           fRootmapFiles->Add(new TNamed(f, p));
                        }
                        //                        else {
                        //                           fprintf(stderr,"Reject %s because %s is already there\n",p.Data(),f.Data());
                        //                           fRootmapFiles->FindObject(f)->ls();
                        //                        }
                     }
                  }
                  if (f.BeginsWith("rootmap")) {
                     TString p;
                     p = d + "/" + f;
                     FileStat_t stat;
                     if (gSystem->GetPathInfo(p, stat) == 0 && R_ISREG(stat.fMode)) {
                        Warning("LoadLibraryMap", "please rename %s to end with \".rootmap\"", p.Data());
                     }
                  }
               }
            }
            gSystem->FreeDirectory(dirp);
         }
      }
      delete paths;
      if (!fMapfile->GetTable()->GetEntries()) {
         return -1;
      }
   }
   if (rootmapfile && *rootmapfile) {
      // Add content of a specific rootmap file
      Bool_t ignore = fMapfile->IgnoreDuplicates(kFALSE);
      fMapfile->ReadFile(rootmapfile, kEnvGlobal);
      fRootmapFiles->Add(new TNamed(gSystem->BaseName(rootmapfile), rootmapfile));
      fMapfile->IgnoreDuplicates(ignore);
   }
   TEnvRec* rec;
   TIter next(fMapfile->GetTable());
   while ((rec = (TEnvRec*) next())) {
      TString cls = rec->GetName();
      if (!strncmp(cls.Data(), "Library.", 8) && cls.Length() > 8) {
         // get the first lib from the list of lib and dependent libs
         TString libs = rec->GetValue();
         if (libs == "") {
            continue;
         }
         TString delim(" ");
         TObjArray* tokens = libs.Tokenize(delim);
         const char* lib = ((TObjString*)tokens->At(0))->GetName();
         // convert "@@" to "::", we used "@@" because TEnv
         // considers "::" a terminator
         cls.Remove(0, 8);
         cls.ReplaceAll("@@", "::");
         // convert "-" to " ", since class names may have
         // blanks and TEnv considers a blank a terminator
         cls.ReplaceAll("-", " ");
         if (cls.Contains(":")) {
            // We have a namespace and we have to check it first
            int slen = cls.Length();
            for (int k = 0; k < slen; k++) {
               if (cls[k] == ':') {
                  if (k + 1 >= slen || cls[k + 1] != ':') {
                     // we expected another ':'
                     break;
                  }
                  if (k) {
                     TString base = cls(0, k);
                     if (base == "std") {
                        // std is not declared but is also ignored by CINT!
                        break;
                     }
                     else {
                        // Only declared the namespace do not specify any library because
                        // the namespace might be spread over several libraries and we do not
                        // know (yet?) which one the user will need!
                        // But what if it's not a namespace but a class?
                        // Does CINT already know it?
                        const char* baselib = G__get_class_autoloading_table(const_cast<char*>(base.Data()));
                        if ((!baselib || !baselib[0]) && !rec->FindObject(base)) {
                           G__set_class_autoloading_table(const_cast<char*>(base.Data()), const_cast<char*>(""));
                        }
                     }
                     ++k;
                  }
               }
               else if (cls[k] == '<') {
                  // We do not want to look at the namespace inside the template parameters!
                  break;
               }
            }
         }
         G__set_class_autoloading_table(const_cast<char*>(cls.Data()), const_cast<char*>(lib));
         G__security_recover(stderr); // Ignore any error during this setting.
         if (gDebug > 6) {
            const char* wlib = gSystem->DynamicPathName(lib, kTRUE);
            if (wlib) {
               Info("LoadLibraryMap", "class %s in %s", cls.Data(), wlib);
            }
            else {
               Info("LoadLibraryMap", "class %s in %s (library does not exist)", cls.Data(), lib);
            }
            delete[] wlib;
         }
         delete tokens;
      }
   }
   return 0;
}

//______________________________________________________________________________
Int_t TCintWithCling::RescanLibraryMap()
{
   // Scan again along the dynamic path for library maps. Entries for the loaded
   // shared libraries are unloaded first. This can be useful after reseting
   // the dynamic path through TSystem::SetDynamicPath()
   // In case of error -1 is returned, 0 otherwise.
   UnloadAllSharedLibraryMaps();
   LoadLibraryMap();
   return 0;
}

//______________________________________________________________________________
Int_t TCintWithCling::ReloadAllSharedLibraryMaps()
{
   // Reload the library map entries coming from all the loaded shared libraries,
   // after first unloading the current ones.
   // In case of error -1 is returned, 0 otherwise.
   const TString sharedLibLStr = GetSharedLibs();
   const TObjArray* sharedLibL = sharedLibLStr.Tokenize(" ");
   const Int_t nrSharedLibs = sharedLibL->GetEntriesFast();
   for (Int_t ilib = 0; ilib < nrSharedLibs; ilib++) {
      const TString sharedLibStr = ((TObjString*)sharedLibL->At(ilib))->GetString();
      const  TString sharedLibBaseStr = gSystem->BaseName(sharedLibStr);
      const Int_t ret = UnloadLibraryMap(sharedLibBaseStr);
      if (ret < 0) {
         continue;
      }
      TString rootMapBaseStr = sharedLibBaseStr;
      if (sharedLibBaseStr.EndsWith(".dll")) {
         rootMapBaseStr.ReplaceAll(".dll", "");
      }
      else if (sharedLibBaseStr.EndsWith(".DLL")) {
         rootMapBaseStr.ReplaceAll(".DLL", "");
      }
      else if (sharedLibBaseStr.EndsWith(".so")) {
         rootMapBaseStr.ReplaceAll(".so", "");
      }
      else if (sharedLibBaseStr.EndsWith(".sl")) {
         rootMapBaseStr.ReplaceAll(".sl", "");
      }
      else if (sharedLibBaseStr.EndsWith(".dl")) {
         rootMapBaseStr.ReplaceAll(".dl", "");
      }
      else if (sharedLibBaseStr.EndsWith(".a")) {
         rootMapBaseStr.ReplaceAll(".a", "");
      }
      else {
         Error("ReloadAllSharedLibraryMaps", "Unknown library type %s", sharedLibBaseStr.Data());
         delete sharedLibL;
         return -1;
      }
      rootMapBaseStr += ".rootmap";
      const char* rootMap = gSystem->Which(gSystem->GetDynamicPath(), rootMapBaseStr);
      if (!rootMap) {
         Error("ReloadAllSharedLibraryMaps", "Could not find rootmap %s in path", rootMap);
         delete[] rootMap;
         delete sharedLibL;
         return -1;
      }
      const Int_t status = LoadLibraryMap(rootMap);
      if (status < 0) {
         Error("ReloadAllSharedLibraryMaps", "Error loading map %s", rootMap);
         delete[] rootMap;
         delete sharedLibL;
         return -1;
      }
      delete[] rootMap;
   }
   delete sharedLibL;
   return 0;
}

//______________________________________________________________________________
Int_t TCintWithCling::UnloadAllSharedLibraryMaps()
{
   // Unload the library map entries coming from all the loaded shared libraries.
   // Returns 0 if succesful
   const TString sharedLibLStr = GetSharedLibs();
   const TObjArray* sharedLibL = sharedLibLStr.Tokenize(" ");
   for (Int_t ilib = 0; ilib < sharedLibL->GetEntriesFast(); ilib++) {
      const TString sharedLibStr = ((TObjString*)sharedLibL->At(ilib))->GetString();
      const  TString sharedLibBaseStr = gSystem->BaseName(sharedLibStr);
      UnloadLibraryMap(sharedLibBaseStr);
   }
   delete sharedLibL;
   return 0;
}

//______________________________________________________________________________
Int_t TCintWithCling::UnloadLibraryMap(const char* library)
{
   // Unload library map entries coming from the specified library.
   // Returns -1 in case no entries for the specified library were found,
   // 0 otherwise.
   if (!fMapfile || !library || !*library) {
      return 0;
   }
   TEnvRec* rec;
   TIter next(fMapfile->GetTable());
   R__LOCKGUARD(gCINTMutex);
   Int_t ret = 0;
   while ((rec = (TEnvRec*) next())) {
      TString cls = rec->GetName();
      if (!strncmp(cls.Data(), "Library.", 8) && cls.Length() > 8) {
         // get the first lib from the list of lib and dependent libs
         TString libs = rec->GetValue();
         if (libs == "") {
            continue;
         }
         TString delim(" ");
         TObjArray* tokens = libs.Tokenize(delim);
         const char* lib = ((TObjString*)tokens->At(0))->GetName();
         // convert "@@" to "::", we used "@@" because TEnv
         // considers "::" a terminator
         cls.Remove(0, 8);
         cls.ReplaceAll("@@", "::");
         // convert "-" to " ", since class names may have
         // blanks and TEnv considers a blank a terminator
         cls.ReplaceAll("-", " ");
         if (cls.Contains(":")) {
            // We have a namespace and we have to check it first
            int slen = cls.Length();
            for (int k = 0; k < slen; k++) {
               if (cls[k] == ':') {
                  if (k + 1 >= slen || cls[k + 1] != ':') {
                     // we expected another ':'
                     break;
                  }
                  if (k) {
                     TString base = cls(0, k);
                     if (base == "std") {
                        // std is not declared but is also ignored by CINT!
                        break;
                     }
                     else {
                        // Only declared the namespace do not specify any library because
                        // the namespace might be spread over several libraries and we do not
                        // know (yet?) which one the user will need!
                        //G__remove_from_class_autoloading_table((char*)base.Data());
                     }
                     ++k;
                  }
               }
               else if (cls[k] == '<') {
                  // We do not want to look at the namespace inside the template parameters!
                  break;
               }
            }
         }
         if (!strcmp(library, lib)) {
            if (fMapfile->GetTable()->Remove(rec) == 0) {
               Error("UnloadLibraryMap", "entry for <%s,%s> not found in library map table", cls.Data(), lib);
               ret = -1;
            }
            G__set_class_autoloading_table(const_cast<char*>(cls.Data()), (char*)(-1));
            G__security_recover(stderr); // Ignore any error during this setting.
         }
         delete tokens;
      }
   }
   if (ret >= 0) {
      TString library_rootmap(library);
      library_rootmap.Append(".rootmap");
      TNamed* mfile = 0;
      while ((mfile = (TNamed*)fRootmapFiles->FindObject(library_rootmap))) {
         fRootmapFiles->Remove(mfile);
         delete mfile;
      }
      fRootmapFiles->Compress();
   }
   return ret;
}

//______________________________________________________________________________
Int_t TCintWithCling::AutoLoad(const char* cls)
{
   // Load library containing the specified class. Returns 0 in case of error
   // and 1 in case if success.
   R__LOCKGUARD(gCINTMutex);
   Int_t status = 0;
   if (!gROOT || !gInterpreter || gROOT->TestBit(TObject::kInvalidObject)) {
      return status;
   }
   // Prevent the recursion when the library dictionary are loaded.
   Int_t oldvalue = G__set_class_autoloading(0);
   // lookup class to find list of dependent libraries
   TString deplibs = GetClassSharedLibs(cls);
   if (!deplibs.IsNull()) {
      TString delim(" ");
      TObjArray* tokens = deplibs.Tokenize(delim);
      for (Int_t i = tokens->GetEntriesFast() - 1; i > 0; i--) {
         const char* deplib = ((TObjString*)tokens->At(i))->GetName();
         if (gROOT->LoadClass(cls, deplib) == 0) {
            if (gDebug > 0)
               ::Info("TCintWithCling::AutoLoad", "loaded dependent library %s for class %s",
                      deplib, cls);
         }
         else
            ::Error("TCintWithCling::AutoLoad", "failure loading dependent library %s for class %s",
                    deplib, cls);
      }
      const char* lib = ((TObjString*)tokens->At(0))->GetName();
      if (lib[0]) {
         if (gROOT->LoadClass(cls, lib) == 0) {
            if (gDebug > 0)
               ::Info("TCintWithCling::AutoLoad", "loaded library %s for class %s",
                      lib, cls);
            status = 1;
         }
         else
            ::Error("TCintWithCling::AutoLoad", "failure loading library %s for class %s",
                    lib, cls);
      }
      delete tokens;
   }
   G__set_class_autoloading(oldvalue);
   return status;
}

//______________________________________________________________________________
Int_t TCintWithCling::AutoLoadCallback(const char* cls, const char* lib)
{
   // Load library containing specified class. Returns 0 in case of error
   // and 1 in case if success.
   R__LOCKGUARD(gCINTMutex);
   if (!gROOT || !gInterpreter || !cls || !lib) {
      return 0;
   }
   // calls to load libCore might come in the very beginning when libCore
   // dictionary is not fully loaded yet, ignore it since libCore is always
   // loaded
   if (strstr(lib, "libCore")) {
      return 1;
   }
   // lookup class to find list of dependent libraries
   TString deplibs = gInterpreter->GetClassSharedLibs(cls);
   if (!deplibs.IsNull()) {
      if (gDebug > 0 && gDebug <= 4)
         ::Info("TCintWithCling::AutoLoadCallback", "loaded dependent library %s for class %s",
                deplibs.Data(), cls);
      TString delim(" ");
      TObjArray* tokens = deplibs.Tokenize(delim);
      for (Int_t i = tokens->GetEntriesFast() - 1; i > 0; i--) {
         const char* deplib = ((TObjString*)tokens->At(i))->GetName();
         if (gROOT->LoadClass(cls, deplib) == 0) {
            if (gDebug > 4)
               ::Info("TCintWithCling::AutoLoadCallback", "loaded dependent library %s for class %s",
                      deplib, cls);
         }
         else {
            ::Error("TCintWithCling::AutoLoadCallback", "failure loading dependent library %s for class %s",
                    deplib, cls);
         }
      }
      delete tokens;
   }
   if (lib[0]) {
      if (gROOT->LoadClass(cls, lib) == 0) {
         if (gDebug > 0)
            ::Info("TCintWithCling::AutoLoadCallback", "loaded library %s for class %s",
                   lib, cls);
         return 1;
      }
      else {
         ::Error("TCintWithCling::AutoLoadCallback", "failure loading library %s for class %s",
                 lib, cls);
      }
   }
   return 0;
}

//______________________________________________________________________________
//FIXME: Use of G__ClassInfo in the interface!
void* TCintWithCling::FindSpecialObject(const char* item, G__ClassInfo* type,
                               void** prevObj, void** assocPtr)
{
   // Static function called by CINT when it finds an un-indentified object.
   // This function tries to find the UO in the ROOT files, directories, etc.
   // This functions has been registered by the TCint ctor.
   if (!*prevObj || *assocPtr != gDirectory) {
      *prevObj = gROOT->FindSpecialObject(item, *assocPtr);
      if (!fgSetOfSpecials) {
         fgSetOfSpecials = new std::set<TObject*>;
      }
      if (*prevObj) {
         ((std::set<TObject*>*)fgSetOfSpecials)->insert((TObject*)*prevObj);
      }
   }
   if (*prevObj) {
      type->Init(((TObject*)*prevObj)->ClassName());
   }
   return *prevObj;
}

//______________________________________________________________________________
// Helper class for UpdateClassInfo
namespace {
class TInfoNode {
private:
   string fName;
   Long_t fTagnum;
public:
   TInfoNode(const char* item, Long_t tagnum)
      : fName(item), fTagnum(tagnum)
   {}
   void Update() {
      TCintWithCling::UpdateClassInfoWork(fName.c_str(), fTagnum);
   }
};
}

//______________________________________________________________________________
void TCintWithCling::UpdateClassInfo(char* item, Long_t tagnum)
{
   // Static function called by CINT when it changes the tagnum for
   // a class (e.g. after re-executing the setup function). In such
   // cases we have to update the tagnum in the G__ClassInfo used by
   // the TClass for class "item".
   R__LOCKGUARD(gCINTMutex);
   if (gROOT && gROOT->GetListOfClasses()) {
      static Bool_t entered = kFALSE;
      static vector<TInfoNode> updateList;
      Bool_t topLevel;
      if (entered) {
         topLevel = kFALSE;
      }
      else {
         entered = kTRUE;
         topLevel = kTRUE;
      }
      if (topLevel) {
         UpdateClassInfoWork(item, tagnum);
      }
      else {
         // If we are called indirectly from within another call to
         // TCintWithCling::UpdateClassInfo, we delay the update until the dictionary loading
         // is finished (i.e. when we return to the top level TCintWithCling::UpdateClassInfo).
         // This allows for the dictionary to be fully populated when we actually
         // update the TClass object.   The updating of the TClass sometimes
         // (STL containers and when there is an emulated class) forces the building
         // of the TClass object's real data (which needs the dictionary info).
         updateList.push_back(TInfoNode(item, tagnum));
      }
      if (topLevel) {
         while (!updateList.empty()) {
            TInfoNode current(updateList.back());
            updateList.pop_back();
            current.Update();
         }
         entered = kFALSE;
      }
   }
}

//______________________________________________________________________________
void TCintWithCling::UpdateClassInfoWork(const char* item, Long_t tagnum)
{
   // This does the actual work of UpdateClassInfo.
   Bool_t load = kFALSE;
   if (strchr(item, '<') && TClass::GetClassShortTypedefHash()) {
      // We have a template which may have duplicates.
      TString resolvedItem(
         TClassEdit::ResolveTypedef(TClassEdit::ShortType(item,
                                    TClassEdit::kDropStlDefault).c_str(), kTRUE));
      if (resolvedItem != item) {
         TClass* cl = (TClass*)gROOT->GetListOfClasses()->FindObject(resolvedItem);
         if (cl) {
            load = kTRUE;
         }
      }
      if (!load) {
         TIter next(TClass::GetClassShortTypedefHash()->GetListForObject(resolvedItem));
         while (TClass::TNameMapNode* htmp =
                   static_cast<TClass::TNameMapNode*>(next())) {
            if (resolvedItem == htmp->String()) {
               TClass* cl = gROOT->GetClass(htmp->fOrigName, kFALSE);
               if (cl) {
                  // we found at least one equivalent.
                  // let's force a reload
                  load = kTRUE;
                  break;
               }
            }
         }
      }
   }
   TClass* cl = gROOT->GetClass(item, load);
   if (cl) {
      cl->ResetClassInfo(tagnum);
   }
}

//______________________________________________________________________________
void TCintWithCling::UpdateAllCanvases()
{
   // Update all canvases at end the terminal input command.
   TIter next(gROOT->GetListOfCanvases());
   TVirtualPad* canvas;
   while ((canvas = (TVirtualPad*)next())) {
      canvas->Update();
   }
}

//______________________________________________________________________________
const char* TCintWithCling::GetSharedLibs()
{
   // Return the list of shared libraries known to CINT.
   if (fSharedLibsSerial == G__SourceFileInfo::SerialNumber()) {
      return fSharedLibs;
   }
   fSharedLibsSerial = G__SourceFileInfo::SerialNumber();
   fSharedLibs.Clear();
   G__SourceFileInfo cursor(0);
   while (cursor.IsValid()) {
      const char* filename = cursor.Name();
      if (filename == 0) {
         continue;
      }
      Int_t len = strlen(filename);
      const char* end = filename + len;
      Bool_t needToSkip = kFALSE;
      if (len > 5 && ((strcmp(end - 4, ".dll") == 0) || (strstr(filename, "Dict.") != 0)  || (strstr(filename, "MetaTCint") != 0))) {
         // Filter out the cintdlls
         static const char* excludelist [] = {
            "stdfunc.dll", "stdcxxfunc.dll", "posix.dll", "ipc.dll", "posix.dll"
            "string.dll", "vector.dll", "vectorbool.dll", "list.dll", "deque.dll",
            "map.dll", "map2.dll", "set.dll", "multimap.dll", "multimap2.dll",
            "multiset.dll", "stack.dll", "queue.dll", "valarray.dll",
            "exception.dll", "stdexcept.dll", "complex.dll", "climits.dll",
            "libvectorDict.", "libvectorboolDict.", "liblistDict.", "libdequeDict.",
            "libmapDict.", "libmap2Dict.", "libsetDict.", "libmultimapDict.", "libmultimap2Dict.",
            "libmultisetDict.", "libstackDict.", "libqueueDict.", "libvalarrayDict."
         };
         static const unsigned int excludelistsize = sizeof(excludelist) / sizeof(excludelist[0]);
         static int excludelen[excludelistsize] = { -1};
         if (excludelen[0] == -1) {
            for (unsigned int i = 0; i < excludelistsize; ++i) {
               excludelen[i] = strlen(excludelist[i]);
            }
         }
         const char* basename = gSystem->BaseName(filename);
         for (unsigned int i = 0; !needToSkip && i < excludelistsize; ++i) {
            needToSkip = (!strncmp(basename, excludelist[i], excludelen[i]));
         }
      }
      if (!needToSkip &&
            (
#if defined(R__MACOSX) && defined(MAC_OS_X_VERSION_10_5)
               (dlopen_preflight(filename)) ||
#endif
               (len > 2 && strcmp(end - 2, ".a") == 0)    ||
               (len > 3 && (strcmp(end - 3, ".sl") == 0   ||
                            strcmp(end - 3, ".dl") == 0   ||
                            strcmp(end - 3, ".so") == 0)) ||
               (len > 4 && (strcasecmp(end - 4, ".dll") == 0)) ||
               (len > 6 && (strcasecmp(end - 6, ".dylib") == 0)))) {
         if (!fSharedLibs.IsNull()) {
            fSharedLibs.Append(" ");
         }
         fSharedLibs.Append(filename);
      }
      cursor.Next();
   }
   return fSharedLibs;
}

//______________________________________________________________________________
const char* TCintWithCling::GetClassSharedLibs(const char* cls)
{
   // Get the list of shared libraries containing the code for class cls.
   // The first library in the list is the one containing the class, the
   // others are the libraries the first one depends on. Returns 0
   // in case the library is not found.
   if (!cls || !*cls) {
      return 0;
   }
   // lookup class to find list of libraries
   if (fMapfile) {
      TString c = TString("Library.") + cls;
      // convert "::" to "@@", we used "@@" because TEnv
      // considers "::" a terminator
      c.ReplaceAll("::", "@@");
      // convert "-" to " ", since class names may have
      // blanks and TEnv considers a blank a terminator
      c.ReplaceAll(" ", "-");
      // Use TEnv::Lookup here as the rootmap file must start with Library.
      // and do not support using any stars (so we do not need to waste time
      // with the search made by TEnv::GetValue).
      TEnvRec* libs_record = fMapfile->Lookup(c);
      if (libs_record) {
         const char* libs = libs_record->GetValue();
         return (*libs) ? libs : 0;
      }
   }
   return 0;
}

//______________________________________________________________________________
const char* TCintWithCling::GetSharedLibDeps(const char* lib)
{
   // Get the list a libraries on which the specified lib depends. The
   // returned string contains as first element the lib itself.
   // Returns 0 in case the lib does not exist or does not have
   // any dependencies.
   if (!fMapfile || !lib || !lib[0]) {
      return 0;
   }
   TString libname(lib);
   Ssiz_t idx = libname.Last('.');
   if (idx != kNPOS) {
      libname.Remove(idx);
   }
   TEnvRec* rec;
   TIter next(fMapfile->GetTable());
   size_t len = libname.Length();
   while ((rec = (TEnvRec*) next())) {
      const char* libs = rec->GetValue();
      if (!strncmp(libs, libname.Data(), len) && strlen(libs) >= len
            && (!libs[len] || libs[len] == ' ' || libs[len] == '.')) {
         return libs;
      }
   }
   return 0;
}

//______________________________________________________________________________
Bool_t TCintWithCling::IsErrorMessagesEnabled() const
{
   // If error messages are disabled, the interpreter should suppress its
   // failures and warning messages from stdout.
   return !G__const_whatnoerror();
}

//______________________________________________________________________________
Bool_t TCintWithCling::SetErrorMessages(Bool_t enable)
{
   // If error messages are disabled, the interpreter should suppress its
   // failures and warning messages from stdout. Return the previous state.
   if (enable) {
      G__const_resetnoerror();
   }
   else {
      G__const_setnoerror();
   }
   return !G__const_whatnoerror();
}

//______________________________________________________________________________
const char* TCintWithCling::GetIncludePath()
{
   // Refresh the list of include paths known to the interpreter and return it
   // with -I prepended.
   R__LOCKGUARD(gCINTMutex);
   fIncludePath = "";
   G__IncludePathInfo path;
   while (path.Next()) {
      const char* pathname = path.Name();
      fIncludePath.Append(" -I\"").Append(pathname).Append("\" ");
   }
   return fIncludePath;
}

//______________________________________________________________________________
const char* TCintWithCling::GetSTLIncludePath() const
{
   // Return the directory containing CINT's stl cintdlls.
   static TString stldir;
   if (!stldir.Length()) {
#ifdef CINTINCDIR
      stldir = CINTINCDIR;
#else
      stldir = gRootDir;
      stldir += "/cint";
#endif
      if (!stldir.EndsWith("/")) {
         stldir += '/';
      }
      stldir += "cint/stl";
   }
   return stldir;
}

//______________________________________________________________________________
//                      M I S C
//______________________________________________________________________________

int TCintWithCling::DisplayClass(FILE* fout, char* name, int base, int start) const
{
   // Interface to CINT function
   return G__display_class(fout, name, base, start);
}

//______________________________________________________________________________
int TCintWithCling::DisplayIncludePath(FILE* fout) const
{
   // Interface to CINT function
   return G__display_includepath(fout);
}

//______________________________________________________________________________
void* TCintWithCling::FindSym(const char* entry) const
{
   // Interface to CINT function
   return G__findsym(entry);
}

//______________________________________________________________________________
void TCintWithCling::GenericError(const char* error) const
{
   // Interface to CINT function
   G__genericerror(error);
}

//______________________________________________________________________________
Long_t TCintWithCling::GetExecByteCode() const
{
   // Interface to CINT function
   return (Long_t)G__exec_bytecode;
}

//______________________________________________________________________________
Long_t TCintWithCling::Getgvp() const
{
   // Interface to CINT function
   return (Long_t)G__getgvp();
}

//______________________________________________________________________________
const char* TCintWithCling::Getp2f2funcname(void* receiver) const
{
   // Interface to CINT function
   return G__p2f2funcname(receiver);
}

//______________________________________________________________________________
int TCintWithCling::GetSecurityError() const
{
   // Interface to CINT function
   return G__get_security_error();
}

//______________________________________________________________________________
int TCintWithCling::LoadFile(const char* path) const
{
   // Interface to CINT function
   return G__loadfile(path);
}

//______________________________________________________________________________
void TCintWithCling::LoadText(const char* text) const
{
   // Interface to CINT function
   G__load_text(text);
}

//______________________________________________________________________________
const char* TCintWithCling::MapCppName(const char* name) const
{
   // Interface to CINT function
   return G__map_cpp_name(name);
}

//______________________________________________________________________________
void TCintWithCling::SetAlloclockfunc(void (*p)()) const
{
   // Interface to CINT function
   G__set_alloclockfunc(p);
}

//______________________________________________________________________________
void TCintWithCling::SetAllocunlockfunc(void (*p)()) const
{
   // Interface to CINT function
   G__set_allocunlockfunc(p);
}

//______________________________________________________________________________
int TCintWithCling::SetClassAutoloading(int autoload) const
{
   // Interface to CINT function
   return G__set_class_autoloading(autoload);
}

//______________________________________________________________________________
void TCintWithCling::SetErrmsgcallback(void* p) const
{
   // Interface to CINT function
   G__set_errmsgcallback(p);
}

//______________________________________________________________________________
void TCintWithCling::Setgvp(Long_t gvp) const
{
   // Interface to CINT function
   G__setgvp(gvp);
}

//______________________________________________________________________________
void TCintWithCling::SetRTLD_NOW() const
{
   // Interface to CINT function
   G__Set_RTLD_NOW();
}

//______________________________________________________________________________
void TCintWithCling::SetRTLD_LAZY() const
{
   // Interface to CINT function
   G__Set_RTLD_LAZY();
}

//______________________________________________________________________________
void TCintWithCling::SetTempLevel(int val) const
{
   // Interface to CINT function
   G__settemplevel(val);
}

//______________________________________________________________________________
int TCintWithCling::UnloadFile(const char* path) const
{
   // Interface to CINT function
   return G__unloadfile(path);
}



//______________________________________________________________________________
//
//  G__CallFunc interface
//

//______________________________________________________________________________
void TCintWithCling::CallFunc_Delete(CallFunc_t* func) const
{
   delete (TClingCallFunc*) func;
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_Exec(CallFunc_t* func, void* address) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->Exec(address);
}

//______________________________________________________________________________
Long_t TCintWithCling::CallFunc_ExecInt(CallFunc_t* func, void* address) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   return f->ExecInt(address);
}

//______________________________________________________________________________
Long_t TCintWithCling::CallFunc_ExecInt64(CallFunc_t* func, void* address) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   return f->ExecInt64(address);
}

//______________________________________________________________________________
Double_t TCintWithCling::CallFunc_ExecDouble(CallFunc_t* func, void* address) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   return f->ExecDouble(address);
}

//______________________________________________________________________________
CallFunc_t* TCintWithCling::CallFunc_Factory() const
{
   return (CallFunc_t*) new TClingCallFunc(fInterpreter);
}

//______________________________________________________________________________
CallFunc_t* TCintWithCling::CallFunc_FactoryCopy(CallFunc_t* func) const
{
   return (CallFunc_t*) new TClingCallFunc(*(TClingCallFunc*)func);
}

//______________________________________________________________________________
MethodInfo_t* TCintWithCling::CallFunc_FactoryMethod(CallFunc_t* func) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   return (MethodInfo_t*) f->FactoryMethod();
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_Init(CallFunc_t* func) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->Init();
}

//______________________________________________________________________________
bool TCintWithCling::CallFunc_IsValid(CallFunc_t* func) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   return f->IsValid();
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_ResetArg(CallFunc_t* func) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->ResetArg();
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetArg(CallFunc_t* func, Long_t param) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->SetArg(param);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetArg(CallFunc_t* func, Double_t param) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->SetArg(param);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetArg(CallFunc_t* func, Long64_t param) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->SetArg(param);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetArg(CallFunc_t* func, ULong64_t param) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->SetArg(param);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetArgArray(CallFunc_t* func, Long_t* paramArr, Int_t nparam) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->SetArgArray(paramArr, nparam);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetArgs(CallFunc_t* func, const char* param) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   f->SetArgs(param);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetFunc(CallFunc_t* func, ClassInfo_t* info, const char* method, const char* params, Long_t* offset) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   TClingClassInfo* ci = (TClingClassInfo*) info;
   f->SetFunc(ci, method, params, offset);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetFunc(CallFunc_t* func, MethodInfo_t* info) const
{
   TClingCallFunc* f = (TClingCallFunc*) func;
   TClingMethodInfo* minfo = (TClingMethodInfo*) info;
   f->SetFunc(minfo);
}

//______________________________________________________________________________
void TCintWithCling::CallFunc_SetFuncProto(CallFunc_t* func, ClassInfo_t* info, const char* method, const char* proto, Long_t* offset) const
{
   // Interface to CINT function
   TClingCallFunc* f = (TClingCallFunc*) func;
   TClingClassInfo* ci = (TClingClassInfo*) info;
   f->SetFuncProto(ci, method, proto, offset);
}


//______________________________________________________________________________
//
//  G__ClassInfo interface
//

Long_t TCintWithCling::ClassInfo_ClassProperty(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->ClassProperty();
}

//______________________________________________________________________________
void TCintWithCling::ClassInfo_Delete(ClassInfo_t* cinfo) const
{
   delete (TClingClassInfo*) cinfo;
}

//______________________________________________________________________________
void TCintWithCling::ClassInfo_Delete(ClassInfo_t* cinfo, void* arena) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   TClinginfo->Delete(arena);
}

//______________________________________________________________________________
void TCintWithCling::ClassInfo_DeleteArray(ClassInfo_t* cinfo, void* arena, bool dtorOnly) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   TClinginfo->DeleteArray(arena, dtorOnly);
}

//______________________________________________________________________________
void TCintWithCling::ClassInfo_Destruct(ClassInfo_t* cinfo, void* arena) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   TClinginfo->Destruct(arena);
}

//______________________________________________________________________________
ClassInfo_t* TCintWithCling::ClassInfo_Factory() const
{
   return (ClassInfo_t*) new TClingClassInfo(fInterpreter);
}

//______________________________________________________________________________
ClassInfo_t* TCintWithCling::ClassInfo_Factory(ClassInfo_t* cinfo) const
{
   return (ClassInfo_t*) new TClingClassInfo(*(TClingClassInfo*)cinfo);
}

//______________________________________________________________________________
ClassInfo_t* TCintWithCling::ClassInfo_Factory(const char* name) const
{
   return (ClassInfo_t*) new TClingClassInfo(fInterpreter, name);
}

//______________________________________________________________________________
int TCintWithCling::ClassInfo_GetMethodNArg(ClassInfo_t* cinfo, const char* method, const char* proto) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->GetMethodNArg(method, proto);
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_HasDefaultConstructor(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->HasDefaultConstructor();
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_HasMethod(ClassInfo_t* cinfo, const char* name) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->HasMethod(name);
}

//______________________________________________________________________________
void TCintWithCling::ClassInfo_Init(ClassInfo_t* cinfo, const char* name) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   TClinginfo->Init(name);
}

//______________________________________________________________________________
void TCintWithCling::ClassInfo_Init(ClassInfo_t* cinfo, int tagnum) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   TClinginfo->Init(tagnum);
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_IsBase(ClassInfo_t* cinfo, const char* name) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->IsBase(name);
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_IsEnum(const char* name) const
{
   return TClingClassInfo::IsEnum(fInterpreter, name);
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_IsLoaded(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->IsLoaded();
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_IsValid(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->IsValid();
}

//______________________________________________________________________________
bool TCintWithCling::ClassInfo_IsValidMethod(ClassInfo_t* cinfo, const char* method, const char* proto, Long_t* offset) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->IsValidMethod(method, proto, offset);
}

//______________________________________________________________________________
int TCintWithCling::ClassInfo_Next(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->Next();
}

//______________________________________________________________________________
void* TCintWithCling::ClassInfo_New(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->New();
}

//______________________________________________________________________________
void* TCintWithCling::ClassInfo_New(ClassInfo_t* cinfo, int n) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->New(n);
}

//______________________________________________________________________________
void* TCintWithCling::ClassInfo_New(ClassInfo_t* cinfo, int n, void* arena) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->New(n, arena);
}

//______________________________________________________________________________
void* TCintWithCling::ClassInfo_New(ClassInfo_t* cinfo, void* arena) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->New(arena);
}

//______________________________________________________________________________
Long_t TCintWithCling::ClassInfo_Property(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->Property();
}

//______________________________________________________________________________
int TCintWithCling::ClassInfo_RootFlag(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->RootFlag();
}

//______________________________________________________________________________
int TCintWithCling::ClassInfo_Size(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->Size();
}

//______________________________________________________________________________
Long_t TCintWithCling::ClassInfo_Tagnum(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->Tagnum();
}

//______________________________________________________________________________
const char* TCintWithCling::ClassInfo_FileName(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->FileName();
}

//______________________________________________________________________________
const char* TCintWithCling::ClassInfo_FullName(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->FullName();
}

//______________________________________________________________________________
const char* TCintWithCling::ClassInfo_Name(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->Name();
}

//______________________________________________________________________________
const char* TCintWithCling::ClassInfo_Title(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->Title();
}

//______________________________________________________________________________
const char* TCintWithCling::ClassInfo_TmpltName(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return TClinginfo->TmpltName();
}



//______________________________________________________________________________
//
//  G__BaseClassInfo interface
//

//______________________________________________________________________________
void TCintWithCling::BaseClassInfo_Delete(BaseClassInfo_t* bcinfo) const
{
   delete(TClingBaseClassInfo*) bcinfo;
}

//______________________________________________________________________________
BaseClassInfo_t* TCintWithCling::BaseClassInfo_Factory(ClassInfo_t* cinfo) const
{
   TClingClassInfo* TClinginfo = (TClingClassInfo*) cinfo;
   return (BaseClassInfo_t*) new TClingBaseClassInfo(fInterpreter, TClinginfo);
}

//______________________________________________________________________________
int TCintWithCling::BaseClassInfo_Next(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->Next();
}

//______________________________________________________________________________
int TCintWithCling::BaseClassInfo_Next(BaseClassInfo_t* bcinfo, int onlyDirect) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->Next(onlyDirect);
}

//______________________________________________________________________________
Long_t TCintWithCling::BaseClassInfo_Offset(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->Offset();
}

//______________________________________________________________________________
Long_t TCintWithCling::BaseClassInfo_Property(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->Property();
}

//______________________________________________________________________________
Long_t TCintWithCling::BaseClassInfo_Tagnum(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->Tagnum();
}

//______________________________________________________________________________
const char* TCintWithCling::BaseClassInfo_FullName(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->FullName();
}

//______________________________________________________________________________
const char* TCintWithCling::BaseClassInfo_Name(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->Name();
}

//______________________________________________________________________________
const char* TCintWithCling::BaseClassInfo_TmpltName(BaseClassInfo_t* bcinfo) const
{
   TClingBaseClassInfo* TClinginfo = (TClingBaseClassInfo*) bcinfo;
   return TClinginfo->TmpltName();
}

//______________________________________________________________________________
//
//  G__DataMemberInfo interface
//

//______________________________________________________________________________
int TCintWithCling::DataMemberInfo_ArrayDim(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->ArrayDim();
}

//______________________________________________________________________________
void TCintWithCling::DataMemberInfo_Delete(DataMemberInfo_t* dminfo) const
{
   delete(TClingDataMemberInfo*) dminfo;
}

//______________________________________________________________________________
DataMemberInfo_t* TCintWithCling::DataMemberInfo_Factory(ClassInfo_t* clinfo /*= 0*/) const
{
   TClingClassInfo* TClingclass_info = (TClingClassInfo*) clinfo;
   return (DataMemberInfo_t*) new TClingDataMemberInfo(fInterpreter, TClingclass_info);
}

//______________________________________________________________________________
DataMemberInfo_t* TCintWithCling::DataMemberInfo_FactoryCopy(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return (DataMemberInfo_t*) new TClingDataMemberInfo(*TClinginfo);
}

//______________________________________________________________________________
bool TCintWithCling::DataMemberInfo_IsValid(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->IsValid();
}

//______________________________________________________________________________
int TCintWithCling::DataMemberInfo_MaxIndex(DataMemberInfo_t* dminfo, Int_t dim) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->MaxIndex(dim);
}

//______________________________________________________________________________
int TCintWithCling::DataMemberInfo_Next(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->Next();
}

//______________________________________________________________________________
Long_t TCintWithCling::DataMemberInfo_Offset(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->Offset();
}

//______________________________________________________________________________
Long_t TCintWithCling::DataMemberInfo_Property(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->Property();
}

//______________________________________________________________________________
Long_t TCintWithCling::DataMemberInfo_TypeProperty(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->TypeProperty();
}

//______________________________________________________________________________
int TCintWithCling::DataMemberInfo_TypeSize(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->TypeSize();
}

//______________________________________________________________________________
const char* TCintWithCling::DataMemberInfo_TypeName(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->TypeName();
}

//______________________________________________________________________________
const char* TCintWithCling::DataMemberInfo_TypeTrueName(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->TypeTrueName();
}

//______________________________________________________________________________
const char* TCintWithCling::DataMemberInfo_Name(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->Name();
}

//______________________________________________________________________________
const char* TCintWithCling::DataMemberInfo_Title(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->Title();
}

//______________________________________________________________________________
const char* TCintWithCling::DataMemberInfo_ValidArrayIndex(DataMemberInfo_t* dminfo) const
{
   TClingDataMemberInfo* TClinginfo = (TClingDataMemberInfo*) dminfo;
   return TClinginfo->ValidArrayIndex();
}



//______________________________________________________________________________
//
//  G__MethodInfo interface
//

//______________________________________________________________________________
void TCintWithCling::MethodInfo_Delete(MethodInfo_t* minfo) const
{
   // Interface to CINT function
   delete(TClingMethodInfo*) minfo;
}

//______________________________________________________________________________
void TCintWithCling::MethodInfo_CreateSignature(MethodInfo_t* minfo, TString& signature) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   info->CreateSignature(signature);
}

//______________________________________________________________________________
MethodInfo_t* TCintWithCling::MethodInfo_Factory() const
{
   return (MethodInfo_t*) new TClingMethodInfo(fInterpreter);
}

//______________________________________________________________________________
MethodInfo_t* TCintWithCling::MethodInfo_FactoryCopy(MethodInfo_t* minfo) const
{
   return (MethodInfo_t*) new TClingMethodInfo(*(TClingMethodInfo*)minfo);
}

//______________________________________________________________________________
void* TCintWithCling::MethodInfo_InterfaceMethod(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return (void*) info->InterfaceMethod();
}

//______________________________________________________________________________
bool TCintWithCling::MethodInfo_IsValid(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->IsValid();
}

//______________________________________________________________________________
int TCintWithCling::MethodInfo_NArg(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->NArg();
}

//______________________________________________________________________________
int TCintWithCling::MethodInfo_NDefaultArg(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->NDefaultArg();
}

//______________________________________________________________________________
int TCintWithCling::MethodInfo_Next(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->Next();
}

//______________________________________________________________________________
Long_t TCintWithCling::MethodInfo_Property(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->Property();
}

//______________________________________________________________________________
void* TCintWithCling::MethodInfo_Type(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->Type();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodInfo_GetMangledName(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->GetMangledName();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodInfo_GetPrototype(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->GetPrototype();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodInfo_Name(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->Name();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodInfo_TypeName(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->TypeName();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodInfo_Title(MethodInfo_t* minfo) const
{
   TClingMethodInfo* info = (TClingMethodInfo*) minfo;
   return info->Title();
}

//______________________________________________________________________________
//
//  G__MethodArgInfo interface
//

//______________________________________________________________________________
void TCintWithCling::MethodArgInfo_Delete(MethodArgInfo_t* marginfo) const
{
   delete(TClingMethodArgInfo*) marginfo;
}

//______________________________________________________________________________
MethodArgInfo_t* TCintWithCling::MethodArgInfo_Factory() const
{
   return (MethodArgInfo_t*) new TClingMethodArgInfo(fInterpreter);
}

//______________________________________________________________________________
MethodArgInfo_t* TCintWithCling::MethodArgInfo_FactoryCopy(MethodArgInfo_t* marginfo) const
{
   return (MethodArgInfo_t*)
          new TClingMethodArgInfo(*(TClingMethodArgInfo*)marginfo);
}

//______________________________________________________________________________
bool TCintWithCling::MethodArgInfo_IsValid(MethodArgInfo_t* marginfo) const
{
   TClingMethodArgInfo* info = (TClingMethodArgInfo*) marginfo;
   return info->IsValid();
}

//______________________________________________________________________________
int TCintWithCling::MethodArgInfo_Next(MethodArgInfo_t* marginfo) const
{
   TClingMethodArgInfo* info = (TClingMethodArgInfo*) marginfo;
   return info->Next();
}

//______________________________________________________________________________
Long_t TCintWithCling::MethodArgInfo_Property(MethodArgInfo_t* marginfo) const
{
   TClingMethodArgInfo* info = (TClingMethodArgInfo*) marginfo;
   return info->Property();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodArgInfo_DefaultValue(MethodArgInfo_t* marginfo) const
{
   TClingMethodArgInfo* info = (TClingMethodArgInfo*) marginfo;
   return info->DefaultValue();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodArgInfo_Name(MethodArgInfo_t* marginfo) const
{
   TClingMethodArgInfo* info = (TClingMethodArgInfo*) marginfo;
   return info->Name();
}

//______________________________________________________________________________
const char* TCintWithCling::MethodArgInfo_TypeName(MethodArgInfo_t* marginfo) const
{
   TClingMethodArgInfo* info = (TClingMethodArgInfo*) marginfo;
   return info->TypeName();
}


//______________________________________________________________________________
//
//  G__TypeInfo interface
//

//______________________________________________________________________________
void TCintWithCling::TypeInfo_Delete(TypeInfo_t* tinfo) const
{
   delete (TClingTypeInfo*) tinfo;
}

//______________________________________________________________________________
TypeInfo_t* TCintWithCling::TypeInfo_Factory() const
{
   return (TypeInfo_t*) new TClingTypeInfo(fInterpreter);
}

//______________________________________________________________________________
TypeInfo_t* TCintWithCling::TypeInfo_FactoryCopy(TypeInfo_t* tinfo) const
{
   return (TypeInfo_t*) new TClingTypeInfo(*(TClingTypeInfo*)tinfo);
}

//______________________________________________________________________________
void TCintWithCling::TypeInfo_Init(TypeInfo_t* tinfo, const char* name) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   TClinginfo->Init(name);
}

//______________________________________________________________________________
bool TCintWithCling::TypeInfo_IsValid(TypeInfo_t* tinfo) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   return TClinginfo->IsValid();
}

//______________________________________________________________________________
const char* TCintWithCling::TypeInfo_Name(TypeInfo_t* tinfo) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   return TClinginfo->Name();
}

//______________________________________________________________________________
Long_t TCintWithCling::TypeInfo_Property(TypeInfo_t* tinfo) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   return TClinginfo->Property();
}

//______________________________________________________________________________
int TCintWithCling::TypeInfo_RefType(TypeInfo_t* tinfo) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   return TClinginfo->RefType();
}

//______________________________________________________________________________
int TCintWithCling::TypeInfo_Size(TypeInfo_t* tinfo) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   return TClinginfo->Size();
}

//______________________________________________________________________________
const char* TCintWithCling::TypeInfo_TrueName(TypeInfo_t* tinfo) const
{
   TClingTypeInfo* TClinginfo = (TClingTypeInfo*) tinfo;
   return TClinginfo->TrueName();
}


//______________________________________________________________________________
//
//  G__TypedefInfo interface
//

//______________________________________________________________________________
void TCintWithCling::TypedefInfo_Delete(TypedefInfo_t* tinfo) const
{
   delete(TClingTypedefInfo*) tinfo;
}

//______________________________________________________________________________
TypedefInfo_t* TCintWithCling::TypedefInfo_Factory() const
{
   return (TypedefInfo_t*) new TClingTypedefInfo(fInterpreter);
}

//______________________________________________________________________________
TypedefInfo_t* TCintWithCling::TypedefInfo_FactoryCopy(TypedefInfo_t* tinfo) const
{
   return (TypedefInfo_t*) new TClingTypedefInfo(*(TClingTypedefInfo*)tinfo);
}

//______________________________________________________________________________
TypedefInfo_t TCintWithCling::TypedefInfo_Init(TypedefInfo_t* tinfo,
                                      const char* name) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   TClinginfo->Init(name);
}

//______________________________________________________________________________
bool TCintWithCling::TypedefInfo_IsValid(TypedefInfo_t* tinfo) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   return TClinginfo->IsValid();
}

//______________________________________________________________________________
Long_t TCintWithCling::TypedefInfo_Property(TypedefInfo_t* tinfo) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   return TClinginfo->Property();
}

//______________________________________________________________________________
int TCintWithCling::TypedefInfo_Size(TypedefInfo_t* tinfo) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   return TClinginfo->Size();
}

//______________________________________________________________________________
const char* TCintWithCling::TypedefInfo_TrueName(TypedefInfo_t* tinfo) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   return TClinginfo->TrueName();
}

//______________________________________________________________________________
const char* TCintWithCling::TypedefInfo_Name(TypedefInfo_t* tinfo) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   return TClinginfo->Name();
}

//______________________________________________________________________________
const char* TCintWithCling::TypedefInfo_Title(TypedefInfo_t* tinfo) const
{
   TClingTypedefInfo* TClinginfo = (TClingTypedefInfo*) tinfo;
   return TClinginfo->Title();
}

