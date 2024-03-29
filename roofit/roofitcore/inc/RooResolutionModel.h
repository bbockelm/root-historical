/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 *    File: $Id: RooResolutionModel.h,v 1.26 2007/05/14 18:37:46 wouter Exp $
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
#ifndef ROO_RESOLUTION_MODEL
#define ROO_RESOLUTION_MODEL

#include "RooAbsPdf.h"
#include "RooRealProxy.h"
#include "RooRealVar.h"
#include "RooFormulaVar.h"

class RooResolutionModel : public RooAbsPdf {
public:

  // Constructors, assignment etc
  inline RooResolutionModel() : _basis(0) { }
  RooResolutionModel(const char *name, const char *title, RooRealVar& x) ; 
  RooResolutionModel(const RooResolutionModel& other, const char* name=0);
  virtual TObject* clone(const char* newname) const = 0 ;
  virtual ~RooResolutionModel();

  Double_t getValV(const RooArgSet* nset=0) const ;
  virtual RooResolutionModel* convolution(RooFormulaVar* basis, RooAbsArg* owner) const ;
  RooRealVar& convVar() const ;
  const RooRealVar& basisConvVar() const ;

  inline Bool_t isBasisSupported(const char* name) const { return basisCode(name)?kTRUE:kFALSE ; }
  virtual Int_t basisCode(const char* name) const = 0 ;

  virtual void normLeafServerList(RooArgSet& list) const ;
  Double_t getNorm(const RooArgSet* nset=0) const ;

  inline const RooFormulaVar& basis() const { return _basis?*_basis:*identity() ; }
  Bool_t isConvolved() { return _basis ? kTRUE : kFALSE ; }

  virtual void printMultiline(std::ostream& os, Int_t content, Bool_t verbose=kFALSE, TString indent="") const ;

  static void cleanup() ;

  static RooFormulaVar* identity() ;

protected:

  static RooFormulaVar* _identity ;  // Identity basis function pointe


  friend class RooConvGenContext ;
  friend class RooAddModel ;
  RooRealProxy x ;                   // Dependent/convolution variable

  virtual Bool_t redirectServersHook(const RooAbsCollection& newServerList, Bool_t mustReplaceAll, Bool_t nameChange, Bool_t isRecursive) ;
  virtual void changeBasis(RooFormulaVar* basis) ;
  Bool_t traceEvalHook(Double_t value) const ;


  friend class RooAbsAnaConvPdf ;

  Int_t _basisCode ;         // Identifier code for selected basis function
  RooFormulaVar* _basis ;    // Basis function convolved with this resolution model
  Bool_t _ownBasis ;         // Flag indicating ownership of _basis 

  ClassDef(RooResolutionModel,1) // Abstract Resolution Model
};

#endif
