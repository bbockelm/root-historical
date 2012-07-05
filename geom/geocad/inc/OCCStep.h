// @(#)geom/geocad:$Id$
// Author: Cinzia Luzzi   5/5/2012

/*************************************************************************
 * Copyright (C) 1995-2012, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/
 
#ifndef ROOT_OCCStep
#define ROOT_OCCStep

#include "TGeoNode.h"
#include "TGeoMatrix.h"
#include "RootOCC.h"

#include <TDF_Label.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <TDocStd_Document.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDF_Label.hxx>
#include <TopoDS_Shape.hxx>


class OCCStep {

private:
   typedef std::map <TGeoVolume *, TDF_Label> LabelMap_t;

   STEPCAFControl_Writer    fWriter; //the step file pointer
   Handle(TDocStd_Document) fDoc;    //the step document element
   LabelMap_t               fTree;   //tree of Label's volumes
   TDF_Label                fLabel;  //label of the OCC shape elemet
   RootOCC                  fRootShape;
   TopoDS_Shape             fShape;  //OCC shape (translated root shape)

   void            OCCDocCreation();
   TopoDS_Shape    AssemblyShape(TGeoVolume *vol, TGeoHMatrix m);
   TGeoVolume     *GetVolumeOfLabel(TDF_Label fLabel);
   TDF_Label       GetLabelOfVolume(TGeoVolume * v);
   void            AddChildLabel(TDF_Label mother, TDF_Label child, TopLoc_Location loc);
   TopLoc_Location CalcLocation(TGeoHMatrix matrix);

public:
   OCCStep();
   void      PrintAssembly();
   TDF_Label OCCShapeCreation(TGeoManager *m);
   void      OCCTreeCreation(TGeoManager *m);
   void      OCCWriteStep(const char *fname);
};

#endif
