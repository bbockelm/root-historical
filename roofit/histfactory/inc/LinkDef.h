

#ifdef __CINT__
#pragma link off all globals;
#pragma link off all classes;
#pragma link off all functions;

#pragma link C++ class PiecewiseInterpolation- ;
#pragma link C++ class ParamHistFunc+ ;
#pragma link C++ class RooStats::HistFactory::LinInterpVar+ ;
#pragma link C++ class RooStats::HistFactory::FlexibleInterpVar+ ;
#pragma link C++ class RooStats::HistFactory::EstimateSummary+ ;
#pragma link C++ class RooStats::HistFactory::HistoToWorkspaceFactory+ ;
#pragma link C++ class RooStats::HistFactory::HistoToWorkspaceFactoryFast+ ;
#pragma link C++ class RooStats::HistFactory::RooBarlowBeestonLL+ ;  
#pragma link C++ class RooStats::HistFactory::HistFactorySimultaneous+ ;  

#pragma link C++ class RooStats::HistFactory::ConfigParser+ ;

#pragma link C++ class RooStats::HistFactory::Measurement+ ;
#pragma link C++ class RooStats::HistFactory::Channel+ ;
#pragma link C++ class RooStats::HistFactory::Sample+ ;
#pragma link C++ class RooStats::HistFactory::Data+ ;

#pragma link C++ class RooStats::HistFactory::StatError+ ;
#pragma link C++ class RooStats::HistFactory::StatErrorConfig+ ;
#pragma link C++ class RooStats::HistFactory::PreprocessFunction+ ;


#pragma link C++ class std::vector< RooStats::HistFactory::Channel >+ ;
#pragma link C++ class std::vector< RooStats::HistFactory::Sample >+ ;

#pragma link C++ defined_in "include/RooStats/HistFactory/MakeModelAndMeasurementsFast.h"; 
#pragma link C++ defined_in "include/RooStats/HistFactory/Systematics.h"; 
#pragma link C++ defined_in "include/RooStats/HistFactory/HistFactoryModelUtils.h"; 


#endif
