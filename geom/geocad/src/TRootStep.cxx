#define HAVE_LIMITS_H
#define HAVE_IOSTREAM 
#define HAVE_IOMANIP

//////////////////////////////////////////////////////////////////////////////////
// TRootStep Class                                                              //
// --------------------                                                         //
//                                                                              //
// This class is an interface to convert ROOT's geometry file                   //
// to STEP file. The TRootStep Class takes a gGeoManager pointer and gives      //
// back a STEP file. gGeoManager is the instance of TGeoManager class           //
// containing tree of geometries creating resulting geometry.                   //
// Standard for the Exchange of Product model data (STEP) is an international   //
// standard for the exchange of industrial product data. It is typically used   //
// to exchange data between various CAD, CAM and CAE applications.              // 
// TRootStep Class is using RootOCC class to translate the root geometry        //
// in the corresponding OpenCascade geometry and  and OCCStep to write the      //
// OpenCascade geometry to the step File.                                       //
// OpenCascade Technology (OCC) is a software development platform freely       //
// available in open source. It includes C++ components for 3D surface and      //
// solid modeling,visualization, data exchange and rapid application            //
// development. For more information about OCC see http://www.opencascade.org   //
// Each object in ROOT is represented by an OCC TopoDS_Shape                    //
//                                                                              //
//   This class is needed to be instanciated and can be used calling the        //
//   CreateGeometry method:                                                     //
//   TRootStep * mygeom= new TRootStep(gGeoManager);                            //
//   mygeom->CreateGeometry();                                                  //
//                                                                              //
// The resuling STEP file will be saved in the current directory and called     //
// geometry.stp                                                                 //
// To compile the TGeoCad module on ROOT, OpenCascade must be installed!        //
//////////////////////////////////////////////////////////////////////////////////



#include "TGeoManager.h"
#include "OCCStep.h"
#include "TRootStep.h"
#include <TString.h>
#include <TClass.h>

ClassImp(TRootStep)

TRootStep::TRootStep():TObject(),
                       fGeometry(0)
{

}

TRootStep::TRootStep(TGeoManager *geom):TObject(),
                                        fGeometry(geom)
{

}

TRootStep::~TRootStep()
{
   if (fGeometry) delete fGeometry;
}

void * TRootStep::CreateGeometry()
{	
   //ROOT CAD CONVERSION
   fCreate = new OCCStep();
   //cout<<"logical tree created"<<endl;
   fCreate->OCCShapeCreation(fGeometry);
   fCreate->OCCTreeCreation(fGeometry);
   fCreate->OCCWriteStep("geometry.stp");
   //fCreate->PrintAssembly();

   // CAD ROOT CONVERSION
   //CadDDLConverter * myConverter = new CadDDLConverter("buildxxx.stp", "Root_Test.root","Materials.dat", "Replacement.dat", true);
   //myConverter->ImportStep();
   //myConverter->WriteOutputFile("ROOT");
   delete(fCreate);
   return NULL;
}
