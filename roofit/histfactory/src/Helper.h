// @(#)root/roostats:$Id:  cranmer $
// Author: Kyle Cranmer, Akira Shibata
/*************************************************************************
 * Copyright (C) 1995-2008, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOSTATS_HELPER
#define ROOSTATS_HELPER

#include <string>
#include <vector>
#include <map>

#include "TFile.h"

#include "RooStats/HistFactory/EstimateSummary.h"
#include "RooStats/HistFactory/Measurement.h"
#include "RooStats/HistFactory/HistFactoryException.h"
#include "RooWorkspace.h"



namespace RooStats{
namespace HistFactory{
  std::vector<EstimateSummary>*  loadSavedInputs(TFile* outFile, std::string channel );
  void saveInputs(TFile* outFile, std::string channel, std::vector<EstimateSummary> summaries);
  TH1 * GetHisto( TFile * inFile, const std::string name );
  TH1 * GetHisto( const std::string file, const std::string path, const std::string obj );
  bool AddSummaries( std::vector<EstimateSummary> & summary, std::vector<std::vector<EstimateSummary> > &master);
  std::vector<std::pair<std::string, std::string> > get_comb(std::vector<std::string> names);
  void AddSubStrings( std::vector<std::string> & vs, std::string s);

  std::vector<EstimateSummary> GetChannelEstimateSummaries(Measurement& measurement, Channel& channel);


}
}
#endif
