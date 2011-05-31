// @(#)root/roostats:$Id: FrequentistCalculator.cxx 37084 2010-11-29 21:37:13Z moneta $
// Author: Kyle Cranmer, Sven Kreiss   23/05/10
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/**
Does a frequentist hypothesis test. Nuisance parameters are fixed to their
MLEs.
*/

#include "RooStats/FrequentistCalculator.h"
#include "RooStats/ToyMCSampler.h"


ClassImp(RooStats::FrequentistCalculator)

using namespace RooStats;



int FrequentistCalculator::PreNullHook(RooArgSet *parameterPoint, double obsTestStat) const {

   // ****** any TestStatSampler ********

   // note: making nll or profile class variables can only be done in the constructor
   // as all other hooks are const (which has to be because GetHypoTest is const). However,
   // when setting it only in constructor, they would have to be changed every time SetNullModel
   // or SetAltModel is called. Simply put, converting them into class variables breaks
   // encapsulation.
   RooFit::MsgLevel msglevel = RooMsgService::instance().globalKillBelow();
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   // create profile keeping everything but nuisance parameters fixed
   RooArgSet allButNuisance(*fNullModel->GetPdf()->getParameters(*fData));
   allButNuisance.remove(*fNullModel->GetNuisanceParameters());
   RooAbsReal* nll = fNullModel->GetPdf()->createNLL(*const_cast<RooAbsData*>(fData), RooFit::CloneData(kFALSE));
   RooAbsReal* profile = nll->createProfile(allButNuisance);
   profile->getVal(); // this will do fit and set nuisance parameters to profiled values
   // add nuisance parameters to parameter point
   if(fNullModel->GetNuisanceParameters())
      parameterPoint->add(*fNullModel->GetNuisanceParameters());

   delete profile;
   delete nll;
   RooMsgService::instance().setGlobalKillBelow(msglevel);



   // ***** ToyMCSampler specific *******

   // check whether TestStatSampler is a ToyMCSampler
   ToyMCSampler *toymcs = dynamic_cast<ToyMCSampler*>(GetTestStatSampler());
   if(toymcs) {
      oocoutI((TObject*)0,InputArguments) << "Using a ToyMCSampler. Now configuring for Null." << endl;

      // variable number of toys
      if(fNToysNull) toymcs->SetNToys(fNToysNull);

      // adaptive sampling
      if(fNToysNullTail) {
         oocoutI((TObject*)0,InputArguments) << "Adaptive Sampling" << endl;
         if(GetTestStatSampler()->GetTestStatistic()->PValueIsRightTail()) {
            toymcs->SetToysRightTail(fNToysNullTail, obsTestStat);
         }else{
            toymcs->SetToysLeftTail(fNToysNullTail, obsTestStat);
         }
      }else{
         toymcs->SetToysBothTails(0, 0, obsTestStat); // disable adaptive sampling
      }

      // importance sampling
      if(fNullImportanceDensity) {
         oocoutI((TObject*)0,InputArguments) << "Importance Sampling" << endl;
         toymcs->SetImportanceDensity(fNullImportanceDensity);
         if(fNullImportanceSnapshot) toymcs->SetImportanceSnapshot(*fNullImportanceSnapshot);
      }else{
         toymcs->SetImportanceDensity(NULL);       // disable importance sampling
      }
      GetNullModel()->LoadSnapshot();
   }

   return 0;
}


int FrequentistCalculator::PreAltHook(RooArgSet *parameterPoint, double obsTestStat) const {

   // ****** any TestStatSampler ********

   RooFit::MsgLevel msglevel = RooMsgService::instance().globalKillBelow();
   RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

   // create profile keeping everything but nuisance parameters fixed
   RooArgSet allButNuisance(*fAltModel->GetPdf()->getParameters(*fData));
   allButNuisance.remove(*fAltModel->GetNuisanceParameters());
   RooAbsReal* nll = fAltModel->GetPdf()->createNLL(*const_cast<RooAbsData*>(fData), RooFit::CloneData(kFALSE));
   RooAbsReal* profile = nll->createProfile(allButNuisance);
   profile->getVal(); // this will do fit and set nuisance parameters to profiled values
   // add nuisance parameters to parameter point
   if(fAltModel->GetNuisanceParameters())
      parameterPoint->add(*fAltModel->GetNuisanceParameters());

   delete profile;
   delete nll;
   RooMsgService::instance().setGlobalKillBelow(msglevel);




   // ***** ToyMCSampler specific *******

   // check whether TestStatSampler is a ToyMCSampler
   ToyMCSampler *toymcs = dynamic_cast<ToyMCSampler*>(GetTestStatSampler());
   if(toymcs) {
      oocoutI((TObject*)0,InputArguments) << "Using a ToyMCSampler. Now configuring for Alt." << endl;

      // variable number of toys
      if(fNToysAlt) toymcs->SetNToys(fNToysAlt);

      // adaptive sampling
      if(fNToysAltTail) {
         oocoutI((TObject*)0,InputArguments) << "Adaptive Sampling" << endl;
         if(GetTestStatSampler()->GetTestStatistic()->PValueIsRightTail()) {
            toymcs->SetToysLeftTail(fNToysAltTail, obsTestStat);
         }else{
            toymcs->SetToysRightTail(fNToysAltTail, obsTestStat);
         }
      }else{
         toymcs->SetToysBothTails(0, 0, obsTestStat); // disable adaptive sampling
      }


      // importance sampling
      if(fAltImportanceDensity) {
         oocoutI((TObject*)0,InputArguments) << "Importance Sampling" << endl;
         toymcs->SetImportanceDensity(fAltImportanceDensity);
         if(fAltImportanceSnapshot) toymcs->SetImportanceSnapshot(*fAltImportanceSnapshot);
      }else{
         toymcs->SetImportanceDensity(NULL);       // disable importance sampling
      }
   }

   return 0;
}




