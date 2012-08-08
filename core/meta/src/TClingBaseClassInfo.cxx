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
// TClingBaseClassInfo                                                  //
//                                                                      //
// Emulation of the CINT BaseClassInfo class.                           //
//                                                                      //
// The CINT C++ interpreter provides an interface to metadata about     //
// the base classes of a class through the BaseClassInfo class.  This   //
// class provides the same functionality, using an interface as close   //
// as possible to BaseClassInfo but the base class metadata comes from  //
// the Clang C++ compiler, not CINT.                                    //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "TClingBaseClassInfo.h"

TClingBaseClassInfo::~TClingBaseClassInfo()
{
   delete fBaseClassInfo;
   fBaseClassInfo = 0;
   fInterp = 0;
   delete fClassInfo;
   fClassInfo = 0;
   //fFirstTime = true;
   //fDescend = false;
   fDecl = 0;
   fIter = 0;
   delete fBaseInfo;
   fBaseInfo = 0;
   //fIterStack.clear();
   //fOffset = 0L;
}

TClingBaseClassInfo::TClingBaseClassInfo(cling::Interpreter* interp,
      tcling_ClassInfo* tcling_class_info)
   : fBaseClassInfo(0), fInterp(interp), fClassInfo(0), fFirstTime(true),
     fDescend(false), fDecl(0), fIter(0), fBaseInfo(0), fOffset(0L)
{
   if (!tcling_class_info) {
      G__ClassInfo ci;
      fBaseClassInfo = new G__BaseClassInfo(ci);
      fClassInfo = new tcling_ClassInfo(interp);
      return;
   }
   fBaseClassInfo = new G__BaseClassInfo(*tcling_class_info->GetClassInfo());
   fClassInfo = new tcling_ClassInfo(*tcling_class_info);
   const clang::CXXRecordDecl* CRD =
      llvm::dyn_cast<clang::CXXRecordDecl>(fClassInfo->GetDecl());
   if (!CRD) {
      // We were initialized with something that is not a class.
      // FIXME: We should prevent this from happening!
      return;
   }
   fDecl = CRD;
   fIter = CRD->bases_begin();
}

TClingBaseClassInfo::TClingBaseClassInfo(const TClingBaseClassInfo& rhs)
   : fBaseClassInfo(0), fInterp(rhs.fInterp), fClassInfo(0),
     fFirstTime(rhs.fFirstTime), fDescend(rhs.fDescend), fDecl(rhs.fDecl),
     fIter(rhs.fIter), fBaseInfo(0), fIterStack(rhs.fIterStack),
     fOffset(rhs.fOffset)
{
   fBaseClassInfo = new G__BaseClassInfo(*rhs.fBaseClassInfo);
   fClassInfo = new tcling_ClassInfo(*rhs.fClassInfo);
   fBaseInfo = new tcling_ClassInfo(*rhs.fBaseInfo);
}

TClingBaseClassInfo& TClingBaseClassInfo::operator=(
   const TClingBaseClassInfo& rhs)
{
   if (this != &rhs) {
      delete fBaseClassInfo;
      fBaseClassInfo = new G__BaseClassInfo(*rhs.fBaseClassInfo);
      fInterp = rhs.fInterp;
      delete fClassInfo;
      fClassInfo = new tcling_ClassInfo(*rhs.fClassInfo);
      fFirstTime = rhs.fFirstTime;
      fDescend = rhs.fDescend;
      fDecl = rhs.fDecl;
      fIter = rhs.fIter;
      delete fBaseInfo;
      fBaseInfo = new tcling_ClassInfo(*rhs.fBaseInfo);
      fIterStack = rhs.fIterStack;
      fOffset = rhs.fOffset;
   }
   return *this;
}

bool TClingBaseClassInfo::IsValidCint() const
{
   if (gAllowCint) {
      return fBaseClassInfo->IsValid();
   }
   return false;
}

