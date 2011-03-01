/*****************************************************************************
 * Project: RooFit                                                           *
 * Package: RooFitCore                                                       *
 * @(#)root/roofitcore:$Id$
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

//////////////////////////////////////////////////////////////////////////////
// 
// BEGIN_HTML 
// RooAddGenContext is an efficient implementation of the
// generator context specific for RooAddPdf PDFs. The strategy
// of RooAddGenContext is to defer generation of each component
// to a dedicated generator context for that component and to
// randomly choose one of those context to generate an event,
// with a probability proportional to its associated coefficient
// END_HTML
//


#include "RooFit.h"

#include "Riostream.h"


#include "RooMsgService.h"
#include "RooAddGenContext.h"
#include "RooAddGenContext.h"
#include "RooAddPdf.h"
#include "RooDataSet.h"
#include "RooRandom.h"
#include "RooAddModel.h"

ClassImp(RooAddGenContext)
;
  

//_____________________________________________________________________________
RooAddGenContext::RooAddGenContext(const RooAddPdf &model, const RooArgSet &vars, 
				   const RooDataSet *prototype, const RooArgSet* auxProto,
				   Bool_t verbose) :
  RooAbsGenContext(model,vars,prototype,auxProto,verbose), _isModel(kFALSE)
{
  // Constructor

  cxcoutI(Generation) << "RooAddGenContext::ctor() setting up event special generator context for sum p.d.f. " << model.GetName() 
			<< " for generation of observable(s) " << vars ;
  if (prototype) ccxcoutI(Generation) << " with prototype data for " << *prototype->get() ;
  if (auxProto && auxProto->getSize()>0)  ccxcoutI(Generation) << " with auxiliary prototypes " << *auxProto ;
  ccxcoutI(Generation) << endl ;

  // Constructor. Build an array of generator contexts for each product component PDF
  _pdfSet = (RooArgSet*) RooArgSet(model).snapshot(kTRUE) ;
  _pdf = (RooAddPdf*) _pdfSet->find(model.GetName()) ;

  // Fix normalization set of this RooAddPdf
  if (prototype) 
    {
      RooArgSet coefNSet(vars) ;
      coefNSet.add(*prototype->get()) ;
      _pdf->fixAddCoefNormalization(coefNSet) ;
    }

  model._pdfIter->Reset() ;
  RooAbsPdf* pdf ;
  _nComp = model._pdfList.getSize() ;
  _coefThresh = new Double_t[_nComp+1] ;
  _vars = (RooArgSet*) vars.snapshot(kFALSE) ;

  while((pdf=(RooAbsPdf*)model._pdfIter->Next())) {
    RooAbsGenContext* cx = pdf->genContext(vars,prototype,auxProto,verbose) ;
    _gcList.Add(cx) ;
  }  

  ((RooAddPdf*)_pdf)->getProjCache(_vars) ;
  _pdf->recursiveRedirectServers(*_theEvent) ;
}



//_____________________________________________________________________________
RooAddGenContext::RooAddGenContext(const RooAddModel &model, const RooArgSet &vars, 
				   const RooDataSet *prototype, const RooArgSet* auxProto,
				   Bool_t verbose) :
  RooAbsGenContext(model,vars,prototype,auxProto,verbose), _isModel(kTRUE)
{
  // Constructor

  cxcoutI(Generation) << "RooAddGenContext::ctor() setting up event special generator context for sum resolution model " << model.GetName() 
			<< " for generation of observable(s) " << vars ;
  if (prototype) ccxcoutI(Generation) << " with prototype data for " << *prototype->get() ;
  if (auxProto && auxProto->getSize()>0)  ccxcoutI(Generation) << " with auxiliary prototypes " << *auxProto ;
  ccxcoutI(Generation) << endl ;

  // Constructor. Build an array of generator contexts for each product component PDF
  _pdfSet = (RooArgSet*) RooArgSet(model).snapshot(kTRUE) ;
  _pdf = (RooAbsPdf*) _pdfSet->find(model.GetName()) ;


  model._pdfIter->Reset() ;
  RooAbsPdf* pdf ;
  _nComp = model._pdfList.getSize() ;
  _coefThresh = new Double_t[_nComp+1] ;
  _vars = (RooArgSet*) vars.snapshot(kFALSE) ;

  while((pdf=(RooAbsPdf*)model._pdfIter->Next())) {
    RooAbsGenContext* cx = pdf->genContext(vars,prototype,auxProto,verbose) ;
    _gcList.Add(cx) ;
  }  

  ((RooAddModel*)_pdf)->getProjCache(_vars) ;
  _pdf->recursiveRedirectServers(*_theEvent) ;
}



//_____________________________________________________________________________
RooAddGenContext::~RooAddGenContext()
{
  // Destructor. Delete all owned subgenerator contexts

  delete[] _coefThresh ;
  _gcList.Delete() ;
  delete _vars ;
  delete _pdfSet ;
}



//_____________________________________________________________________________
void RooAddGenContext::attach(const RooArgSet& args) 
{
  // Attach given set of variables to internal p.d.f. clone

  _pdf->recursiveRedirectServers(args) ;

  // Forward initGenerator call to all components
  TIterator* iter = _gcList.MakeIterator() ;
  RooAbsGenContext* gc ;
  while((gc=(RooAbsGenContext*)iter->Next())){
    gc->attach(args) ;
  }
  delete iter ;
}



//_____________________________________________________________________________
void RooAddGenContext::initGenerator(const RooArgSet &theEvent)
{
  // One-time initialization of generator contex. Attach theEvent
  // to internal p.d.f clone and forward initialization call to 
  // the component generators

  _pdf->recursiveRedirectServers(theEvent) ;

  // Forward initGenerator call to all components
  TIterator* iter = _gcList.MakeIterator() ;
  RooAbsGenContext* gc ;
  while((gc=(RooAbsGenContext*)iter->Next())){
    gc->initGenerator(theEvent) ;
  }
  delete iter ;
}


//_____________________________________________________________________________
void RooAddGenContext::generateEvent(RooArgSet &theEvent, Int_t remaining)
{
  // Randomly choose one of the component contexts to generate this event,
  // with a probability proportional to its coefficient

  // Throw a random number to determin which component to generate
  updateThresholds() ;
  Double_t rand = RooRandom::uniform() ;
  Int_t i=0 ;
  for (i=0 ; i<_nComp ; i++) {
    if (rand>_coefThresh[i] && rand<_coefThresh[i+1]) {
      ((RooAbsGenContext*)_gcList.At(i))->generateEvent(theEvent,remaining) ;
      return ;
    }
  }
}


//_____________________________________________________________________________
void RooAddGenContext::updateThresholds()
{
  // Update the cumulative threshold table from the current coefficient
  // values

  if (_isModel) {
    
    RooAddModel* amod = (RooAddModel*) _pdf ;

    RooAddModel::CacheElem* cache = amod->getProjCache(_vars) ;
    amod->updateCoefficients(*cache,_vars) ;

    _coefThresh[0] = 0. ;
    Int_t i ;
    for (i=0 ; i<_nComp ; i++) {
      _coefThresh[i+1] = amod->_coefCache[i] ;
      _coefThresh[i+1] += _coefThresh[i] ;
    }

  } else {
    RooAddPdf* apdf = (RooAddPdf*) _pdf ;

    //cout << "Now calling getProjCache()" << endl ;
    RooAddPdf::CacheElem* cache = apdf->getProjCache(_vars,0,"FULL_RANGE_ADDGENCONTEXT") ;
    apdf->updateCoefficients(*cache,_vars) ;

    _coefThresh[0] = 0. ;
    Int_t i ;
    for (i=0 ; i<_nComp ; i++) {
      _coefThresh[i+1] = apdf->_coefCache[i] ;
      _coefThresh[i+1] += _coefThresh[i] ;
//       cout << "RooAddGenContext::updateThresholds(" << GetName() << ") _coefThresh[" << i+1 << "] = " << _coefThresh[i+1] << endl ;
    }
    
  }

}


//_____________________________________________________________________________
void RooAddGenContext::setProtoDataOrder(Int_t* lut)
{
  // Forward the setProtoDataOrder call to the component generator contexts

  RooAbsGenContext::setProtoDataOrder(lut) ;
  Int_t i ;
  for (i=0 ; i<_nComp ; i++) {
    ((RooAbsGenContext*)_gcList.At(i))->setProtoDataOrder(lut) ;
  }
}



//_____________________________________________________________________________
void RooAddGenContext::printMultiline(ostream &os, Int_t content, Bool_t verbose, TString indent) const 
{
  // Print the details of the context

  RooAbsGenContext::printMultiline(os,content,verbose,indent) ;
  os << indent << "--- RooAddGenContext ---" << endl ;
  os << indent << "Using PDF ";
  _pdf->printStream(os,kName|kArgs|kClassName,kSingleLine,indent);
  
  os << indent << "List of component generators" << endl ;
  TString indent2(indent) ;
  indent2.Append("    ") ;
  for (Int_t i=0 ; i<_nComp ; i++) {
    ((RooAbsGenContext*)_gcList.At(i))->printMultiline(os,content,verbose,indent2) ;
  }
}
