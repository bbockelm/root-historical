// @(#)root/roostats:$Id$
// Author: Kyle Cranmer, Lorenzo Moneta, Gregory Schott, Wouter Verkerke
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//_________________________________________________________________
/**
   HypoTestInverter class for performing an hypothesis test inversion by scanning the hypothesis test results of an 
  HypoTestCalculator  for various values of the parameter of interest. By looking at the confidence level curve of 
 the result  an upper limit, where it intersects the desired confidence level, can be derived.
 The class implements the RooStats::IntervalCalculator interface and returns an  RooStats::HypoTestInverterResult class.
 The result is a SimpleInterval, which via the method UpperLimit returns to the user the upper limit value.

The  HypoTestInverter implements various option for performing the scan. HypoTestInverter::RunFixedScan will scan using a fixed grid the parameter of interest. HypoTestInverter::RunAutoScan will perform an automatic scan to find optimally the curve and it will stop until the desired precision is obtained.
The confidence level value at a given point can be done via  HypoTestInverter::RunOnePoint.
The class can scan the CLs+b values or alternativly CLs (if the method HypoTestInverter::UseCLs has been called).


   Contributions to this class have been written by Giovanni Petrucciani and Annapaola Decosa
**/

// include other header files

#include "RooAbsData.h"
#
#include "TMath.h"

#include "RooStats/HybridResult.h"

#include "RooStats/HypoTestInverter.h"
#include <cassert>

#include "TF1.h"
#include "TLine.h"
#include "TCanvas.h"
#include "TGraphErrors.h"
#include "RooRealVar.h"
#include "RooArgSet.h"
#include "RooAbsPdf.h"
#include "RooRandom.h"
#include "RooConstVar.h"
#include "RooMsgService.h"
#include "RooStats/ModelConfig.h"
#include "RooStats/HybridCalculator.h"
#include "RooStats/FrequentistCalculator.h"
#include "RooStats/SimpleLikelihoodRatioTestStat.h"
#include "RooStats/RatioOfProfiledLikelihoodsTestStat.h"
#include "RooStats/ProfileLikelihoodTestStat.h"
#include "RooStats/ToyMCSampler.h"
#include "RooStats/HypoTestPlot.h"
#include "RooStats/HypoTestInverterPlot.h"



ClassImp(RooStats::HypoTestInverter)

using namespace RooStats;

// static variable definitions
double HypoTestInverter::fgCLAccuracy = 0.005;
unsigned int HypoTestInverter::fgNToys = 500;

double HypoTestInverter::fgAbsAccuracy = 0.05;
double HypoTestInverter::fgRelAccuracy = 0.05;
std::string HypoTestInverter::fgAlgo = "logSecant";


// helper class to wrap the functionality of the various HypoTestCalculators

template<class HypoTestType> 
struct HypoTestWrapper { 

   static void SetToys(HypoTestType * h, int toyNull, int toyAlt) { h->SetToys(toyNull,toyAlt); }
   
};

 
RooRealVar * HypoTestInverter::GetVariableToScan(const HypoTestCalculatorGeneric &hc) { 
    // get  the variable to scan 
   // try first with null model if not go to alternate model
   
   RooRealVar * varToScan = 0;
   const ModelConfig * mc = hc.GetNullModel();
   if (mc) { 
      const RooArgSet * poi  = mc->GetParametersOfInterest();
      if (poi) varToScan = dynamic_cast<RooRealVar*> (poi->first() );
   }
   if (!varToScan) { 
      mc = hc.GetAlternateModel();
      if (mc) { 
         const RooArgSet * poi  = mc->GetParametersOfInterest();
         if (poi) varToScan = dynamic_cast<RooRealVar*> (poi->first() );
      }
   }
   return varToScan;
}