bool TClingBaseClassInfo::IsValidClang() const
{
   if (gAllowClang) {
      return
         // inited with a valid class, and
         fClassInfo->IsValidClang() &&
         // the base class we are iterating over is valid, and
         fDecl &&
         // our internal iterator is currently valid, and
         fIter &&
         (fIter != llvm::dyn_cast<clang::CXXRecordDecl>(fDecl)->bases_end()) &&
         // our current base has a tcling_ClassInfo, and
         fBaseInfo &&
         // our current base is a valid class
         fBaseInfo->IsValidClang();
   }
   return false;
}

bool TClingBaseClassInfo::IsValid() const
{
   return IsValidCint() || IsValidClang();
}

int TClingBaseClassInfo::InternalNext(int onlyDirect)
{
   // Exit early if the iterator is already invalid.
   if (!fDecl || !fIter ||
         (fIter == llvm::dyn_cast<clang::CXXRecordDecl>(fDecl)->bases_end())) {
      return 0;
   }
   // Advance to the next valid base.
   while (1) {
      // Advance the iterator.
      if (fFirstTime) {
         // The cint semantics are strange.
         fFirstTime = false;
      }
      else if (!onlyDirect && fDescend) {
         // We previously processed a base class which itself has bases,
         // now we process the bases of that base class.
         fDescend = false;
         const clang::RecordType* Ty = fIter->getType()->
                                       getAs<clang::RecordType>();
         // Note: We made sure this would work when we selected the
         //       base for processing.
         clang::CXXRecordDecl* Base = llvm::cast<clang::CXXRecordDecl>(
                                         Ty->getDecl()->getDefinition());
         clang::ASTContext& Context = Base->getASTContext();
         const clang::RecordDecl* RD = llvm::dyn_cast<clang::RecordDecl>(fDecl);
         const clang::ASTRecordLayout& Layout = Context.getASTRecordLayout(RD);
         int64_t offset = Layout.getBaseClassOffset(Base).getQuantity();
         fOffset += static_cast<long>(offset);
         fIterStack.push_back(std::make_pair(std::make_pair(fDecl, fIter),
                                             static_cast<long>(offset)));
         fDecl = Base;
         fIter = Base->bases_begin();
      }
      else {
         // Simple case, move on to the next base class specifier.
         ++fIter;
      }
      // Fix it if we went past the end.
      while (
         (fIter == llvm::dyn_cast<clang::CXXRecordDecl>(fDecl)->bases_end()) &&
         fIterStack.size()
      ) {
         // All done with this base class.
         fDecl = fIterStack.back().first.first;
         fIter = fIterStack.back().first.second;
         fOffset -= fIterStack.back().second;
         fIterStack.pop_back();
         ++fIter;
      }
      // Check for final termination.
      if (fIter == llvm::dyn_cast<clang::CXXRecordDecl>(fDecl)->bases_end()) {
         // We have reached the end of the direct bases, all done.
         delete fBaseInfo;
         fBaseInfo = 0;
         // Iterator is now invalid.
         return 0;
      }
      // Check if current base class is a dependent type, that is, an
      // uninstantiated template class.
      const clang::RecordType* Ty = fIter->getType()->
                                    getAs<clang::RecordType>();
      if (!Ty) {
         // A dependent type (uninstantiated template), skip it.
         continue;
      }
      // Check if current base class has a definition.
      const clang::CXXRecordDecl* Base =
         llvm::cast_or_null<clang::CXXRecordDecl>(Ty->getDecl()->
               getDefinition());
      if (!Base) {
         // No definition yet (just forward declared), skip it.
         continue;
      }
      // Now that we are going to return this base, check to see if
      // we need to examine its bases next call.
      if (!onlyDirect && Base->getNumBases()) {
         fDescend = true;
      }
      // Update info for this base class.
      delete fBaseInfo;
      fBaseInfo = new tcling_ClassInfo(fInterp, Base);
      // Iterator is now valid.
      return 1;
   }
}

