// @(#)root/core/meta:$Id$
// Author: Paul Russo   30/07/2012

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TClingTypeInfo                                                       //
//                                                                      //
// Emulation of the CINT TypeInfo class.                                //
//                                                                      //
// The CINT C++ interpreter provides an interface to metadata about     //
// a type through the TypeInfo class.  This class provides the same     //
// functionality, using an interface as close as possible to TypeInfo   //
// but the type metadata comes from the Clang C++ compiler, not CINT.   //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TClingTypeInfo.h"

#include "Property.h" // for G__BIT_*, after CINT is gone,
                      // replace with the following 
//#include "TClingProperty.h" // After CINT is gone, instead of Property.h
#include "Rtypes.h" // for gDebug
#include "cling/Interpreter/Interpreter.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Type.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Frontend/CompilerInstance.h"
#include <cstdio>
#include <string>

using namespace std;

//______________________________________________________________________________
tcling_TypeInfo::tcling_TypeInfo(cling::Interpreter *interp, const char *name)
   : fInterp(interp)
{
   Init(name);
}

//______________________________________________________________________________
void tcling_TypeInfo::Init(const char *name)
{
   fQualType = clang::QualType();
   if (gDebug > 0) {
      fprintf(stderr,
              "tcling_TypeInfo::Init(name): looking up clang type: %s", name);
   }
   clang::QualType QT = fInterp->lookupType(name);
   if (QT.isNull()) {
      if (gDebug > 0) {
         fprintf(stderr,
                 "tcling_TypeInfo::Init(name): clang type not found: %s", name);
      }
      std::string buf = TClassEdit::InsertStd(name);
      QT = fInterp->lookupType(buf);
      if (QT.isNull()) {
         if (gDebug > 0) {
            fprintf(stderr,
                    "tcling_TypeInfo::Init(name):  "
                    "clang type not found name: %s\n", buf.c_str());
         }
      }
      else {
         fQualType = QT;
         if (gDebug > 0) {
            fprintf(stderr,
                    "tcling_TypeInfo::Init(name): found clang type name: %s\n",
                    buf.c_str());
         }
      }
   }
   else {
      fQualType = QT;
      if (gDebug > 0) {
         fprintf(stderr,
                 "tcling_TypeInfo::Init(name): clang type found: %s\n", name);
      }
   }
}

//______________________________________________________________________________
bool tcling_TypeInfo::IsValid() const
{
   return !fQualType.isNull();
}

//______________________________________________________________________________
const char *tcling_TypeInfo::Name() const
{
   if (!IsValid()) {
      return 0;
   }
   // Note: This *must* be static because we are returning a pointer inside it!
   static std::string buf;
   buf.clear();
   clang::PrintingPolicy Policy(fInterp->getCI()->getASTContext().
                                getPrintingPolicy());
   fQualType.getAsStringInternal(buf, Policy);
   return buf.c_str();
}

//______________________________________________________________________________
long tcling_TypeInfo::Property() const
{
   if (!IsValid()) {
      return 0L;
   }
   long property = 0L;
   if (llvm::isa<clang::TypedefType>(*fQualType)) {
      property |= G__BIT_ISTYPEDEF;
   }
   clang::QualType QT = fQualType.getCanonicalType();
   if (QT.isConstQualified()) {
      property |= G__BIT_ISCONSTANT;
   }
   while (1) {
      if (QT->isArrayType()) {
         QT = llvm::cast<clang::ArrayType>(QT)->getElementType();
         continue;
      }
      else if (QT->isReferenceType()) {
         property |= G__BIT_ISREFERENCE;
         QT = llvm::cast<clang::ReferenceType>(QT)->getPointeeType();
         continue;
      }
      else if (QT->isPointerType()) {
         property |= G__BIT_ISPOINTER;
         if (QT.isConstQualified()) {
            property |= G__BIT_ISPCONSTANT;
         }
         QT = llvm::cast<clang::PointerType>(QT)->getPointeeType();
         continue;
      }
      else if (QT->isMemberPointerType()) {
         QT = llvm::cast<clang::MemberPointerType>(QT)->getPointeeType();
         continue;
      }
      break;
   }
   if (QT->isBuiltinType()) {
      property |= G__BIT_ISFUNDAMENTAL;
   }
   if (QT.isConstQualified()) {
      property |= G__BIT_ISCONSTANT;
   }
   return property;
}