void HypoTestInverter::CheckInputModels(const HypoTestCalculatorGeneric &hc,const RooRealVar & scanVariable) { 
    // check  the model given the given hypotestcalculator  
   const ModelConfig * modelSB = hc.GetNullModel();
   const ModelConfig * modelB = hc.GetAlternateModel();
   if (!modelSB || ! modelB) 
      oocoutF((TObject*)0,InputArguments) << "HypoTestInverter - model are not existing" << std::endl;    
   assert(modelSB && modelB);

   oocoutI((TObject*)0,InputArguments) << "HypoTestInverter ---- Input models: \n"
                                       << "\t\t using as S+B (null) model     : " 
                                       << modelSB->GetName() << "\n" 
                                       << "\t\t using as B (alternate) model  : " 
                                       << modelB->GetName() << "\n" << std::endl; 

   // check if scanVariable is included in B model pdf 
   RooAbsPdf * bPdf = modelB->GetPdf();
   const RooArgSet * bObs = modelB->GetObservables();
   if (!bPdf || !bObs) { 
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - B model has no pdf or observables defined" <<  std::endl; 
      return;
   }
   RooArgSet * bParams = bPdf->getParameters(*bObs);
   if (!bParams) { 
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - pdf of B model has no parameters" << std::endl; 
      return;
   }
   if (bParams->find(scanVariable.GetName() ) ) { 
      const RooArgSet * poiB  = modelB->GetSnapshot();
      if (!poiB ||  !poiB->find(scanVariable.GetName()) || 
          ( (RooRealVar*)  poiB->find(scanVariable.GetName()) )->getVal() != 0 )
         oocoutW((TObject*)0,InputArguments) << "HypoTestInverter - using a B model  with POI " 
                                             <<    scanVariable.GetName()  << " not equal to zero "
                                             << " user must check input model configurations " << endl;
   }
   delete bParams;
}

HypoTestInverter::HypoTestInverter( ) :
   fTotalToysRun(0),
   fMaxToys(0),
   fCalculator0(0),
   fScannedVariable(0),
   fResults(0),
   fUseCLs(false),
   fSize(0),
   fVerbose(0),
   fCalcType(kUndefined), 
   fNBins(0), fXmin(1), fXmax(1)
{
  // default constructor (doesn't do anything) 
}

HypoTestInverter::HypoTestInverter( HypoTestCalculatorGeneric& hc,
                                          RooRealVar* scannedVariable, double size ) :
   fTotalToysRun(0),
   fMaxToys(0),
   fCalculator0(0),
   fScannedVariable(scannedVariable), 
   fResults(0),
   fUseCLs(false),
   fSize(size),
   fVerbose(0),
   fCalcType(kUndefined), 
   fNBins(0), fXmin(1), fXmax(1)
{
   // get scanned variabke
   if (!fScannedVariable) { 
      fScannedVariable = HypoTestInverter::GetVariableToScan(hc);
   }
   if (!fScannedVariable) 
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - Cannot guess the variable to scan " << std::endl;    
   else 
      CheckInputModels(hc,*fScannedVariable);


   // constructor from a generic HypoTestInverter
   HybridCalculator * hybCalc = dynamic_cast<HybridCalculator*>(&hc);
   if (hybCalc) { 
      fCalcType = kHybrid;
      fCalculator0 = hybCalc;
      return;
   }
   FrequentistCalculator * freqCalc = dynamic_cast<FrequentistCalculator*>(&hc);
   if (freqCalc) { 
      fCalcType = kFrequentist;
      fCalculator0 = freqCalc;
      return;
   }
   oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - Type of hypotest calculator is not supported " << std::endl;    
}


HypoTestInverter::HypoTestInverter( HybridCalculator& hc,
                                          RooRealVar* scannedVariable, double size ) :
   fTotalToysRun(0),
   fMaxToys(0),
   fCalculator0(&hc),
   fScannedVariable(scannedVariable), 
   fResults(0),
   fUseCLs(false),
   fSize(size),
   fVerbose(0),
   fCalcType(kHybrid), 
   fNBins(0), fXmin(1), fXmax(1)
{
   // constructor from a reference to an HybridCalculator
   // get scanned variabke
   if (!fScannedVariable) { 
      fScannedVariable = GetVariableToScan(hc);
   }
   if (!fScannedVariable) 
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - Cannot guess the variable to scan " << std::endl;    
   else 
      CheckInputModels(hc,*fScannedVariable);

}




HypoTestInverter::HypoTestInverter( FrequentistCalculator& hc,
                                          RooRealVar* scannedVariable, double size ) :
   fTotalToysRun(0),
   fMaxToys(0),
   fCalculator0(&hc),
   fScannedVariable(scannedVariable), 
   fResults(0),
   fUseCLs(false),
   fSize(size),
   fVerbose(0),
   fCalcType(kFrequentist), 
   fNBins(0), fXmin(1), fXmax(1)
{
   // constructor from a reference to a FrequentistCalculator 
   // get scanned variabke
   if (!fScannedVariable) { 
      fScannedVariable = GetVariableToScan(hc);
   }
   if (!fScannedVariable) 
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - Cannot guess the variable to scan " << std::endl;    
   else 
      CheckInputModels(hc,*fScannedVariable);
}


