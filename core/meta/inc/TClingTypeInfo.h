// @(#)root/core/meta:$Id$
// Author: Paul Russo   30/07/2012

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TClingTypeInfo
#define ROOT_TClingTypeInfo

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

#include "clang/AST/Type.h"

namespace cling {
class Interpreter;
}

extern "C" struct G__value;

class TClingTypeInfo {

private:
   cling::Interpreter  *fInterp;    //Cling interpreter, we do *not* own.
   clang::QualType      fQualType;  //Clang qualified type we are querying.

public:

   explicit TClingTypeInfo(cling::Interpreter *interp)
      : fInterp(interp) {}

   TClingTypeInfo(cling::Interpreter *interp, clang::QualType ty)
      : fInterp(interp), fQualType(ty) {}

   TClingTypeInfo(cling::Interpreter *interp, const char *name);

   cling::Interpreter  *GetInterpreter() const { return fInterp; }

   clang::QualType      GetQualType() const { return fQualType; }

   void                 Init(const char *name); // Set type by name.
   void                 Init(clang::QualType ty) { fQualType = ty; }
   bool                 IsValid() const { return !fQualType.isNull(); }
   const char          *Name() const; // Get name of type.
   long                 Property() const; // Get properties of type.
   int                  RefType() const; // Get CINT reftype of type.
   int                  Size() const; // Get size in bytes of type.
   const char          *StemName() const; // Get name of type chain leaf node.
   const char          *TrueName() const; // Get name of type with no typedefs.

};

#endif // ROOT_TClingTypeInfo
