// @(#)root/core/meta:$Id$
// Author: Paul Russo   30/07/2012

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TClingDataMemberInfo
#define ROOT_TClingDataMemberInfo

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TClingDataMemberInfo                                                 //
//                                                                      //
// Emulation of the CINT DataMemberInfo class.                          //
//                                                                      //
// The CINT C++ interpreter provides an interface to metadata about     //
// the data members of a class through the DataMemberInfo class.  This  //
// class provides the same functionality, using an interface as close   //
// as possible to DataMemberInfo but the data member metadata comes     //
// from the Clang C++ compiler, not CINT.                               //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TClingClassInfo.h"

#include "cling/Interpreter/Interpreter.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Frontend/CompilerInstance.h"

#include <vector>

namespace clang {
class Decl;
}

class TClingClassInfo;

class TClingDataMemberInfo {

private:

   cling::Interpreter    *fInterp; // Cling interpreter, we do *not* own.
   TClingClassInfo       *fClassInfo; // Class we are iterating over, we own.
   bool                   fFirstTime; // We need to skip the first increment to support the cint Next() semantics.
   clang::DeclContext::decl_iterator fIter; // Current decl.
   std::vector<clang::DeclContext::decl_iterator> fIterStack; // Recursion stack for traversing nested transparent scopes.

public:

   ~TClingDataMemberInfo() { delete fClassInfo; }

   explicit TClingDataMemberInfo(cling::Interpreter *interp)
         : fInterp(interp), fClassInfo(0), fFirstTime(true)
   {
      fClassInfo = new TClingClassInfo(fInterp);
      fIter = fInterp->getCI()->getASTContext().getTranslationUnitDecl()->decls_begin();
      // Move to first global variable.
      InternalNext();
   }

   TClingDataMemberInfo(cling::Interpreter *, TClingClassInfo *);

   TClingDataMemberInfo(const TClingDataMemberInfo &rhs)
   {
      fInterp = rhs.fInterp;
      fClassInfo = new TClingClassInfo(*rhs.fClassInfo);
      fFirstTime = rhs.fFirstTime;
      fIter = rhs.fIter;
      fIterStack = rhs.fIterStack;
   }

   TClingDataMemberInfo &operator=(const TClingDataMemberInfo &rhs)
   {
      if (this != &rhs) {
         fInterp = rhs.fInterp;
         delete fClassInfo;
         fClassInfo = new TClingClassInfo(*rhs.fClassInfo);
         fFirstTime = rhs.fFirstTime;
         fIter = rhs.fIter;
         fIterStack = rhs.fIterStack;
      }
      return *this;
   }


   int                ArrayDim() const;
   TClingClassInfo   *GetClassInfo() const { return fClassInfo; }
   clang::Decl       *GetDecl() const { return *fIter; }
   bool               IsValid() const { return *fIter; }
   int                MaxIndex(int dim) const;
   int                InternalNext();
   bool               Next() { return InternalNext(); }
   long               Offset() const;
   long               Property() const;
   long               TypeProperty() const;
   int                TypeSize() const;
   const char        *TypeName() const;
   const char        *TypeTrueName() const;
   const char        *Name() const;
   const char        *Title() const;
   const char        *ValidArrayIndex() const;

};

#endif // ROOT_TClingDataMemberInfo