//_________________________________________________________________________________________________
HypoTestInverter::HypoTestInverter( RooAbsData& data, ModelConfig &bModel, ModelConfig &sbModel,
				    RooRealVar * scannedVariable,  ECalculatorType type, double size) :
   fTotalToysRun(0),
   fMaxToys(0),
   fCalculator0(0),
   fScannedVariable(scannedVariable), 
   fResults(0),
   fUseCLs(false),
   fSize(size),
   fVerbose(0),
   fCalcType(type), 
   fNBins(0), fXmin(1), fXmax(1)

{
   // Constructor from a model for B model and a model for S+B. 
   // An HypoTestCalculator (Hybrid of Frequentis) will be ccreated using the 
   // S+B model as the null and the B model as the alternate
  if(fCalcType==kFrequentist) fHC = auto_ptr<HypoTestCalculatorGeneric>(new FrequentistCalculator(data, sbModel, bModel)); 
  if(fCalcType==kHybrid) fHC = auto_ptr<HypoTestCalculatorGeneric>(new HybridCalculator(data, sbModel, bModel)); 
  fCalculator0 = fHC.get();
   // get scanned variabke
   if (!fScannedVariable) { 
      fScannedVariable = GetVariableToScan(*fCalculator0);
   }
   if (!fScannedVariable) 
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter - Cannot guess the variable to scan " << std::endl;    
   else
      CheckInputModels(*fCalculator0,*fScannedVariable);

}

HypoTestInverter::~HypoTestInverter()
{
  // destructor
  
  // delete the HypoTestInverterResult
  if (fResults) delete fResults;
  fCalculator0 = 0;
}


TestStatistic * HypoTestInverter::GetTestStatistic( ) const
{
  if(fCalculator0 &&  fCalculator0->GetTestStatSampler()){
     return fCalculator0->GetTestStatSampler()->GetTestStatistic();
  }
  else 
     return 0;
} 

bool HypoTestInverter::SetTestStatistic(TestStatistic& stat)
{
  if(fCalculator0 &&  fCalculator0->GetTestStatSampler()){
    fCalculator0->GetTestStatSampler()->SetTestStatistic(&stat);
    return true; 
  }
  else return false;
} 
void  HypoTestInverter::Clear()  { 
   // delete contained result and graph
   if (fResults) delete fResults; 
   fResults = 0;
   if (fLimitPlot.get()) fLimitPlot = std::auto_ptr<TGraphErrors>();
}   

void  HypoTestInverter::CreateResults() const { 
  // create a new HypoTestInverterResult to hold all computed results
   if (fResults == 0) {
      TString results_name = "result_";
      results_name += fScannedVariable->GetName();
      fResults = new HypoTestInverterResult(results_name,*fScannedVariable,ConfidenceLevel());
      TString title = "HypoTestInverter Result For ";
      title += fScannedVariable->GetName();
      fResults->SetTitle(title);
   }
   fResults->UseCLs(fUseCLs);
   fResults->SetConfidenceLevel(1.-fSize);
}

HypoTestInverterResult* HypoTestInverter::GetInterval() const { 
   // run a fixed scan or the automatic scan 
   // return a copy of the result object which will be managed by the user

   // if having a result with more thon one point return it
   if (fResults && fResults->ArraySize() > 1) return (HypoTestInverterResult*)(fResults->Clone());

   if (fNBins > 0) {
      bool ret = RunFixedScan(fNBins, fXmin, fXmax); 
      if (!ret) 
         oocoutE((TObject*)0,Eval) << "HypoTestInverter::GetInterval - error running a fixed scan " << std::endl;    
   }
   else { 
      double limit(0),err(0);
      bool ret = RunLimit(limit,err);
      if (!ret) 
         oocoutE((TObject*)0,Eval) << "HypoTestInverter::GetInterval - error running an auto scan " << std::endl;    
   }
   return (HypoTestInverterResult*)(fResults->Clone());
}



