// @(#)root/roostats:$Id: DetailedOutputAggregator.h 37084 2010-11-29 21:37:13Z moneta $
// Author: Sven Kreiss, Kyle Cranmer   Nov 2010
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOSTATS_DetailedOutputAggregator
#define ROOSTATS_DetailedOutputAggregator

//_________________________________________________
/*
   BEGIN_HTML
   <p>
   This class is designed to aid in the construction of RooDataSets and RooArgSets,
   particularly those naturally arising in fitting operations.

   Typically, the usage of this class is as follows:
   <ol>
   <li> create DetailedOutputAggregator instance </li>
   <li> use AppendArgSet to add value sets to be stored as one row of the dataset </li>
   <li> call CommitSet when an entire row's worth of values has been added </li>
   <li> repeat steps 2 and 3 until all rows have been added </li>
   <li> call GetAsDataSet to extract result RooDataSet </li>
   </ol>

   </p>
   END_HTML
   */
//

#include <limits>
#include "RooFitResult.h"
#include "RooPullVar.h"
#include "RooRealVar.h"
#include "RooDataSet.h"

namespace RooStats {

   class DetailedOutputAggregator {

      public:
         // Translate the given fit result to a RooArgSet in a generic way.
         // Prefix is prepended to all variable names.
         static RooArgSet *GetAsArgSet(RooFitResult *result, TString prefix="", bool withErrorsAndPulls=false) {
            RooArgSet *detailedOutput = new RooArgSet;
            const RooArgList &detOut = result->floatParsFinal();
            const RooArgList &truthSet = result->floatParsInit();
            TIterator *it = detOut.createIterator();
            while(RooAbsArg* v = dynamic_cast<RooAbsArg*>(it->Next())) {
               RooAbsArg* clone = v->cloneTree(TString().Append(prefix).Append(v->GetName()));
               clone->SetTitle( TString().Append(prefix).Append(v->GetTitle()) );
               RooRealVar* var = dynamic_cast<RooRealVar*>(v);
               if (var) clone->setAttribute("StoreError");
               detailedOutput->addOwned(*clone);

               if( withErrorsAndPulls && var ) {
                  clone->setAttribute("StoreAsymError");

                  TString pullname = TString().Append(prefix).Append(TString::Format("%s_pull", var->GetName()));
                  //             TString pulldesc = TString::Format("%s pull for fit %u", var->GetTitle(), fitNumber);
                  RooRealVar* truth = dynamic_cast<RooRealVar*>(truthSet.find(var->GetName()));
                  RooPullVar pulltemp("temppull", "temppull", *var, *truth);
                  RooRealVar* pull = new RooRealVar(pullname, pullname, pulltemp.getVal());
                  detailedOutput->addOwned(*pull);
               }
            }
            delete it;

            // monitor a few more variables
            detailedOutput->addOwned( *new RooRealVar(TString().Append(prefix).Append("minNLL"), TString().Append(prefix).Append("minNLL"), result->minNll() ) );
            detailedOutput->addOwned( *new RooRealVar(TString().Append(prefix).Append("fitStatus"), TString().Append(prefix).Append("fitStatus"), result->status() ) );
            detailedOutput->addOwned( *new RooRealVar(TString().Append(prefix).Append("covQual"), TString().Append(prefix).Append("covQual"), result->covQual() ) );
            detailedOutput->addOwned( *new RooRealVar(TString().Append(prefix).Append("numInvalidNLLEval"), TString().Append(prefix).Append("numInvalidNLLEval"), result->numInvalidNLL() ) );
            return detailedOutput;
         }

         DetailedOutputAggregator() {
            result = NULL;
            builtSet = NULL;
         }

         // For each variable in aset, prepend prefix to its name and add
         // to the internal store. Note this will not appear in the produced
         // dataset unless CommitSet is called.
         void AppendArgSet(const RooAbsCollection *aset, TString prefix="") {
            if (aset == NULL) {
               // silently ignore
               //std::cout << "Attempted to append NULL" << endl;
               return;
            }
            if (builtSet == NULL) {
               builtSet = new RooArgList();
            }
            TIterator* iter = aset->createIterator();
            while(RooAbsArg* v = dynamic_cast<RooAbsArg*>( iter->Next() ) ) {
               TString renamed(TString::Format("%s%s", prefix.Data(), v->GetName()));
               if (result == NULL) {
                  // we never commited, so by default all columns are expected to not exist
                  RooAbsArg* var = v->createFundamental();
                  (RooArgSet(*var)) = RooArgSet(*v);
                  var->SetName(renamed);
                  if (RooRealVar* rvar= dynamic_cast<RooRealVar*>(var)) {
                     if (v->getAttribute("StoreError"))     var->setAttribute("StoreError");
                     else rvar->removeError();
                     if (v->getAttribute("StoreAsymError")) var->setAttribute("StoreAsymError");
                     else rvar->removeAsymError();
                  }
                  if (builtSet->addOwned(*var)) continue;  // OK - can skip past setting value
               }
               if (RooAbsArg* var = builtSet->find(renamed)) {
                  // we already commited an argset once, so we expect all columns to already be in the set
                  var->SetName(v->GetName());
                  (RooArgSet(*var)) = RooArgSet(*v); // copy values and errors
                  var->SetName(renamed);
               }
            }
            delete iter;
         }

         const RooArgList* GetAsArgList() const {
            // Returns this set of detailed output.
            return builtSet;
         }

         // Commit to the result RooDataSet.
         void CommitSet(double weight=1.0) {
            if (result == NULL) {
               // Store dataset as a tree - problem with VectorStore and StoreError (bug #94908)
               RooAbsData::StorageType defStore= RooAbsData::defaultStorageType;
               RooAbsData::defaultStorageType = RooAbsData::Tree;
               RooRealVar wgt("weight","weight",1.0);
               result = new RooDataSet("", "", RooArgSet(*builtSet,wgt), RooFit::WeightVar(wgt));
               RooAbsData::defaultStorageType = defStore;
            }
            result->add(RooArgSet(*builtSet), weight);
            TIterator* iter = builtSet->createIterator();
            while(RooAbsArg* v = dynamic_cast<RooAbsArg*>( iter->Next() ) ) {
               if (RooRealVar* var= dynamic_cast<RooRealVar*>(v)) {
                  // Invalidate values in case we don't set some of them next time round (eg. if fit not done)
                  var->setVal(std::numeric_limits<Double_t>::quiet_NaN());
                  var->removeError();
                  var->removeAsymError();
               }
            }
            delete iter;
         }

         RooDataSet *GetAsDataSet(TString name, TString title) {
            // Returns all detailed output as a dataset.
            // Ownership of the dataset is transferred to the caller.
            RooDataSet* temp = NULL;
            if( result ) {
               temp = result;
               result = NULL;   // we no longer own the dataset
               temp->SetNameTitle( name.Data(), title.Data() );
            }else{
               RooRealVar wgt("weight","weight",1.0);
               temp = new RooDataSet(name.Data(), title.Data(), RooArgSet(wgt), RooFit::WeightVar(wgt));
            }
            delete builtSet;
            builtSet = NULL;

            return temp;
         }

         virtual ~DetailedOutputAggregator() {
            if (result != NULL) delete result;
            if (builtSet != NULL) delete builtSet;
         }

      private:
         RooDataSet *result;
         RooArgList *builtSet;

      protected:
         ClassDef(DetailedOutputAggregator,1)
   };
}

#endif
