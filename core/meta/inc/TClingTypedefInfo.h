// @(#)root/core/meta:$Id$
// Author: Paul Russo   30/07/2012

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TClingTypedefInfo
#define ROOT_TClingTypedefInfo

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TClingTypedefInfo                                                    //
//                                                                      //
// Emulation of the CINT TypedefInfo class.                             //
//                                                                      //
// The CINT C++ interpreter provides an interface to metadata about     //
// a typedef through the TypedefInfo class.  This class provides the    //
// same functionality, using an interface as close as possible to       //
// TypedefInfo but the typedef metadata comes from the Clang C++        //
// compiler, not CINT.                                                  //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "cling/Interpreter/Interpreter.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/Frontend/CompilerInstance.h"
#include <vector>

class TClingTypedefInfo {

private:

   cling::Interpreter  *fInterp; // Cling interpreter, we do *not* own.
   bool                 fFirstTime; // We need to skip the first increment to support the cint Next() semantics.
   bool                 fDescend; // Flag for signaling the need to descend on this advancement.
   clang::DeclContext::decl_iterator fIter; // Current decl in scope.
   const clang::Decl   *fDecl; // Current decl.
   std::vector<clang::DeclContext::decl_iterator> fIterStack; // Recursion stack for traversing nested scopes.

public:

   explicit TClingTypedefInfo(cling::Interpreter *interp)
         : fInterp(interp), fFirstTime(true), fDescend(false), fDecl(0)
   {
      const clang::TranslationUnitDecl *TU = fInterp->getCI()->getASTContext().getTranslationUnitDecl();
      const clang::DeclContext *DC = llvm::cast<clang::DeclContext>(TU);
      fIter = DC->decls_begin();
   }

   explicit TClingTypedefInfo(cling::Interpreter *, const char *);

   const clang::Decl   *GetDecl() const;
   void                 Init(const char *name);
   bool                 IsValid() const;
   int                  AdvanceToDecl(const clang::Decl *);
   int                  InternalNext();
   int                  Next();
   long                 Property() const;
   int                  Size() const;
   const char          *TrueName() const;
   const char          *Name() const;
   const char          *Title() const;

};

#endif // ROOT_TClingTypedefInfo