HypoTestResult * HypoTestInverter::Eval(HypoTestCalculatorGeneric &hc, bool adaptive, double clsTarget) const {

   //for debug
   // std::cout << ">>>>>>>>>>> " << std::endl;
   // std::cout << "alternate model " << std::endl;
   // hc.GetAlternateModel()->GetNuisanceParameters()->Print("V");
   // hc.GetAlternateModel()->GetParametersOfInterest()->Print("V");
   // std::cout << "Null model " << std::endl;
   // hc.GetNullModel()->GetNuisanceParameters()->Print("V");
   // hc.GetNullModel()->GetParametersOfInterest()->Print("V");
   // std::cout << "<<<<<<<<<<<<<<< " << std::endl;

   // run the hypothesis test 
   HypoTestResult *  hcResult = hc.GetHypoTest();
   if (hcResult == 0) {
      oocoutE((TObject*)0,Eval) << "HypoTestInverter::Eval - HypoTest failed" << std::endl;
      return hcResult; 
   }

   // since the b model is the alt need to set the flag
   hcResult->SetBackgroundAsAlt(true);

   // need to flip p-values for PL test statistic 

   bool flipPValues = false;
   //TestStatistic * testStat = hc.GetTestStatSampler()->GetTestStatistic();
//   if ( dynamic_cast<ProfileLikelihoodTestStat*>(testStat)  ) { 
      // I need to flip the P-values
      // flipPValues = true;
      // hcResult->SetPValueIsRightTail(!hcResult->GetPValueIsRightTail());
   //  hcResult->SetTestStatisticData(hcResult->GetTestStatisticData()+1e-9); // issue with < vs <= in discrete models
//   }
   // else {
   // //  hcResult->SetTestStatisticData(hcResult->GetTestStatisticData()+1e-9); // issue with < vs <= in discrete models
   // }

   double clsMid    = (fUseCLs ? hcResult->CLs()      : hcResult->CLsplusb());
   double clsMidErr = (fUseCLs ? hcResult->CLsError() : hcResult->CLsplusbError());

   //if (fVerbose) std::cout << (fUseCLs ? "\tCLs = " : "\tCLsplusb = ") << clsMid << " +/- " << clsMidErr << std::endl;
   
   if (adaptive) {
 
      if (fCalcType == kHybrid) HypoTestWrapper<HybridCalculator>::SetToys((HybridCalculator*)&hc, fUseCLs ? fgNToys : 1, 4*fgNToys);
      if (fCalcType == kFrequentist) HypoTestWrapper<FrequentistCalculator>::SetToys((FrequentistCalculator*)&hc, fUseCLs ? fgNToys : 1, 4*fgNToys);

   while (clsMidErr >= fgCLAccuracy && (clsTarget == -1 || fabs(clsMid-clsTarget) < 3*clsMidErr) ) {
      std::auto_ptr<HypoTestResult> more(hc.GetHypoTest());
      
      if (flipPValues)
         more->SetPValueIsRightTail(!more->GetPValueIsRightTail());

      hcResult->Append(more.get());
      clsMid    = (fUseCLs ? hcResult->CLs()      : hcResult->CLsplusb());
      clsMidErr = (fUseCLs ? hcResult->CLsError() : hcResult->CLsplusbError());
      if (fVerbose) std::cout << (fUseCLs ? "\tCLs = " : "\tCLsplusb = ") << clsMid << " +/- " << clsMidErr << std::endl;
   }

   }
   if (fVerbose ) {
      oocoutP((TObject*)0,Eval) << "P values for  " << fScannedVariable->GetName()  << " =  " <<
         fScannedVariable->getVal() << "\n" <<
         "\tCLs      = " << hcResult->CLs()      << " +/- " << hcResult->CLsError()      << "\n" <<
         "\tCLb      = " << hcResult->CLb()      << " +/- " << hcResult->CLbError()      << "\n" <<
         "\tCLsplusb = " << hcResult->CLsplusb() << " +/- " << hcResult->CLsplusbError() << "\n" <<
         std::endl;
   }
   
   fTotalToysRun += (hcResult->GetAltDistribution()->GetSize() + hcResult->GetNullDistribution()->GetSize());


   return hcResult;
} 


bool HypoTestInverter::RunFixedScan( int nBins, double xMin, double xMax ) const
{
   // Run a Fixed scan in npoints between min and max

   CreateResults();
   // interpolate the limits
   fResults->fFittedLowerLimit = false; 
   fResults->fFittedUpperLimit = false; 

  // safety checks
  if ( nBins<=0 ) {
    std::cout << "Please provide nBins>0\n";
    return false;
  }
  if ( nBins==1 && xMin!=xMax ) {
    std::cout << "nBins==1 -> I will run for xMin (" << xMin << ")\n";
  }
  if ( xMin==xMax && nBins>1 ) { 
    std::cout << "xMin==xMax -> I will enforce nBins==1\n";
    nBins = 1;
  }
  if ( xMin>xMax ) {
    std::cout << "Please provide xMin (" << xMin << ") smaller that xMax (" << xMax << ")\n";
    return false;
  } 
  
  for (int i=0; i<nBins; i++) {
    double thisX = xMin+i*(xMax-xMin)/(nBins-1);
    bool status = RunOnePoint(thisX);
    
    // check if failed status
    if ( status==false ) {
      std::cout << "\t\tLoop interupted because of failed status\n";
      return false;
    }
  }

  return true;
}