int TClingBaseClassInfo::Next(int onlyDirect)
{
   if (!gAllowClang) {
      if (gAllowCint) {
         return fBaseClassInfo->Next(onlyDirect);
      }
      return 0;
   }
   return InternalNext(onlyDirect);
}

int TClingBaseClassInfo::Next()
{
   return Next(1);
}

long TClingBaseClassInfo::Offset() const
{
   if (!IsValid()) {
      return -1;
   }
   if (!IsValidClang()) {
      if (gAllowCint) {
         return fBaseClassInfo->Offset();
      }
      return -1;
   }
   if (!gAllowClang) {
      return -1;
   }
   const clang::RecordType* Ty = fIter->getType()->getAs<clang::RecordType>();
   if (!Ty) {
      // A dependent type (uninstantiated template), invalid.
      return -1;
   }
   // Check if current base class has a definition.
   const clang::CXXRecordDecl* Base =
      llvm::cast_or_null<clang::CXXRecordDecl>(Ty->getDecl()->
            getDefinition());
   if (!Base) {
      // No definition yet (just forward declared), invalid.
      return -1;
   }
   clang::ASTContext& Context = Base->getASTContext();
   const clang::RecordDecl* RD = llvm::dyn_cast<clang::RecordDecl>(fDecl);
   const clang::ASTRecordLayout& Layout = Context.getASTRecordLayout(RD);
   int64_t offset = Layout.getBaseClassOffset(Base).getQuantity();
   long clang_val = fOffset + static_cast<long>(offset);
   return clang_val;
}

long TClingBaseClassInfo::Property() const
{
   if (!IsValid()) {
      return 0L;
   }
   if (!IsValidClang()) {
      if (gAllowCint) {
         return fBaseClassInfo->Property();
      }
      return 0L;
   }
   if (!gAllowClang) {
      return 0L;
   }
   long property = 0L;
   if (fIter->isVirtual()) {
      property |= G__BIT_ISVIRTUALBASE;
   }
   if (fDecl == fClassInfo->GetDecl()) {
      property |= G__BIT_ISDIRECTINHERIT;
   }
   switch (fIter->getAccessSpecifier()) {
      case clang::AS_public:
         property |= G__BIT_ISPUBLIC;
         break;
      case clang::AS_protected:
         property |= G__BIT_ISPROTECTED;
         break;
      case clang::AS_private:
         property |= G__BIT_ISPRIVATE;
         break;
      case clang::AS_none:
         // IMPOSSIBLE
         break;
      default:
         // IMPOSSIBLE
         break;
   }
   return property;
}

long TClingBaseClassInfo::Tagnum() const
{
   if (!IsValid()) {
      return -1L;
   }
   if (!IsValidClang()) {
      if (gAllowCint) {
         return fBaseClassInfo->Tagnum();
      }
      return -1L;
   }
   if (!gAllowClang) {
      return -1L;
   }
   return fBaseInfo->Tagnum();
}

const char* TClingBaseClassInfo::FullName() const
{
   if (!IsValid()) {
      return 0;
   }
   if (!IsValidClang()) {
      if (gAllowCint) {
         return fBaseClassInfo->Fullname();
      }
      return 0;
   }
   if (!gAllowClang) {
      return 0;
   }
   return fBaseInfo->FullName();
}

const char* TClingBaseClassInfo::Name() const
{
   if (!IsValid()) {
      return 0;
   }
   if (!IsValidClang()) {
      if (gAllowCint) {
         return fBaseClassInfo->Name();
      }
      return 0;
   }
   if (!gAllowClang) {
      return 0;
   }
   return fBaseInfo->Name();
}

const char* TClingBaseClassInfo::TmpltName() const
{
   if (!IsValid()) {
      return 0;
   }
   if (!IsValidClang()) {
      if (gAllowCint) {
         return fBaseClassInfo->TmpltName();
      }
      return 0;
   }
   if (!gAllowClang) {
      return 0;
   }
   return fBaseInfo->TmpltName();
}

