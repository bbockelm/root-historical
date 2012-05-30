// @(#)root/roostats:$Id$
// Author: Kyle Cranmer   28/07/2008

/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/////////////////////////////////////////
// RooStats
//
// namespace for classes and functions of the RooStats package
/////////////////////////////////////////
#include "Rtypes.h"

#if !defined(R__ALPHA) && !defined(R__SOLARIS) && !defined(R__ACC) && !defined(R__FBSD)
NamespaceImp(RooStats)
#endif

#include "TTree.h"

#include "RooProdPdf.h"
#include "RooSimultaneous.h"
#include "RooStats/ModelConfig.h"
#include "RooStats/RooStatsUtils.h"
#include <typeinfo>

using namespace std;

// this file is only for the documentation of RooStats namespace

namespace RooStats { 



 void FactorizePdf(const RooArgSet &observables, RooAbsPdf &pdf, RooArgList &obsTerms, RooArgList &constraints) {
   // utility function to factorize constraint terms from a pdf 
   // (from G. Petrucciani)
   const std::type_info & id = typeid(pdf);
   if (id == typeid(RooProdPdf)) {
      RooProdPdf *prod = dynamic_cast<RooProdPdf *>(&pdf);
      RooArgList list(prod->pdfList());
      for (int i = 0, n = list.getSize(); i < n; ++i) {
         RooAbsPdf *pdfi = (RooAbsPdf *) list.at(i);
            FactorizePdf(observables, *pdfi, obsTerms, constraints);
         }
      } else if (id == typeid(RooSimultaneous) ) {    //|| id == typeid(RooSimultaneousOpt)) {
         RooSimultaneous *sim  = dynamic_cast<RooSimultaneous *>(&pdf);
         assert(sim != 0);
         RooAbsCategoryLValue *cat = (RooAbsCategoryLValue *) sim->indexCat().Clone();
         for (int ic = 0, nc = cat->numBins((const char *)0); ic < nc; ++ic) {
            cat->setBin(ic);
            FactorizePdf(observables, *sim->getPdf(cat->getLabel()), obsTerms, constraints);
         }
         delete cat;
      } else if (pdf.dependsOn(observables)) {
         if (!obsTerms.contains(pdf)) obsTerms.add(pdf);
      } else {
         if (!constraints.contains(pdf)) constraints.add(pdf);
      }
   }


   void FactorizePdf(RooStats::ModelConfig &model, RooAbsPdf &pdf, RooArgList &obsTerms, RooArgList &constraints) {
      // utility function to factorize constraint terms from a pdf 
      // (from G. Petrucciani)
      if (!model.GetObservables() ) { 
         oocoutE((TObject*)0,InputArguments) << "MakeNuisancePdf - invalid input model: missing observables  " << endl;
         return;
      }
      return FactorizePdf(*model.GetObservables(), pdf, obsTerms, constraints);
   }


   RooAbsPdf * MakeNuisancePdf(RooAbsPdf &pdf, const RooArgSet &observables, const char *name) { 
      // make a nuisance pdf by factorizing out all constraint terms in a common pdf 
      RooArgList obsTerms, constraints;
      FactorizePdf(observables, pdf, obsTerms, constraints);
      return new RooProdPdf(name,"", constraints);
   }

   RooAbsPdf * MakeNuisancePdf(const RooStats::ModelConfig &model, const char *name) { 
      // make a nuisance pdf by factorizing out all constraint terms in a common pdf
      if (!model.GetPdf() || !model.GetObservables() ) { 
         oocoutE((TObject*)0,InputArguments) << "MakeNuisancePdf - invalid input model  " << endl;
         return 0;
      }
      return MakeNuisancePdf(*model.GetPdf(), *model.GetObservables(), name);
   }

   // Helper class for GetAsTTree
   class BranchStore {
      public:
         std::map<TString, Double_t> varVals;
         double inval;

         BranchStore(const vector <TString> &params = vector <TString>(), double _inval = -999.) {
            inval = _inval;
            for(unsigned int i = 0;i<params.size();i++)
               varVals[params[i]] = _inval;
         }

         void AssignToTTree(TTree &myTree) {
            for(std::map<TString, Double_t>::iterator it = varVals.begin();it!=varVals.end();it++) {
               const TString& name = it->first;
               myTree.Branch( name, &varVals[name], TString::Format("%s/D", name.Data()));
            }
         }
         void ResetValues() {
            for(std::map<TString, Double_t>::iterator it = varVals.begin();it!=varVals.end();it++) {
               const TString& name = it->first;
               varVals[name] = inval;
            }
         }
   };

   BranchStore* CreateBranchStore(const RooDataSet& data) {
      if (data.numEntries() == 0) {
         return new BranchStore;
      }
      vector <TString> V;
      const RooArgSet* aset = data.get(0);
      RooAbsArg *arg(0);
      TIterator *it = aset->createIterator();
      for(;(arg = dynamic_cast<RooAbsArg*>(it->Next()));) {
         RooRealVar *rvar = dynamic_cast<RooRealVar*>(arg);
         if (rvar == NULL)
            continue;
         V.push_back(rvar->GetName());
         if (rvar->hasAsymError()) {
            V.push_back(TString::Format("%s_errlo", rvar->GetName()));
            V.push_back(TString::Format("%s_errhi", rvar->GetName()));
         }
         else if (rvar->hasError()) {
            V.push_back(TString::Format("%s_err", rvar->GetName()));
         }
      }
      delete it;
      return new BranchStore(V);
   }

   void FillTree(TTree &myTree, const RooDataSet &data) {
      BranchStore *bs = CreateBranchStore(data);
      bs->AssignToTTree(myTree);

      for(int entry = 0;entry<data.numEntries();entry++) {
         bs->ResetValues();
         const RooArgSet* aset = data.get(entry);
         RooAbsArg *arg(0);
         RooLinkedListIter it = aset->iterator();
         for(;(arg = dynamic_cast<RooAbsArg*>(it.Next()));) {
            RooRealVar *rvar = dynamic_cast<RooRealVar*>(arg);
            if (rvar == NULL)
               continue;
            bs->varVals[rvar->GetName()] = rvar->getValV();
            if (rvar->hasAsymError()) {
               bs->varVals[TString::Format("%s_errlo", rvar->GetName())] = rvar->getAsymErrorLo();
               bs->varVals[TString::Format("%s_errhi", rvar->GetName())] = rvar->getAsymErrorHi();
            }
            else if (rvar->hasError()) {
               bs->varVals[TString::Format("%s_err", rvar->GetName())] = rvar->getError();
            }
         }
         myTree.Fill();
      }
      delete bs;
   }

   TTree * GetAsTTree(TString name, TString desc, const RooDataSet& data) {
      TTree* myTree = new TTree(name, desc);
      FillTree(*myTree, data);
      return myTree;
   }

}