bool HypoTestInverter::RunOnePoint( double rVal, bool adaptive, double clTarget) const
{
   // run only one point at the given value

   CreateResults();

   // check if rVal is in the range specified for fScannedVariable
   if ( rVal<fScannedVariable->getMin() ) {
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter::RunOnePoint - Out of range: using the lower bound " 
                                          << fScannedVariable->getMin() 
                                          << " on the scanned variable rather than " << rVal<< "\n";
     rVal = fScannedVariable->getMin();
   }
   if ( rVal>fScannedVariable->getMax() ) {
      oocoutE((TObject*)0,InputArguments) << "HypoTestInverter::RunOnePoint - Out of range: using the upper bound " 
                                          << fScannedVariable->getMax() 
                                          << " on the scanned variable rather than " << rVal<< "\n";
     rVal = fScannedVariable->getMax();
   }

   // save old value 
   double oldValue = fScannedVariable->getVal();

   // evaluate hybrid calculator at a single point
   fScannedVariable->setVal(rVal);
   // need to set value of rval in hybridcalculator
   // assume null model is S+B and alternate is B only
   const ModelConfig * sbModel = fCalculator0->GetNullModel();
   RooArgSet poi; poi.add(*sbModel->GetParametersOfInterest());
   // set poi to right values 
   poi = RooArgSet(*fScannedVariable);
   const_cast<ModelConfig*>(sbModel)->SetSnapshot(poi);

   if (fVerbose > 0) 
      oocoutP((TObject*)0,Eval) << "Running for " << fScannedVariable->GetName() << " = " << fScannedVariable->getVal() << endl;
   
   // compute the results
   HypoTestResult* result =   Eval(*fCalculator0,adaptive,clTarget);
   if (!result) { 
      oocoutE((TObject*)0,Eval) << "HypoTestInverter - Error running point " << fScannedVariable->GetName() << " = " <<
   fScannedVariable->getVal() << endl;
      return false;
   }
   
   double lastXtested;
   if ( fResults->ArraySize()!=0 ) lastXtested = fResults->GetXValue(fResults->ArraySize()-1);
   else lastXtested = -999;

   if ( lastXtested==rVal ) {
     
     std::cout << "Merge with previous result\n";
     HypoTestResult* prevResult =  fResults->GetResult(fResults->ArraySize()-1);
     prevResult->Append(result);
     delete result; // t.b.c

   } else {
     
     // fill the results in the HypoTestInverterResult array
     fResults->fXValues.push_back(rVal);
     fResults->fYObjects.Add(result);

   }

      // std::cout << "computed value for poi  " << rVal  << " : " << fResults->GetYValue(fResults->ArraySize()-1) 
      //        << " +/- " << fResults->GetYError(fResults->ArraySize()-1) << endl;

   fScannedVariable->setVal(oldValue);
   
   return true;
}



