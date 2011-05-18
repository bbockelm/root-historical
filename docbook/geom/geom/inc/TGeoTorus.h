// @(#)root/base:$Id$
// Author: Andrei Gheata   28/07/03

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TGeoTorus
#define ROOT_TGeoTorus

#ifndef ROOT_TGeoBBox
#include "TGeoBBox.h"
#endif

////////////////////////////////////////////////////////////////////////////
//                                                                        //
// TGeoTorus - Torus segment class. A torus has 5 parameters :            //
//            R    - axial radius                                         //
//            Rmin - inner radius                                         //
//            Rmax - outer radius                                         //
//            Phi1 - starting phi                                         //
//            Dphi - phi extent                                           //
//                                                                        //
////////////////////////////////////////////////////////////////////////////

class TGeoTorus : public TGeoBBox
{
protected :
// data members
   Double_t              fR;    // axial radius
   Double_t              fRmin; // inner radius
   Double_t              fRmax; // outer radius
   Double_t              fPhi1; // starting phi
   Double_t              fDphi; // phi extent
// methods

public:
   virtual Double_t      Capacity() const;
   Double_t              Daxis(Double_t *pt, Double_t *dir, Double_t t) const;
   Double_t              DDaxis(Double_t *pt, Double_t *dir, Double_t t) const;
   Double_t              DDDaxis(Double_t *pt, Double_t *dir, Double_t t) const;
   Double_t              ToBoundary(Double_t *pt, Double_t *dir, Double_t r) const;
   Int_t                 SolveCubic(Double_t a, Double_t b, Double_t c, Double_t *x) const;
   Int_t                 SolveQuartic(Double_t a, Double_t b, Double_t c, Double_t d, Double_t *x) const;
public:
   // constructors
   TGeoTorus();
   TGeoTorus(Double_t r, Double_t rmin, Double_t rmax, Double_t phi1=0, Double_t dphi=360);
   TGeoTorus(const char * name, Double_t r, Double_t rmin, Double_t rmax, Double_t phi1=0, Double_t dphi=360);
   TGeoTorus(Double_t *params);
   // destructor
   virtual ~TGeoTorus() {}
   // methods

   virtual void          ComputeBBox();
   virtual void          ComputeNormal(Double_t *point, Double_t *dir, Double_t *norm);
   virtual Bool_t        Contains(Double_t *point) const;
   virtual Double_t      DistFromInside(Double_t *point, Double_t *dir, Int_t iact=1, 
                                   Double_t step=TGeoShape::Big(), Double_t *safe=0) const;
   virtual Double_t      DistFromOutside(Double_t *point, Double_t *dir, Int_t iact=1, 
                                   Double_t step=TGeoShape::Big(), Double_t *safe=0) const;
   virtual Int_t         DistancetoPrimitive(Int_t px, Int_t py);
   virtual TGeoVolume   *Divide(TGeoVolume *voldiv, const char *divname, Int_t iaxis, Int_t ndiv, 
                                Double_t start, Double_t step);
   virtual const char   *GetAxisName(Int_t iaxis) const;
   virtual Double_t      GetAxisRange(Int_t iaxis, Double_t &xlo, Double_t &xhi) const;
   virtual void          GetBoundingCylinder(Double_t *param) const;
   virtual const TBuffer3D &GetBuffer3D(Int_t reqSections, Bool_t localFrame) const;
   virtual Int_t         GetByteCount() const {return 56;}
   virtual TGeoShape    *GetMakeRuntimeShape(TGeoShape *mother, TGeoMatrix *mat) const;
   virtual void          GetMeshNumbers(Int_t &nvert, Int_t &nsegs, Int_t &npols) const;
   virtual Int_t         GetNmeshVertices() const;
   virtual Bool_t        GetPointsOnSegments(Int_t /*npoints*/, Double_t * /*array*/) const {return kFALSE;}
   Double_t              GetR() const    {return fR;}
   Double_t              GetRmin() const {return fRmin;}
   Double_t              GetRmax() const {return fRmax;}
   Double_t              GetPhi1() const {return fPhi1;}
   Double_t              GetDphi() const {return fDphi;}
   virtual void          InspectShape() const;
   virtual Bool_t        IsCylType() const {return kTRUE;}
   virtual TBuffer3D    *MakeBuffer3D() const;
   virtual Double_t      Safety(Double_t *point, Bool_t in=kTRUE) const;
   virtual void          SavePrimitive(ostream &out, Option_t *option = "");
   void                  SetTorusDimensions(Double_t r, Double_t rmin, Double_t rmax, Double_t phi1, Double_t dphi);
   virtual void          SetDimensions(Double_t *param);
   virtual void          SetPoints(Double_t *points) const;
   virtual void          SetPoints(Float_t *points) const;
   virtual void          SetSegsAndPols(TBuffer3D &buff) const;
   virtual void          Sizeof3D() const;

   ClassDef(TGeoTorus, 1)         // torus class

};

#endif
