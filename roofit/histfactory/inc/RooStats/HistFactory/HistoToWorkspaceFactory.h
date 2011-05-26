// @(#)root/roostats:$Id:  cranmer $
// Author: Kyle Cranmer, Akira Shibata
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOSTATS_HISTOTOWORKSPACEFACTORY
#define ROOSTATS_HISTOTOWORKSPACEFACTORY

#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <sstream>

#include <RooPlot.h>
#include <RooArgSet.h>
#include <RooFitResult.h>
#include <RooAbsReal.h>
#include <RooRealVar.h>
#include <RooWorkspace.h>
#include <TObject.h>
#include <TH1F.h>
#include <TDirectory.h>


namespace RooStats{
namespace HistFactory{

  struct EstimateSummary;
   
  class HistoToWorkspaceFactory: public TObject {

    public:

    HistoToWorkspaceFactory(  string, string , vector<string> , double =200, double =20, int =0, int =6, TFile * =0);
      HistoToWorkspaceFactory();
      virtual ~HistoToWorkspaceFactory();

      void AddEfficiencyTerms(RooWorkspace* proto, string prefix, string interpName,
            map<string,pair<double,double> > systMap,
            vector<string>& likelihoodTermNames, vector<string>& totSystTermNames);

      string AddNormFactor(RooWorkspace *, string & , string & , EstimateSummary & , bool );

      void AddMultiVarGaussConstraint(RooWorkspace* proto, string prefix,int lowBin, int highBin, vector<string>& likelihoodTermNames);

      void AddPoissonTerms(RooWorkspace* proto, string prefix, string obsPrefix, string expPrefix, int lowBin, int highBin,
               vector<string>& likelihoodTermNames);

      //void Combine_old();

      RooWorkspace *  MakeCombinedModel(vector<string>, vector<RooWorkspace*>);

      //void Combine_ratio(vector<string> , vector<RooWorkspace*>);

      void Customize(RooWorkspace* proto, const char* pdfNameChar, map<string,string> renameMap);

      void EditSyst(RooWorkspace* proto, const char* pdfNameChar, map<string,double> gammaSyst, map<string,double> uniformSyst, map<string,double> logNormSyst);

      void FormatFrameForLikelihood(RooPlot* frame, string XTitle=string("#sigma / #sigma_{SM}"), string YTitle=string("-log likelihood"));


      void LinInterpWithConstraint(RooWorkspace* proto, TH1F* nominal, vector<TH1F*> lowHist, vector<TH1F*> highHist,
                 vector<string> sourceName, string prefix, string productPrefix, string systTerm,
                 int lowBin, int highBin, vector<string>& likelihoodTermNames);

      TDirectory* Makedirs( TDirectory* file, vector<string> names );

      RooWorkspace* MakeSingleChannelModel(vector<RooStats::HistFactory::EstimateSummary> summary, vector<string> systToFix, bool doRatio=false);

      void  MakeTotalExpected(RooWorkspace* proto, string totName, string /**/, string /**/,
            int lowBin, int highBin, vector<string>& syst_x_expectedPrefixNames,
            vector<string>& normByNames);

      TDirectory* Mkdir( TDirectory * file, string name );

      void PrintCovarianceMatrix(RooFitResult* result, RooArgSet* params, string filename);
      void ProcessExpectedHisto(TH1F* hist,RooWorkspace* proto, string prefix, string productPrefix, string systTerm, double low, double high, int lowBin, int highBin);
      void SetObsToExpected(RooWorkspace* proto, string obsPrefix, string expPrefix, int lowBin, int highBin);
      void FitModel(RooWorkspace *, string, string, string, bool=false  );
      std::string FilePrefixStr(std::string);

      string fFileNamePrefix;
      string fRowTitle;
      vector<string> fSystToFix;
     double fNomLumi, fLumiError;
      int  fLowBin, fHighBin;    
      std::stringstream fResultsPrefixStr;
      TFile * fOut_f;
      FILE * pFile;

      ClassDef(RooStats::HistFactory::HistoToWorkspaceFactory,1)
  };

}
}

#endif