bool HypoTestInverter::RunLimit(double &limit, double &limitErr, double absAccuracy, double relAccuracy, const double*hint) const {

// bool HybridNew::runLimit(RooWorkspace *w, RooStats::ModelConfig *mc_s, RooStats::ModelConfig *mc_b, RooAbsData &data, double &limit, double &limitErr, const double *hint) {

   RooRealVar *r = fScannedVariable; 
   r->setConstant(true);
   //w->loadSnapshot("clean");

  if ((hint != 0) && (*hint > r->getMin())) {
     r->setMax(std::min<double>(3.0 * (*hint), r->getMax()));
     r->setMin(std::max<double>(0.3 * (*hint), r->getMin()));
     oocoutI((TObject*)0,InputArguments) << "HypoTestInverter::RunLimit - Use hint value " << *hint 
                                         << " search in interval " << r->getMin() << " , " << r->getMax() << std::endl;
  }

  // if not specified use the default values for rel and absolute accuracy
  if (absAccuracy <= 0) absAccuracy = fgAbsAccuracy;
  if (relAccuracy <= 0) relAccuracy = fgRelAccuracy;  

  typedef std::pair<double,double> CLs_t;
  double clsTarget = fSize; 
  CLs_t clsMin(1,0), clsMax(0,0), clsMid(0,0);
  double rMin = r->getMin(), rMax = r->getMax(); 
  limit    = 0.5*(rMax + rMin);
  limitErr = 0.5*(rMax - rMin);
  bool done = false;

  TF1 expoFit("expoFit","[0]*exp([1]*(x-[2]))", rMin, rMax);

  // if (readHybridResults_) { 
  //     if (verbose > 0) std::cout << "Search for upper limit using pre-computed grid of p-values" << std::endl;

  //     readAllToysFromFile(); 
  //     double minDist=1e3;
  //     for (int i = 0, n = limitPlot_->GetN(); i < n; ++i) {
  //         double x = limitPlot_->GetX()[i], y = limitPlot_->GetY()[i], ey = limitPlot_->GetErrorY(i);
  //         if (verbose > 0) std::cout << "  r " << x << (CLs_ ? ", CLs = " : ", CLsplusb = ") << y << " +/- " << ey << std::endl;
  //         if (y-3*ey >= clsTarget) { rMin = x; clsMin = CLs_t(y,ey); }
  //         if (y+3*ey <= clsTarget) { rMax = x; clsMax = CLs_t(y,ey); }
  //         if (fabs(y-clsTarget) < minDist) { limit = x; minDist = fabs(y-clsTarget); }
  //     }
  //     if (verbose > 0) std::cout << " after scan x ~ " << limit << ", bounds [ " << rMin << ", " << rMax << "]" << std::endl;
  //     limitErr = std::max(limit-rMin, rMax-limit);
  //     expoFit.SetRange(rMin,rMax);

  //     if (limitErr < std::max(rAbsAccuracy_, rRelAccuracy_ * limit)) {
  //         if (verbose > 1) std::cout << "  reached accuracy " << limitErr << " below " << std::max(rAbsAccuracy_, rRelAccuracy_ * limit) << std::endl;
  //         done = true; 
  //     }
  // } else {

  fLimitPlot.reset(new TGraphErrors());

  if (fVerbose > 0) std::cout << "Search for upper limit to the limit" << std::endl;
  for (int tries = 0; tries < 6; ++tries) {
     //clsMax = eval(w, mc_s, mc_b, data, rMax);
     if (! RunOnePoint(rMax) ) { 
        oocoutE((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Hypotest failed" << std::endl;
        return false;
     }
     clsMax = std::make_pair( fResults->GetLastYValue(), fResults->GetLastYError() );
     if (clsMax.first == 0 || clsMax.first + 3 * fabs(clsMax.second) < clsTarget ) break;
     rMax += rMax;
     if (tries == 5) { 
        oocoutE((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Cannot set higher limit: at " << r->GetName() 
                                  << " = " << rMax  << " still get " 
                                  << (fUseCLs ? "CLs" : "CLsplusb") << " = " << clsMax.first << std::endl;
        return false;
     }
  }
  if (fVerbose > 0) { 
     oocoutI((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Search for lower limit to the limit" << std::endl;
  }
  //clsMin = (fUseCLs && rMin == 0 ? CLs_t(1,0) : eval(w, mc_s, mc_b, data, rMin));
  if ( fUseCLs && rMin == 0 ) { 
     clsMin =  CLs_t(1,0); 
  }
  else { 
     if (! RunOnePoint(rMin) ) return false;
     clsMin = std::make_pair( fResults->GetLastYValue(), fResults->GetLastYError() );
  }
  if (clsMin.first != 1 && clsMin.first - 3 * fabs(clsMin.second) < clsTarget) {
     if (fUseCLs) { 
        rMin = 0;
        clsMin = CLs_t(1,0); // this is always true for CLs
     } else {
        rMin = -rMax / 4;
        for (int tries = 0; tries < 6; ++tries) {
           if (! RunOnePoint(rMax) ) return false;
           clsMin = std::make_pair( fResults->GetLastYValue(), fResults->GetLastYError() );
           if (clsMin.first == 1 || clsMin.first - 3 * fabs(clsMin.second) > clsTarget) break;
           rMin += rMin;
           if (tries == 5) { 
              oocoutE((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Cannot set lower limit: at " << r->GetName() 
                                        << " = " << rMin << " still get " << (fUseCLs ? "CLs" : "CLsplusb") 
                                        << " = " << clsMin.first << std::endl;
              return false;
           }
        }
     }
  }

  if (fVerbose > 0) 
      oocoutI((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Now doing proper bracketing & bisection" << std::endl;
  do {

     // break loop in case max toys is reached
     if (fMaxToys > 0 && fTotalToysRun > fMaxToys ) { 
        oocoutW((TObject*)0,Eval) << "HypoTestInverter::RunLimit - maximum number of toys reached  " << std::endl;
        done = false; break;
     }


     // determine point by bisection or interpolation
     limit = 0.5*(rMin+rMax); limitErr = 0.5*(rMax-rMin);
     if (fgAlgo == "logSecant" && clsMax.first != 0) {
        double logMin = log(clsMin.first), logMax = log(clsMax.first), logTarget = log(clsTarget);
        limit = rMin + (rMax-rMin) * (logTarget - logMin)/(logMax - logMin);
        if (clsMax.second != 0 && clsMin.second != 0) {
           limitErr = hypot((logTarget-logMax) * (clsMin.second/clsMin.first), (logTarget-logMin) * (clsMax.second/clsMax.first));
           limitErr *= (rMax-rMin)/((logMax-logMin)*(logMax-logMin));
        }
     }
     r->setError(limitErr);

     // exit if reached accuracy on r 
     if (limitErr < std::max(absAccuracy, relAccuracy * limit)) {
        if (fVerbose > 1) 
            oocoutI((TObject*)0,Eval) << "HypoTestInverter::RunLimit - reached accuracy " << limitErr 
                                      << " below " << std::max(absAccuracy, relAccuracy * limit)  << std::endl;
        done = true; break;
     }
     
     // evaluate point 
     
     //clsMid = eval(w, mc_s, mc_b, data, limit, true, clsTarget);
     if (! RunOnePoint(limit, true, clsTarget) ) return false;
     clsMid = std::make_pair( fResults->GetLastYValue(), fResults->GetLastYError() );

     if (clsMid.second == -1) {
        std::cerr << "Hypotest failed" << std::endl;
        return false;
     }

     // if sufficiently far away, drop one of the points
     if (fabs(clsMid.first-clsTarget) >= 2*clsMid.second) {
        if ((clsMid.first>clsTarget) == (clsMax.first>clsTarget)) {
           rMax = limit; clsMax = clsMid;
        } else {
                  rMin = limit; clsMin = clsMid;
              }
          } else {
              if (fVerbose > 0) std::cout << "Trying to move the interval edges closer" << std::endl;
              double rMinBound = rMin, rMaxBound = rMax;
              // try to reduce the size of the interval 
              while (clsMin.second == 0 || fabs(rMin-limit) > std::max(absAccuracy, relAccuracy * limit)) {
                  rMin = 0.5*(rMin+limit); 
                  if (!RunOnePoint(rMin,true, clsTarget) ) return false;
                  clsMin = std::make_pair( fResults->GetLastYValue(), fResults->GetLastYError() );
                  //clsMin = eval(w, mc_s, mc_b, data, rMin, true, clsTarget); 
                  if (fabs(clsMin.first-clsTarget) <= 2*clsMin.second) break;
                  rMinBound = rMin;
              } 
              while (clsMax.second == 0 || fabs(rMax-limit) > std::max(absAccuracy, relAccuracy * limit)) {
                  rMax = 0.5*(rMax+limit); 
//                  clsMax = eval(w, mc_s, mc_b, data, rMax, true, clsTarget); 
                  if (!RunOnePoint(rMax,true,clsTarget) ) return false;
                  clsMax = std::make_pair( fResults->GetLastYValue(), fResults->GetLastYError() );
                  if (fabs(clsMax.first-clsTarget) <= 2*clsMax.second) break;
                  rMaxBound = rMax;
              } 
              expoFit.SetRange(rMinBound,rMaxBound);
              break;
          }
  } while (true);
  
  if (!done) { // didn't reach accuracy with scan, now do fit
      if (fVerbose) {
         oocoutI((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Before fit   --- \n"; 
         std::cout << "Limit: " << r->GetName() << " < " << limit << " +/- " << limitErr << " [" << rMin << ", " << rMax << "]\n";
      }
      
      expoFit.FixParameter(0,clsTarget);
      expoFit.SetParameter(1,log(clsMax.first/clsMin.first)/(rMax-rMin));
      expoFit.SetParameter(2,limit);
      double rMinBound, rMaxBound; expoFit.GetRange(rMinBound, rMaxBound);
      limitErr = std::max(fabs(rMinBound-limit), fabs(rMaxBound-limit));
      int npoints = 0; 
      
      HypoTestInverterPlot plot("plot","plot",fResults);
      fLimitPlot = std::auto_ptr<TGraphErrors>(plot.MakePlot() );

      
      for (int j = 0; j < fLimitPlot->GetN(); ++j) { 
         if (fLimitPlot->GetX()[j] >= rMinBound && fLimitPlot->GetX()[j] <= rMaxBound) npoints++; 
      }
      for (int i = 0, imax = /*(readHybridResults_ ? 0 : */  8; i <= imax; ++i, ++npoints) {
          fLimitPlot->Sort();
          fLimitPlot->Fit(&expoFit,(fVerbose <= 1 ? "QNR EX0" : "NR EXO"));
          if (fVerbose) {
               oocoutI((TObject*)0,Eval) << "Fit to " << npoints << " points: " << expoFit.GetParameter(2) << " +/- " << expoFit.GetParError(2) << std::endl;
          }
          if ((rMin < expoFit.GetParameter(2))  && (expoFit.GetParameter(2) < rMax) && (expoFit.GetParError(2) < 0.5*(rMaxBound-rMinBound))) { 
              // sanity check fit result
              limit = expoFit.GetParameter(2);
              limitErr = expoFit.GetParError(2);
              if (limitErr < std::max(absAccuracy, relAccuracy * limit)) break;
          }
          // add one point in the interval. 
          double rTry = RooRandom::uniform()*(rMaxBound-rMinBound)+rMinBound; 
          if (i != imax) { 
             if (!RunOnePoint(rTry,true,clsTarget) ) return false;
             //eval(w, mc_s, mc_b, data, rTry, true, clsTarget);
          }

      } 
  }

//if (!plot_.empty() && fLimitPlot.get()) {
  if (fLimitPlot.get() && fLimitPlot->GetN() > 0) {
       //new TCanvas("c1","c1");
      fLimitPlot->Sort();
      fLimitPlot->SetLineWidth(2);
      double xmin = r->getMin(), xmax = r->getMax();
      for (int j = 0; j < fLimitPlot->GetN(); ++j) {
        if (fLimitPlot->GetY()[j] > 1.4*clsTarget || fLimitPlot->GetY()[j] < 0.6*clsTarget) continue;
        xmin = std::min(fLimitPlot->GetX()[j], xmin);
        xmax = std::max(fLimitPlot->GetX()[j], xmax);
      }
      fLimitPlot->GetXaxis()->SetRangeUser(xmin,xmax);
      fLimitPlot->GetYaxis()->SetRangeUser(0.5*clsTarget, 1.5*clsTarget);
      fLimitPlot->Draw("AP");
      expoFit.Draw("SAME");
      TLine line(fLimitPlot->GetX()[0], clsTarget, fLimitPlot->GetX()[fLimitPlot->GetN()-1], clsTarget);
      line.SetLineColor(kRed); line.SetLineWidth(2); line.Draw();
      line.DrawLine(limit, 0, limit, fLimitPlot->GetY()[0]);
      line.SetLineWidth(1); line.SetLineStyle(2);
      line.DrawLine(limit-limitErr, 0, limit-limitErr, fLimitPlot->GetY()[0]);
      line.DrawLine(limit+limitErr, 0, limit+limitErr, fLimitPlot->GetY()[0]);
      //c1->Print(plot_.c_str());
  }

  oocoutI((TObject*)0,Eval) << "HypoTestInverter::RunLimit - Result:    \n" 
                            << "\tLimit: " << r->GetName() << " < " << limit << " +/- " << limitErr << " @ " << (1-fSize) * 100 << "% CL\n";
  if (fVerbose > 1) oocoutI((TObject*)0,Eval) << "Total toys: " << fTotalToysRun << std::endl;

  // set value in results
  fResults->fUpperLimit = limit; 
  fResults->fUpperLimitError = limitErr;
  fResults->fFittedUpperLimit = true;
  // lower limit are always min of p value
  fResults->fLowerLimit = fScannedVariable->getMin(); 
  fResults->fLowerLimitError = 0;
  fResults->fFittedLowerLimit = true; 

  return true;
}

SamplingDistribution * HypoTestInverter::GetLowerLimitDistribution(bool rebuild) {
   // get the distribution of lower limit
   // if rebuild = false (default) it will re-use the results of the scan done 
   // for obtained the observed limit and no extra toys will be generated
   // if rebuild a new set of B toys will be done and the procedure will be repeted
   // for each toy
   if (!rebuild) {
      if (!fResults) { 
         oocoutE((TObject*)0,InputArguments) << "HypoTestInverter::GetLowerLimitDistribution(false) - result not existing\n";
         return 0;
      }
      return fResults->GetLowerLimitDistribution(); 
   }
   oocoutE((TObject*)0,InputArguments) << "HypoTestInverter::GetLowerLimitDistribution(true) - rebuild option not yet implemented\n";
   return 0;
}

SamplingDistribution * HypoTestInverter::GetUpperLimitDistribution(bool rebuild) {
   // get the distribution of lower limit
   // if rebuild = false (default) it will re-use the results of the scan done 
   // for obtained the observed limit and no extra toys will be generated
   // if rebuild a new set of B toys will be done and the procedure will be repeted
   // for each toy
   if (!rebuild) {
      if (!fResults) { 
         oocoutE((TObject*)0,InputArguments) << "HypoTestInverter::GetUpperLimitDistribution(false) - result not existing\n";
         return 0;
      }
      return fResults->GetUpperLimitDistribution(); 
   }
   oocoutE((TObject*)0,InputArguments) << "HypoTestInverter::GetUpperLimitDistribution(true) - rebuild option not yet implemented\n";
   return 0;
}
