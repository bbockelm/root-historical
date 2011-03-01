/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 *    File: $Id$
 * Authors:                                                                  *
 *   WV, Wouter Verkerke, UC Santa Barbara, verkerke@slac.stanford.edu       *
 *   DK, David Kirkby,    UC Irvine,         dkirkby@uci.edu                 *
 *                                                                           *
 * Copyright (c) 2000-2005, Regents of the University of California          *
 *                          and Stanford University. All rights reserved.    *
 *                                                                           *
 * Redistribution and use in source and binary forms,                        *
 * with or without modification, are permitted according to the terms        *
 * listed in LICENSE (http://roofit.sourceforge.net/license.txt)             *
 *****************************************************************************/
#ifndef ROO_ABS_DATA_STORE
#define ROO_ABS_DATA_STORE

#include "Rtypes.h"
#include "RooArgSet.h" 
#include "RooAbsData.h"
#include "TNamed.h"
#include <list>


class RooAbsArg ;
class RooArgList ;
class TIterator ;
class TTree ;

class RooAbsDataStore : public TNamed, public RooPrintable {
public:

  RooAbsDataStore() ; 
  RooAbsDataStore(const char* name, const char* title, const RooArgSet& vars) ; 
  RooAbsDataStore(const RooAbsDataStore& other, const char* newname=0) ; 
  RooAbsDataStore(const RooAbsDataStore& other, const RooArgSet& vars, const char* newname=0) ; 
  virtual RooAbsDataStore* clone(const RooArgSet& vars, const char* newname=0) const = 0 ;
  virtual ~RooAbsDataStore() ;

  // Write current row
  virtual Int_t fill() = 0 ;
  
  // Retrieve a row
  virtual const RooArgSet* get(Int_t index) const = 0 ;
  virtual const RooArgSet* get() const { return &_vars ; } 
  virtual Double_t weight() const = 0 ;

  virtual Double_t weightError(RooAbsData::ErrorType etype=RooAbsData::Poisson) const = 0 ;
  virtual void weightError(Double_t& lo, Double_t& hi, RooAbsData::ErrorType etype=RooAbsData::Poisson) const = 0 ; 

  virtual Double_t weight(Int_t index) const = 0 ;

  virtual Bool_t isWeighted() const = 0 ;

  // Change observable name
  virtual Bool_t changeObservableName(const char* from, const char* to) =0 ;
  
  // Add one or more columns
  virtual RooAbsArg* addColumn(RooAbsArg& var, Bool_t adjustRange=kTRUE) = 0 ;
  virtual RooArgSet* addColumns(const RooArgList& varList) = 0 ;

  // Merge column-wise
  virtual RooAbsDataStore* merge(const RooArgSet& allvars, std::list<RooAbsDataStore*> dstoreList) = 0 ;

  // Add rows 
  virtual void append(RooAbsDataStore& other)= 0 ;

  // General & bookkeeping methods
  virtual Bool_t valid() const = 0 ;
  virtual Int_t numEntries() const = 0 ;
  virtual void reset() = 0 ;


  // Printing interface (human readable)
  inline virtual void Print(Option_t *options= 0) const {
    // Print contents on stdout
    printStream(defaultPrintStream(),defaultPrintContents(options),defaultPrintStyle(options));
  }

  virtual void printName(ostream& os) const ;
  virtual void printTitle(ostream& os) const ;
  virtual void printClassName(ostream& os) const ;
  virtual void printArgs(ostream& os) const ;
  virtual void printValue(ostream& os) const ;
  void printMultiline(ostream& os, Int_t content, Bool_t verbose, TString indent) const ;

  virtual Int_t defaultPrintContents(Option_t* opt) const ;
   

  // Constant term  optimizer interface
  virtual void cacheArgs(const RooAbsArg* cacheOwner, RooArgSet& varSet, const RooArgSet* nset=0) = 0 ;
  virtual const RooAbsArg* cacheOwner() = 0 ;
  virtual void attachCache(const RooAbsArg* newOwner, const RooArgSet& cachedVars) = 0 ;
  virtual void setArgStatus(const RooArgSet& set, Bool_t active) = 0 ;
  const RooArgSet& cachedVars() const { return _cachedVars ; }
  virtual void resetCache() = 0 ;

  virtual void setDirtyProp(Bool_t flag) { _doDirtyProp = flag ; }

  virtual void checkInit() const {} ;
  
  Bool_t hasFilledCache() const { return _cachedVars.getSize()>0 ; }

  virtual const TTree* tree() const { return 0 ; }

 protected:

  RooArgSet _vars ;
  RooArgSet _cachedVars ;
  TIterator *_iterator;    //! Iterator over dimension variables
  TIterator *_cacheIter ;  //! Iterator over cached variables

  Bool_t _doDirtyProp ;    // Switch do (de)activate dirty state propagation when loading a data point

  ClassDef(RooAbsDataStore,1) // Abstract Data Storage class
};


#endif
