// @(#)root/roostats:$Id$
// Author: Kyle Cranmer, Lorenzo Moneta, Gregory Schott, Wouter Verkerke
// Additional Contributions: Giovanni Petrucciani 
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "RooStats/ProfileLikelihoodTestStat.h"

#include "RooProfileLL.h"
#include "RooNLLVar.h"
#include "RooMsgService.h"
#include "RooMinimizer.h"
#include "RooArgSet.h"
#include "RooAbsData.h"
#include "TStopwatch.h"

#include "RooStats/RooStatsUtils.h"


Bool_t RooStats::ProfileLikelihoodTestStat::fgAlwaysReuseNll = kTRUE ;

Double_t RooStats::ProfileLikelihoodTestStat::EvaluateProfileLikelihood(int type, RooAbsData& data, RooArgSet& paramsOfInterest) {
        // interna function to evaluate test statistics
        // can do depending on type: 
        // type  = 0 standard evaluation, type = 1 find only unconditional NLL minimum, type = 2 conditional MLL

       if (!&data) {
	 cout << "problem with data" << endl;
	 return 0 ;
       }

       //data.Print("V");
       
       TStopwatch tsw; 
       tsw.Start();

       double initial_mu_value  = 0;
       RooRealVar* firstPOI = dynamic_cast<RooRealVar*>( paramsOfInterest.first());       
       if (firstPOI) initial_mu_value = firstPOI->getVal();
       //paramsOfInterest.getRealValue(firstPOI->GetName());
       if (fPrintLevel > 0) { 
            cout << "POIs: " << endl;
            paramsOfInterest.Print("v");
       }

       RooFit::MsgLevel msglevel = RooMsgService::instance().globalKillBelow();
       if (fPrintLevel < 3) RooMsgService::instance().setGlobalKillBelow(RooFit::FATAL);

       // simple
       Bool_t reuse=(fReuseNll || fgAlwaysReuseNll) ;
       
       Bool_t created(kFALSE) ;
       if (!reuse || fNll==0) {
          RooArgSet* allParams = fPdf->getParameters(data);
          RooStats::RemoveConstantParameters(allParams);

          // need to call constrain for RooSimultaneous until stripDisconnected problem fixed
          fNll = (RooNLLVar*) fPdf->createNLL(data, RooFit::CloneData(kFALSE),RooFit::Constrain(*allParams));

          created = kTRUE ;
          delete allParams;
          if (fPrintLevel > 1) cout << "creating NLL " << fNll << " with data = " << &data << endl ;
       }
       if (reuse && !created) {
          if (fPrintLevel > 1) cout << "reusing NLL " << fNll << " new data = " << &data << endl ;
          fNll->setData(data,kFALSE) ;
       }


       // make sure we set the variables attached to this nll
       RooArgSet* attachedSet = fNll->getVariables();

       *attachedSet = paramsOfInterest;
       RooArgSet* origAttachedSet = (RooArgSet*) attachedSet->snapshot();

       ///////////////////////////////////////////////////////////////////////
       // New profiling based on RooMinimizer (allows for Minuit2)
       // based on major speed increases seen by CMS for complex problems

 
       // other order
       // get the numerator
       RooArgSet* snap =  (RooArgSet*)paramsOfInterest.snapshot();

       tsw.Stop(); 
       double createTime = tsw.CpuTime();
       tsw.Start();

       // get the denominator
       double uncondML = 0;
       double fit_favored_mu = 0;
       int statusD = 0;
       if (type != 2) {
          // minimize and count eval errors
          fNll->clearEvalErrorLog();
          uncondML = GetMinNLL(statusD);
          int invalidNLLEval = fNll->numEvalErrors();

          // get best fit value for one-sided interval 
          if (firstPOI) fit_favored_mu = attachedSet->getRealValue(firstPOI->GetName()) ;
          
          // save this snapshot
          if( fDetailedOutputEnabled ) {
            if( fDetailedOutput ) delete fDetailedOutput;
            
            RooArgSet detOut( *attachedSet );
            RooStats::RemoveConstantParameters( &detOut );
            
            // monitor a few more variables
            fUncML->setVal( uncondML ); detOut.add( *fUncML );
            fFitStatus->setVal( statusD ); detOut.add( *fFitStatus );
            //fCovQual->setVal( covQual ); detOut.add( *fCovQual );
            fNumInvalidNLLEval->setVal( invalidNLLEval ); detOut.add( *fNumInvalidNLLEval );
            
            // store it
            fDetailedOutput = (const RooArgSet*)detOut.snapshot();
            
            cout << endl << "STORING THIS AS DETAILED OUTPUT:" << endl;
            fDetailedOutput->Print("v");
            cout << endl;
          }

       }
       tsw.Stop();
       double fitTime1  = tsw.CpuTime();
          
       //double ret = 0; 
       int statusN = 0;
       tsw.Start();

       double condML = 0; 

       bool doConditionalFit = (type != 1); 

       // skip the conditional ML (the numerator) only when fit value is smaller than test value
       if (!fSigned && type==0 &&
           ((fLimitType==oneSided          && fit_favored_mu >= initial_mu_value) ||
            (fLimitType==oneSidedDiscovery && fit_favored_mu <= initial_mu_value))) {
          doConditionalFit = false; 
          condML = uncondML;
       }

       if (doConditionalFit) {  


          //       cout <<" reestablish snapshot"<<endl;
          *attachedSet = *snap;

 
          // set the POI to constant
          RooLinkedListIter it = paramsOfInterest.iterator();
          RooRealVar* tmpPar = NULL, *tmpParA=NULL;
          while((tmpPar = (RooRealVar*)it.Next())){
             tmpParA =  dynamic_cast<RooRealVar*>( attachedSet->find(tmpPar->GetName()));
             if (tmpParA) tmpParA->setConstant();
          }


          // check if there are non-const parameters so it is worth to do the minimization
          RooArgSet allParams(*attachedSet); 
          RooStats::RemoveConstantParameters(&allParams);
          
          // in case no nuisance parameters are present
          // no need to minimize just evaluate the nll
          if (allParams.getSize() == 0 ) {
             condML = fNll->getVal(); 
          }
          else {              
             condML = GetMinNLL(statusN);
          }

       }

       tsw.Stop();
       double fitTime2 = tsw.CpuTime();

       double pll;
       if      (type == 1) pll = uncondML;
       else if (type == 2) pll = condML;
       else {
         pll = condML-uncondML;
         if (fSigned) {
           if (pll<0.0) pll = 0.0;   // bad fit
           if (fLimitType==oneSidedDiscovery ? (fit_favored_mu < initial_mu_value)
                                             : (fit_favored_mu > initial_mu_value))
             pll = -pll;
         }
       }

       if (fPrintLevel > 0) { 
          std::cout << "EvaluateProfileLikelihood - ";
          if (type <= 1)  
             std::cout << "mu hat = " << fit_favored_mu  <<  ", uncond ML = " << uncondML; 
          if (type != 1) 
             std::cout << ", cond ML = " << condML;
          if (type == 0)
             std::cout << " pll = " << pll;
          std::cout << " time (create/fit1/2) " << createTime << " , " << fitTime1 << " , " << fitTime2  
                    << std::endl;
       }


       // need to restore the values ?
       *attachedSet = *origAttachedSet;

       delete attachedSet;
       delete origAttachedSet;
       delete snap;

       if (!reuse) {
	 delete fNll;
	 fNll = 0; 
       }

       RooMsgService::instance().setGlobalKillBelow(msglevel);

       if(statusN!=0 || statusD!=0) {
	      return -1; // indicate failed fit (WVE is not used anywhere yet)
       }

       return pll;
             
     }     