//______________________________________________________________________________
int tcling_TypeInfo::RefType() const
{
   if (!IsValid()) {
      return 0;
   }
   int cnt = 0;
   bool is_ref = false;
   clang::QualType QT = fQualType.getCanonicalType();
   while (1) {
      if (QT->isArrayType()) {
         QT = llvm::cast<clang::ArrayType>(QT)->getElementType();
         continue;
      }
      else if (QT->isReferenceType()) {
         is_ref = true;
         QT = llvm::cast<clang::ReferenceType>(QT)->getPointeeType();
         continue;
      }
      else if (QT->isPointerType()) {
         ++cnt;
         QT = llvm::cast<clang::PointerType>(QT)->getPointeeType();
         continue;
      }
      else if (QT->isMemberPointerType()) {
         QT = llvm::cast<clang::MemberPointerType>(QT)->getPointeeType();
         continue;
      }
      break;
   }
   int val = 0;
   if (cnt > 1) {
      val = cnt;
   }
   if (is_ref) {
      if (cnt < 2) {
         val = G__PARAREFERENCE;
      }
      else {
         val |= G__PARAREF;
      }
   }
   return val;
}

//______________________________________________________________________________
int tcling_TypeInfo::Size() const
{
   if (!IsValid()) {
      return 1;
   }
   if (fQualType->isDependentType()) {
      // Dependent on a template parameter, we do not know what it is yet.
      return 0;
   }
   if (const clang::RecordType *RT = fQualType->getAs<clang::RecordType>()) {
      if (!RT->getDecl()->getDefinition()) {
         // This is a forward-declared class.
         return 0;
      }
   }
   clang::ASTContext &Context = fInterp->getCI()->getASTContext();
   // Note: This is an int64_t.
   clang::CharUnits::QuantityType Quantity =
      Context.getTypeSizeInChars(fQualType).getQuantity();
   return static_cast<int>(Quantity);
}

//______________________________________________________________________________
const char *tcling_TypeInfo::StemName() const
{
   if (!IsValid()) {
      return 0;
   }
   clang::QualType QT = fQualType.getCanonicalType();
   while (1) {
      if (QT->isArrayType()) {
         QT = llvm::cast<clang::ArrayType>(QT)->getElementType();
         continue;
      }
      else if (QT->isReferenceType()) {
         QT = llvm::cast<clang::ReferenceType>(QT)->getPointeeType();
         continue;
      }
      else if (QT->isPointerType()) {
         QT = llvm::cast<clang::PointerType>(QT)->getPointeeType();
         continue;
      }
      else if (QT->isMemberPointerType()) {
         QT = llvm::cast<clang::MemberPointerType>(QT)->getPointeeType();
         continue;
      }
      break;
   }
   // Note: This *must* be static because we are returning a pointer inside it.
   static std::string buf;
   buf.clear();
   clang::PrintingPolicy Policy(fInterp->getCI()->getASTContext().
                                getPrintingPolicy());
   QT.getAsStringInternal(buf, Policy);
   return buf.c_str();
}

//______________________________________________________________________________
const char *tcling_TypeInfo::TrueName() const
{
   if (!IsValid()) {
      return 0;
   }
   // Note: This *must* be static because we are returning a pointer inside it.
   static std::string buf;
   buf.clear();
   clang::PrintingPolicy Policy(fInterp->getCI()->getASTContext().
                                getPrintingPolicy());
   fQualType.getCanonicalType().getAsStringInternal(buf, Policy);
   return buf.c_str();
}

