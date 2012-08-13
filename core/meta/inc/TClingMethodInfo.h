// @(#)root/core/meta:$Id$
// Author: Paul Russo   30/07/2012

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TClingMethodInfo
#define ROOT_TClingMethodInfo

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TClingMethodInfo                                                     //
//                                                                      //
// Emulation of the CINT MethodInfo class.                              //
//                                                                      //
// The CINT C++ interpreter provides an interface to metadata about     //
// a function through the MethodInfo class.  This class provides the    //
// same functionality, using an interface as close as possible to       //
// MethodInfo but the typedef metadata comes from the Clang C++         //
// compiler, not CINT.                                                  //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TString.h"

#include "clang/AST/DeclBase.h"
#include "llvm/ADT/SmallVector.h"

namespace cling {
class Interpreter;
}

namespace clang {
class FunctionDecl;
}

class TClingClassInfo;
class TClingTypeInfo;

class TClingMethodInfo {
private:
   cling::Interpreter                          *fInterp; // Cling interpreter, we do *not* own.
   llvm::SmallVector<clang::DeclContext *, 2>   fContexts; // Set of DeclContext that we will iterate over.
   bool                                         fFirstTime; // Flag for first time incrementing iterator, cint semantics are weird.
   unsigned int                                 fContextIdx; // Index in fContexts of DeclContext we are iterating over.
   clang::DeclContext::decl_iterator            fIter; // Our iterator.
public:
   explicit TClingMethodInfo(cling::Interpreter *interp) : fInterp(interp), fFirstTime(true), fContextIdx(0U) {}

   TClingMethodInfo(cling::Interpreter *, TClingClassInfo *);

   const clang::FunctionDecl                   *GetMethodDecl() const;
   void                                         CreateSignature(TString &signature) const;
   void                                         Init(const clang::FunctionDecl *);
   void                                        *InterfaceMethod() const;
   bool                                         IsValid() const;
   int                                          NArg() const;
   int                                          NDefaultArg() const;
   int                                          InternalNext();
   int                                          Next();
   long                                         Property() const;
   TClingTypeInfo                              *Type() const;
   const char                                  *GetMangledName() const;
   const char                                  *GetPrototype() const;
   const char                                  *Name() const;
   const char                                  *TypeName() const;
   const char                                  *Title() const;
};

#endif // ROOT_TClingMethodInfo