double RooStats::ProfileLikelihoodTestStat::GetMinNLL(int& status) {
   //find minimum of NLL using RooMinimizer

   RooMinimizer minim(*fNll);
   minim.setStrategy(fStrategy);
   //LM: RooMinimizer.setPrintLevel has +1 offset - so subtruct  here -1 + an extra -1 
   int level = (fPrintLevel == 0) ? -1 : fPrintLevel -2;
   minim.setPrintLevel(level);
   minim.setEps(fTolerance);
   // this cayses a memory leak
   minim.optimizeConst(2); 
   TString minimizer = fMinimizer;
   TString algorithm = ROOT::Math::MinimizerOptions::DefaultMinimizerAlgo();
   if (algorithm == "Migrad") algorithm = "Minimize"; // prefer to use Minimize instead of Migrad
   for (int tries = 1, maxtries = 4; tries <= maxtries; ++tries) {
      status = minim.minimize(minimizer,algorithm);
      if (status%1000 == 0) {  // ignore erros from Improve 
         break;
      } else if (tries < maxtries) {
         cout << "    ----> Doing a re-scan first" << endl;
         minim.minimize(minimizer,"Scan");
         if (tries == 2) {
            if (fStrategy == 0 ) { 
               cout << "    ----> trying with strategy = 1" << endl;;
               minim.setStrategy(1);
            }
            else 
               tries++; // skip this trial if stratehy is already 1 
         }
         if (tries == 3) {
            cout << "    ----> trying with improve" << endl;;
            minimizer = "Minuit";
            algorithm = "migradimproved";
         }
      }
   }

   double val =  fNll->getVal();
   //minim.optimizeConst(false); 

   return val;
}
