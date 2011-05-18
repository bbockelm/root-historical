// @(#)root/geom:$Id$
// Author: Andrei Gheata   24/10/01
// TGeoPcon::Contains() implemented by Mihaela Gheata

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//_____________________________________________________________________________
// TGeoPcon - a polycone. It has at least 9 parameters :
//            - the lower phi limit;
//            - the range in phi;
//            - the number of z planes (at least two) where the inner/outer 
//              radii are changing;
//            - z coordinate, inner and outer radius for each z plane
//
//_____________________________________________________________________________
//Begin_Html
/*
<img src="gif/t_pcon.gif">
*/
//End_Html

//Begin_Html
/*
<img src="gif/t_pcondivPHI.gif">
*/
//End_Html

//Begin_Html
/*
<img src="gif/t_pcondivstepPHI.gif">
*/
//End_Html

//Begin_Html
/*
<img src="gif/t_pcondivstepZ.gif">
*/
//End_Html

#include "Riostream.h"

#include "TGeoManager.h"
#include "TGeoVolume.h"
#include "TVirtualGeoPainter.h"
#include "TGeoTube.h"
#include "TGeoCone.h"
#include "TGeoPcon.h"
#include "TVirtualPad.h"
#include "TBuffer3D.h"
#include "TBuffer3DTypes.h"
#include "TMath.h"

ClassImp(TGeoPcon)

//_____________________________________________________________________________
TGeoPcon::TGeoPcon()
         :TGeoBBox(0, 0, 0),
          fNz(0),
          fPhi1(0.),
          fDphi(0.),
          fRmin(NULL),
          fRmax(NULL),
          fZ(NULL)
{
// dummy ctor
   SetShapeBit(TGeoShape::kGeoPcon);
}   

//_____________________________________________________________________________
TGeoPcon::TGeoPcon(Double_t phi, Double_t dphi, Int_t nz)
         :TGeoBBox(0, 0, 0),
          fNz(nz),
          fPhi1(phi),
          fDphi(dphi),
          fRmin(NULL),
          fRmax(NULL),
          fZ(NULL)
{
// Default constructor
   SetShapeBit(TGeoShape::kGeoPcon);
   if (fPhi1<0) fPhi1+=360.;
   fRmin = new Double_t [nz];
   fRmax = new Double_t [nz];
   fZ    = new Double_t [nz];
   memset(fRmin, 0, nz*sizeof(Double_t));
   memset(fRmax, 0, nz*sizeof(Double_t));
   memset(fZ, 0, nz*sizeof(Double_t));
}

//_____________________________________________________________________________
TGeoPcon::TGeoPcon(const char *name, Double_t phi, Double_t dphi, Int_t nz)
         :TGeoBBox(name, 0, 0, 0),
          fNz(nz),
          fPhi1(phi),
          fDphi(dphi),
          fRmin(NULL),
          fRmax(NULL),
          fZ(NULL)         
{
// Default constructor
   SetShapeBit(TGeoShape::kGeoPcon);
   if (fPhi1<0) fPhi1+=360.;
   fRmin = new Double_t [nz];
   fRmax = new Double_t [nz];
   fZ    = new Double_t [nz];
   memset(fRmin, 0, nz*sizeof(Double_t));
   memset(fRmax, 0, nz*sizeof(Double_t));
   memset(fZ, 0, nz*sizeof(Double_t));
}

//_____________________________________________________________________________
TGeoPcon::TGeoPcon(Double_t *param)
         :TGeoBBox(0, 0, 0),
          fNz(0),
          fPhi1(0.),
          fDphi(0.),
          fRmin(0),
          fRmax(0),
          fZ(0)
{
// Default constructor in GEANT3 style
// param[0] = phi1
// param[1] = dphi
// param[2] = nz
//
// param[3] = z1
// param[4] = Rmin1
// param[5] = Rmax1
// ...
   SetShapeBit(TGeoShape::kGeoPcon);
   SetDimensions(param);
   ComputeBBox();
}

//_____________________________________________________________________________
TGeoPcon::TGeoPcon(const TGeoPcon& pc) : 
  TGeoBBox(pc),
  fNz(pc.fNz),
  fPhi1(pc.fPhi1),
  fDphi(pc.fDphi),
  fRmin(pc.fRmin),
  fRmax(pc.fRmax),
  fZ(pc.fZ)
{ 
   //copy constructor
}

//_____________________________________________________________________________
TGeoPcon& TGeoPcon::operator=(const TGeoPcon& pc) 
{
   //assignment operator
   if(this!=&pc) {
      TGeoBBox::operator=(pc);
      fNz=pc.fNz;
      fPhi1=pc.fPhi1;
      fDphi=pc.fDphi;
      fRmin=pc.fRmin;
      fRmax=pc.fRmax;
      fZ=pc.fZ;
   } 
   return *this;
}

//_____________________________________________________________________________
TGeoPcon::~TGeoPcon()
{
// destructor
   if (fRmin) {delete[] fRmin; fRmin = 0;}
   if (fRmax) {delete[] fRmax; fRmax = 0;}
   if (fZ)    {delete[] fZ; fZ = 0;}
}

//_____________________________________________________________________________
Double_t TGeoPcon::Capacity() const
{
// Computes capacity of the shape in [length^3]
   Int_t ipl;
   Double_t rmin1, rmax1, rmin2, rmax2, phi1, phi2, dz;
   Double_t capacity = 0.;
   phi1 = fPhi1;
   phi2 = fPhi1 + fDphi;
   for (ipl=0; ipl<fNz-1; ipl++) {
      dz    = 0.5*(fZ[ipl+1]-fZ[ipl]);
      if (dz < TGeoShape::Tolerance()) continue;
      rmin1 = fRmin[ipl];
      rmax1 = fRmax[ipl];
      rmin2 = fRmin[ipl+1];
      rmax2 = fRmax[ipl+1];
      capacity += TGeoConeSeg::Capacity(dz,rmin1,rmax1,rmin2,rmax2,phi1,phi2);
   }
   return capacity;   
}

//_____________________________________________________________________________   
void TGeoPcon::ComputeBBox()
{
// compute bounding box of the pcon
   // Check if the sections are in increasing Z order
   for (Int_t isec=0; isec<fNz-1; isec++) {
      if (TMath::Abs(fZ[isec]-fZ[isec+1]) < TGeoShape::Tolerance()) fZ[isec+1]=fZ[isec];
      if (fZ[isec]>fZ[isec+1]) {
         InspectShape();
         Fatal("ComputeBBox", "Wrong section order");
      }   
   }
   // Check if the last sections are valid
   if (TMath::Abs(fZ[1]-fZ[0]) < TGeoShape::Tolerance() ||
       TMath::Abs(fZ[fNz-1]-fZ[fNz-2]) < TGeoShape::Tolerance()) {
      InspectShape();
      Fatal("ComputeBBox","Shape %s at index %d: Not allowed first two or last two sections at same Z",
             GetName(), gGeoManager->GetListOfShapes()->IndexOf(this));
   }          
   Double_t zmin = TMath::Min(fZ[0], fZ[fNz-1]);
   Double_t zmax = TMath::Max(fZ[0], fZ[fNz-1]);
   // find largest rmax an smallest rmin
   Double_t rmin, rmax;
   rmin = fRmin[TMath::LocMin(fNz, fRmin)];
   rmax = fRmax[TMath::LocMax(fNz, fRmax)];
   Double_t phi1 = fPhi1;
   Double_t phi2 = phi1 + fDphi;
   
   Double_t xc[4];
   Double_t yc[4];
   xc[0] = rmax*TMath::Cos(phi1*TMath::DegToRad());
   yc[0] = rmax*TMath::Sin(phi1*TMath::DegToRad());
   xc[1] = rmax*TMath::Cos(phi2*TMath::DegToRad());
   yc[1] = rmax*TMath::Sin(phi2*TMath::DegToRad());
   xc[2] = rmin*TMath::Cos(phi1*TMath::DegToRad());
   yc[2] = rmin*TMath::Sin(phi1*TMath::DegToRad());
   xc[3] = rmin*TMath::Cos(phi2*TMath::DegToRad());
   yc[3] = rmin*TMath::Sin(phi2*TMath::DegToRad());

   Double_t xmin = xc[TMath::LocMin(4, &xc[0])];
   Double_t xmax = xc[TMath::LocMax(4, &xc[0])]; 
   Double_t ymin = yc[TMath::LocMin(4, &yc[0])]; 
   Double_t ymax = yc[TMath::LocMax(4, &yc[0])];

   Double_t ddp = -phi1;
   if (ddp<0) ddp+= 360;
   if (ddp<=fDphi) xmax = rmax;
   ddp = 90-phi1;
   if (ddp<0) ddp+= 360;
   if (ddp<=fDphi) ymax = rmax;
   ddp = 180-phi1;
   if (ddp<0) ddp+= 360;
   if (ddp<=fDphi) xmin = -rmax;
   ddp = 270-phi1;
   if (ddp<0) ddp+= 360;
   if (ddp<=fDphi) ymin = -rmax;
   fOrigin[0] = (xmax+xmin)/2;
   fOrigin[1] = (ymax+ymin)/2;
   fOrigin[2] = (zmax+zmin)/2;
   fDX = (xmax-xmin)/2;
   fDY = (ymax-ymin)/2;
   fDZ = (zmax-zmin)/2;
   SetShapeBit(kGeoClosedShape);
}   

//_____________________________________________________________________________   
void TGeoPcon::ComputeNormal(Double_t *point, Double_t *dir, Double_t *norm)
{
// Compute normal to closest surface from POINT. 
   memset(norm,0,3*sizeof(Double_t));
   Double_t r;
   Double_t ptnew[3];
   Double_t dz, rmin1, rmax1, rmin2, rmax2;
   Bool_t is_tube, is_seg;
   Double_t phi1=0, phi2=0, c1=0, s1=0, c2=0, s2=0;
   Int_t ipl = TMath::BinarySearch(fNz, fZ, point[2]);
   if (ipl==(fNz-1) || ipl<0) {
      // point outside Z range
      norm[2] = TMath::Sign(1., dir[2]);
      return;
   }
   Int_t iplclose = ipl;
   if ((fZ[ipl+1]-point[2])<(point[2]-fZ[ipl])) iplclose++;
   dz = TMath::Abs(fZ[iplclose]-point[2]);
   if (dz<1E-5) {
      if (iplclose==0 || iplclose==(fNz-1)) {
         norm[2] = TMath::Sign(1., dir[2]);
         return;
      }
      if (iplclose==ipl && TGeoShape::IsSameWithinTolerance(fZ[ipl],fZ[ipl-1])) {
         r = TMath::Sqrt(point[0]*point[0]+point[1]*point[1]);
         if (r<TMath::Max(fRmin[ipl],fRmin[ipl-1]) || r>TMath::Min(fRmax[ipl],fRmax[ipl-1])) {
            norm[2] = TMath::Sign(1., dir[2]);
            return;
         }
      } else {
         if (TGeoShape::IsSameWithinTolerance(fZ[iplclose],fZ[iplclose+1])) {
            r = TMath::Sqrt(point[0]*point[0]+point[1]*point[1]);
            if (r<TMath::Max(fRmin[iplclose],fRmin[iplclose+1]) || r>TMath::Min(fRmax[iplclose],fRmax[iplclose+1])) {
               norm[2] = TMath::Sign(1., dir[2]);
               return;
            }
         }
      }
   } //-> Z done
   memcpy(ptnew, point, 3*sizeof(Double_t));
   dz = 0.5*(fZ[ipl+1]-fZ[ipl]);
   if (TGeoShape::IsSameWithinTolerance(dz,0.)) {
      norm[2] = TMath::Sign(1., dir[2]);
      return;
   }         
   ptnew[2] -= 0.5*(fZ[ipl]+fZ[ipl+1]);
   rmin1 = fRmin[ipl];
   rmax1 = fRmax[ipl];
   rmin2 = fRmin[ipl+1];
   rmax2 = fRmax[ipl+1];
   is_tube = (TGeoShape::IsSameWithinTolerance(rmin1,rmin2) && TGeoShape::IsSameWithinTolerance(rmax1,rmax2))?kTRUE:kFALSE;
   is_seg  = (fDphi<360)?kTRUE:kFALSE;
   if (is_seg) {
      phi1 = fPhi1;
      if (phi1<0) phi1+=360;
      phi2 = phi1 + fDphi;
      phi1 *= TMath::DegToRad();
      phi2 *= TMath::DegToRad();
      c1 = TMath::Cos(phi1);
      s1 = TMath::Sin(phi1);
      c2 = TMath::Cos(phi2);
      s2 = TMath::Sin(phi2);
      if (is_tube) TGeoTubeSeg::ComputeNormalS(ptnew,dir,norm,rmin1,rmax1,dz,c1,s1,c2,s2);
      else         TGeoConeSeg::ComputeNormalS(ptnew,dir,norm,dz,rmin1,rmax1,rmin2,rmax2,c1,s1,c2,s2);
   } else {
      if (is_tube) TGeoTube::ComputeNormalS(ptnew,dir,norm,rmin1,rmax1,dz);
      else         TGeoCone::ComputeNormalS(ptnew,dir,norm,dz,rmin1,rmax1,rmin2,rmax2);
   }
}

//_____________________________________________________________________________
Bool_t TGeoPcon::Contains(Double_t *point) const
{
// test if point is inside this shape
   // check total z range
   if ((point[2]<fZ[0]) || (point[2]>fZ[fNz-1])) return kFALSE;
   // check R squared
   Double_t r2 = point[0]*point[0]+point[1]*point[1];
   
   Int_t izl = 0;
   Int_t izh = fNz-1;
   Int_t izt = (fNz-1)/2;
   while ((izh-izl)>1) {
      if (point[2] > fZ[izt]) izl = izt;     
      else izh = izt;
      izt = (izl+izh)>>1;
   }
   // the point is in the section bounded by izl and izh Z planes
   
   // compute Rmin and Rmax and test the value of R squared
   Double_t rmin, rmax;  
   if (TGeoShape::IsSameWithinTolerance(fZ[izl],fZ[izh]) && TGeoShape::IsSameWithinTolerance(point[2],fZ[izl])) {
      rmin = TMath::Min(fRmin[izl], fRmin[izh]);
      rmax = TMath::Max(fRmax[izl], fRmax[izh]);
   } else {
      Double_t dz = fZ[izh] - fZ[izl];
      Double_t dz1 = point[2] - fZ[izl];
      rmin = (fRmin[izl]*(dz-dz1)+fRmin[izh]*dz1)/dz;
      rmax = (fRmax[izl]*(dz-dz1)+fRmax[izh]*dz1)/dz;
   }
   if ((r2<rmin*rmin) || (r2>rmax*rmax)) return kFALSE;
   // now check phi 
   if (TGeoShape::IsSameWithinTolerance(fDphi,360)) return kTRUE;
   if (r2<1E-10) return kTRUE;
   Double_t phi = TMath::ATan2(point[1], point[0]) * TMath::RadToDeg();
   if (phi < 0) phi+=360.0;
   Double_t ddp = phi-fPhi1;
   if (ddp<0) ddp+=360.;
   if (ddp<=fDphi) return kTRUE;
   return kFALSE;
}

//_____________________________________________________________________________
Int_t TGeoPcon::DistancetoPrimitive(Int_t px, Int_t py)
{
// compute closest distance from point px,py to each corner
   Int_t n = gGeoManager->GetNsegments()+1;
   const Int_t numPoints = 2*n*fNz;
   return ShapeDistancetoPrimitive(numPoints, px, py);
}

//_____________________________________________________________________________
Double_t TGeoPcon::DistFromInside(Double_t *point, Double_t *dir, Int_t iact, Double_t step, Double_t *safe) const
{
// compute distance from inside point to surface of the polycone
   if (iact<3 && safe) {
      *safe = Safety(point, kTRUE);
      if (iact==0) return TGeoShape::Big();
      if ((iact==1) && (*safe>step)) return TGeoShape::Big();
   }
   Double_t snxt = TGeoShape::Big();
   Double_t sstep = 1E-6;
   Double_t point_new[3];
   // determine which z segment contains the point
   Int_t ipl = TMath::BinarySearch(fNz, fZ, point[2]);
   if (ipl<0) ipl=0;
   if (ipl==(fNz-1)) ipl--;
   Double_t dz = 0.5*(fZ[ipl+1]-fZ[ipl]);
   Bool_t special_case = kFALSE;
   if (dz<1e-9) {
      // radius changing segment, make sure track is not in the XY plane
      if (TGeoShape::IsSameWithinTolerance(dir[2], 0)) {
         special_case = kTRUE;
      } else {
         //check if a close point is still contained
         point_new[0] = point[0]+sstep*dir[0];
         point_new[1] = point[1]+sstep*dir[1];
         point_new[2] = point[2]+sstep*dir[2];
         if (!Contains(point_new)) return 0.;
         return (DistFromInside(point_new,dir,iact,step,safe)+sstep);
      }   
   }   
   // determine if the current segment is a tube or a cone
   Bool_t intub = kTRUE;
   if (!TGeoShape::IsSameWithinTolerance(fRmin[ipl],fRmin[ipl+1])) intub=kFALSE;
   else if (!TGeoShape::IsSameWithinTolerance(fRmax[ipl],fRmax[ipl+1])) intub=kFALSE;
   // determine phi segmentation
   Bool_t inphi=kTRUE;
   if (TGeoShape::IsSameWithinTolerance(fDphi,360)) inphi=kFALSE;     
   memcpy(point_new, point, 2*sizeof(Double_t));
   // new point in reference system of the current segment
   point_new[2] = point[2]-0.5*(fZ[ipl]+fZ[ipl+1]);
   
   Double_t phi1 = fPhi1;
   if (phi1<0) phi1+=360.;
   Double_t phi2 = phi1+fDphi;
   Double_t phim = 0.5*(phi1+phi2);
   Double_t c1 = TMath::Cos(phi1*TMath::DegToRad());
   Double_t s1 = TMath::Sin(phi1*TMath::DegToRad());
   Double_t c2 = TMath::Cos(phi2*TMath::DegToRad());
   Double_t s2 = TMath::Sin(phi2*TMath::DegToRad());
   Double_t cm = TMath::Cos(phim*TMath::DegToRad());
   Double_t sm = TMath::Sin(phim*TMath::DegToRad());
   Double_t cdfi = TMath::Cos(0.5*fDphi*TMath::DegToRad());
   if (special_case) {
      if (inphi) snxt = TGeoTubeSeg::DistFromInsideS(point_new, dir, 
               TMath::Min(fRmin[ipl],fRmin[ipl+1]), TMath::Max(fRmax[ipl],fRmax[ipl+1]),
               dz, c1,s1,c2,s2,cm,sm,cdfi);
      else       snxt = TGeoTube::DistFromInsideS(point_new, dir, 
               TMath::Min(fRmin[ipl],fRmin[ipl+1]), TMath::Max(fRmax[ipl],fRmax[ipl+1]),dz);
      return snxt;
   }   
   if (intub) {
      if (inphi) snxt=TGeoTubeSeg::DistFromInsideS(point_new, dir, fRmin[ipl], fRmax[ipl],dz, c1,s1,c2,s2,cm,sm,cdfi); 
      else snxt=TGeoTube::DistFromInsideS(point_new, dir, fRmin[ipl], fRmax[ipl],dz);
   } else {
      if (inphi) snxt=TGeoConeSeg::DistFromInsideS(point_new,dir,dz,fRmin[ipl],fRmax[ipl],fRmin[ipl+1],fRmax[ipl+1],c1,s1,c2,s2,cm,sm,cdfi);
      else snxt=TGeoCone::DistFromInsideS(point_new,dir,dz,fRmin[ipl],fRmax[ipl],fRmin[ipl+1], fRmax[ipl+1]);
   }                              

   for (Int_t i=0; i<3; i++) point_new[i]=point[i]+(snxt+1E-6)*dir[i];
   if (!Contains(&point_new[0])) return snxt;
   
   snxt += DistFromInside(&point_new[0], dir, 3) + 1E-6;
   return snxt;
}

//_____________________________________________________________________________
Double_t TGeoPcon::DistToSegZ(Double_t *point, Double_t *dir, Int_t &iz, Double_t c1, Double_t s1, 
                              Double_t c2, Double_t s2, Double_t cfio, Double_t sfio, Double_t cdfi) const
{
// compute distance to a pcon Z slice. Segment iz must be valid
   Double_t zmin=fZ[iz];
   Double_t zmax=fZ[iz+1];
   if (TGeoShape::IsSameWithinTolerance(zmin,zmax)) {
      if (TGeoShape::IsSameWithinTolerance(dir[2],0)) return TGeoShape::Big();
      Int_t istep=(dir[2]>0)?1:-1;
      iz+=istep;
      if (iz<0 || iz>(fNz-2)) return TGeoShape::Big();
      return DistToSegZ(point,dir,iz,c1,s1,c2,s2,cfio,sfio,cdfi);
   }
   Double_t dz=0.5*(zmax-zmin);
   Double_t local[3];
   memcpy(&local[0], point, 3*sizeof(Double_t));
   local[2]=point[2]-0.5*(zmin+zmax);
   Double_t snxt;
   Double_t rmin1=fRmin[iz];
   Double_t rmax1=fRmax[iz];
   Double_t rmin2=fRmin[iz+1];
   Double_t rmax2=fRmax[iz+1];
   Bool_t is_seg=(TGeoShape::IsSameWithinTolerance(fDphi,360))?kFALSE:kTRUE;

   if (TGeoShape::IsSameWithinTolerance(rmin1,rmin2) && TGeoShape::IsSameWithinTolerance(rmax1,rmax2)) {
      if (!is_seg) snxt=TGeoTube::DistFromOutsideS(local, dir, rmin1, rmax1, dz);
      else snxt=TGeoTubeSeg::DistFromOutsideS(local,dir,rmin1,rmax1,dz,c1,s1,c2,s2,cfio,sfio,cdfi);
   } else {  
      if (!is_seg) snxt=TGeoCone::DistFromOutsideS(local,dir,dz,rmin1, rmax1,rmin2,rmax2);
      else snxt=TGeoConeSeg::DistFromOutsideS(local,dir,dz,rmin1,rmax1,rmin2,rmax2,c1,s1,c2,s2,cfio,sfio,cdfi);
   }
   if (snxt<1E20) return snxt;
   // check next segment
   if (TGeoShape::IsSameWithinTolerance(dir[2],0)) return TGeoShape::Big();
   Int_t istep=(dir[2]>0)?1:-1;
   iz+=istep;
   if (iz<0 || iz>(fNz-2)) return TGeoShape::Big();
   return DistToSegZ(point,dir,iz,c1,s1,c2,s2,cfio,sfio,cdfi);
}      

//_____________________________________________________________________________
Double_t TGeoPcon::DistFromOutside(Double_t *point, Double_t *dir, Int_t iact, Double_t step, Double_t *safe) const
{
// compute distance from outside point to surface of the tube
   if ((iact<3) && safe) {
      *safe = Safety(point, kFALSE);
      if ((iact==1) && (*safe>step)) return TGeoShape::Big();
      if (iact==0) return TGeoShape::Big();
   }
   // check if ray intersect outscribed cylinder
   if ((point[2]<fZ[0]) && (dir[2]<=0)) return TGeoShape::Big();
   if ((point[2]>fZ[fNz-1]) && (dir[2]>=0)) return TGeoShape::Big();
// Check if the bounding box is crossed within the requested distance
   Double_t sdist = TGeoBBox::DistFromOutside(point,dir, fDX, fDY, fDZ, fOrigin, step);
   if (sdist>=step) return TGeoShape::Big();

   Double_t r2 = point[0]*point[0]+point[1]*point[1];
   Double_t radmax=0;
   radmax=fRmax[TMath::LocMax(fNz, fRmax)];
   if (r2>(radmax*radmax)) {
      Double_t rpr=-point[0]*dir[0]-point[1]*dir[1];
      Double_t nxy=dir[0]*dir[0]+dir[1]*dir[1];
      if (rpr<TMath::Sqrt((r2-radmax*radmax)*nxy)) return TGeoShape::Big();
   }

   // find in which Z segment we are
   Int_t ipl = TMath::BinarySearch(fNz, fZ, point[2]);
   Int_t ifirst = ipl;
   if (ifirst<0) {
      ifirst=0;
   } else if (ifirst>=(fNz-1)) ifirst=fNz-2;
   // find if point is in the phi gap
   Double_t phi=0;
   Double_t phi1=0;
   Double_t phi2=0;
   Double_t c1=0., s1=0., c2=0., s2=0., cfio=0., sfio=0., cdfi=0.;
   Bool_t inphi = (fDphi<360)?kTRUE:kFALSE;
   if (inphi) {
      phi1=fPhi1;
      if (phi1<0) phi1+=360;
      phi2=(phi1+fDphi)*TMath::DegToRad();
      phi1=phi1*TMath::DegToRad();
      phi=TMath::ATan2(point[1], point[0]);
      if (phi<0) phi+=2.*TMath::Pi();
      c1=TMath::Cos(phi1);
      s1=TMath::Sin(phi1);
      c2=TMath::Cos(phi2);
      s2=TMath::Sin(phi2);
      Double_t fio=0.5*(phi1+phi2);
      cfio=TMath::Cos(fio);
      sfio=TMath::Sin(fio);
      cdfi=TMath::Cos(0.5*(phi2-phi1));
   } 

   // compute distance to boundary
   return DistToSegZ(point,dir,ifirst, c1,s1,c2,s2,cfio,sfio,cdfi);
}

//_____________________________________________________________________________
void TGeoPcon::DefineSection(Int_t snum, Double_t z, Double_t rmin, Double_t rmax)
{
// Defines z position of a section plane, rmin and rmax at this z. Sections
// should be defined in increasing or decreasing Z order and the last section 
// HAS to be snum = fNz-1
   if ((snum<0) || (snum>=fNz)) return;
   fZ[snum]    = z;
   fRmin[snum] = rmin;
   fRmax[snum] = rmax;
   if (rmin>rmax) 
      Warning("DefineSection", "Shape %s: invalid rmin=%g rmax=%g", GetName(), rmin, rmax);
   if (snum==(fNz-1)) {
      // Reorder sections in increasing Z order
      if (fZ[0] > fZ[snum]) {
         Int_t iz = 0;
         Int_t izi = fNz-1;
         Double_t temp;
         while (iz<izi) {
            temp = fZ[iz];
            fZ[iz] = fZ[izi];
            fZ[izi] = temp;
            temp = fRmin[iz];
            fRmin[iz] = fRmin[izi];
            fRmin[izi] = temp;
            temp = fRmax[iz];
            fRmax[iz] = fRmax[izi];
            fRmax[izi] = temp;
            iz++;
            izi--;
         }   
      }      
      ComputeBBox();
   }   
}

//_____________________________________________________________________________
Int_t TGeoPcon::GetNsegments() const
{
// Returns number of segments on each mesh circle segment.
   return gGeoManager->GetNsegments();
}

//_____________________________________________________________________________
TGeoVolume *TGeoPcon::Divide(TGeoVolume *voldiv, const char *divname, Int_t iaxis, Int_t ndiv, 
                             Double_t start, Double_t step) 
{
//--- Divide this polycone shape belonging to volume "voldiv" into ndiv volumes
// called divname, from start position with the given step. Returns pointer
// to created division cell volume in case of Z divisions. Z divisions can be
// performed if the divided range is in between two consecutive Z planes.
//  In case a wrong division axis is supplied, returns pointer to 
// volume that was divided.
   TGeoShape *shape;           //--- shape to be created
   TGeoVolume *vol;            //--- division volume to be created
   TGeoVolumeMulti *vmulti;    //--- generic divided volume
   TGeoPatternFinder *finder;  //--- finder to be attached 
   TString opt = "";           //--- option to be attached
   Double_t zmin = start;
   Double_t zmax = start+ndiv*step;            
   Int_t isect = -1;
   Int_t is, id, ipl;
   switch (iaxis) {
      case 1:  //---               R division
         Error("Divide", "Shape %s: cannot divide a pcon on radius", GetName());
         return 0;
      case 2:  //---               Phi division
         finder = new TGeoPatternCylPhi(voldiv, ndiv, start, start+ndiv*step);
         vmulti = gGeoManager->MakeVolumeMulti(divname, voldiv->GetMedium());
         voldiv->SetFinder(finder);
         finder->SetDivIndex(voldiv->GetNdaughters());            
         shape = new TGeoPcon(-step/2, step, fNz);
         for (is=0; is<fNz; is++)
            ((TGeoPcon*)shape)->DefineSection(is, fZ[is], fRmin[is], fRmax[is]); 
            vol = new TGeoVolume(divname, shape, voldiv->GetMedium());
            vmulti->AddVolume(vol);
            opt = "Phi";
            for (id=0; id<ndiv; id++) {
               voldiv->AddNodeOffset(vol, id, start+id*step+step/2, opt.Data());
               ((TGeoNodeOffset*)voldiv->GetNodes()->At(voldiv->GetNdaughters()-1))->SetFinder(finder);
            }
            return vmulti;
      case 3: //---                Z division
         // find start plane
         for (ipl=0; ipl<fNz-1; ipl++) {
            if (start<fZ[ipl]) continue;
            else {
               if ((start+ndiv*step)>fZ[ipl+1]) continue;
            }
            isect = ipl;
            zmin = fZ[isect];
            zmax= fZ[isect+1];
            break;
         }
         if (isect<0) {
            Error("Divide", "Shape %s: cannot divide pcon on Z if divided region is not between 2 planes", GetName());
            return 0;
         }
         finder = new TGeoPatternZ(voldiv, ndiv, start, start+ndiv*step);
         vmulti = gGeoManager->MakeVolumeMulti(divname, voldiv->GetMedium());
         voldiv->SetFinder(finder);
         finder->SetDivIndex(voldiv->GetNdaughters());
         opt = "Z";
         for (id=0; id<ndiv; id++) {
            Double_t z1 = start+id*step;
            Double_t z2 = start+(id+1)*step;
            Double_t rmin1 = (fRmin[isect]*(zmax-z1)-fRmin[isect+1]*(zmin-z1))/(zmax-zmin);
            Double_t rmax1 = (fRmax[isect]*(zmax-z1)-fRmax[isect+1]*(zmin-z1))/(zmax-zmin);
            Double_t rmin2 = (fRmin[isect]*(zmax-z2)-fRmin[isect+1]*(zmin-z2))/(zmax-zmin);
            Double_t rmax2 = (fRmax[isect]*(zmax-z2)-fRmax[isect+1]*(zmin-z2))/(zmax-zmin);
            Bool_t is_tube = (TGeoShape::IsSameWithinTolerance(fRmin[isect],fRmin[isect+1]) && TGeoShape::IsSameWithinTolerance(fRmax[isect],fRmax[isect+1]))?kTRUE:kFALSE;
            Bool_t is_seg = (fDphi<360)?kTRUE:kFALSE;
            if (is_seg) {
               if (is_tube) shape=new TGeoTubeSeg(fRmin[isect],fRmax[isect],step/2, fPhi1, fPhi1+fDphi);
               else shape=new TGeoConeSeg(step/2, rmin1, rmax1, rmin2, rmax2, fPhi1, fPhi1+fDphi);
            } else {
               if (is_tube) shape=new TGeoTube(fRmin[isect],fRmax[isect],step/2);
               else shape = new TGeoCone(step/2,rmin1,rmax1,rmin2,rmax2);
            }    
            vol = new TGeoVolume(divname, shape, voldiv->GetMedium());
            vmulti->AddVolume(vol);
            voldiv->AddNodeOffset(vol, id, start+id*step+step/2, opt.Data());
            ((TGeoNodeOffset*)voldiv->GetNodes()->At(voldiv->GetNdaughters()-1))->SetFinder(finder);
         }
         return vmulti;
      default:
         Error("Divide", "Shape %s: Wrong axis %d for division",GetName(), iaxis);
         return 0;            
   }
}

//_____________________________________________________________________________
const char *TGeoPcon::GetAxisName(Int_t iaxis) const
{
// Returns name of axis IAXIS.
   switch (iaxis) {
      case 1:
         return "R";
      case 2:
         return "PHI";
      case 3:
         return "Z";
      default:
         return "UNDEFINED";
   }
}   

//_____________________________________________________________________________
Double_t TGeoPcon::GetAxisRange(Int_t iaxis, Double_t &xlo, Double_t &xhi) const
{
// Get range of shape for a given axis.
   xlo = 0;
   xhi = 0;
   Double_t dx = 0;
   switch (iaxis) {
      case 2:
         xlo = fPhi1;
         xhi = fPhi1 + fDphi;
         dx = fDphi;
         return dx;
      case 3:
         xlo = fZ[0];
         xhi = fZ[fNz-1];
         dx = xhi-xlo;
         return dx;
   }
   return dx;
}         
            
//_____________________________________________________________________________
void TGeoPcon::GetBoundingCylinder(Double_t *param) const
{
//--- Fill vector param[4] with the bounding cylinder parameters. The order
// is the following : Rmin, Rmax, Phi1, Phi2
   param[0] = fRmin[0];           // Rmin
   param[1] = fRmax[0];           // Rmax
   for (Int_t i=1; i<fNz; i++) {
      if (fRmin[i] < param[0]) param[0] = fRmin[i];
      if (fRmax[i] > param[1]) param[1] = fRmax[i];
   }
   param[0] *= param[0];
   param[1] *= param[1];
   if (TGeoShape::IsSameWithinTolerance(fDphi,360.)) {
      param[2] = 0.;
      param[3] = 360.;
      return;
   }   
   param[2] = (fPhi1<0)?(fPhi1+360.):fPhi1;     // Phi1
   param[3] = param[2]+fDphi;                   // Phi2
}   

//_____________________________________________________________________________
Double_t TGeoPcon::GetRmin(Int_t ipl) const
{
// Returns Rmin for Z segment IPL.
   if (ipl<0 || ipl>(fNz-1)) {
      Error("GetRmin","ipl=%i out of range (0,%i) in shape %s",ipl,fNz-1,GetName());
      return 0.;
   }
   return fRmin[ipl];
}      

//_____________________________________________________________________________
Double_t TGeoPcon::GetRmax(Int_t ipl) const
{
// Returns Rmax for Z segment IPL.
   if (ipl<0 || ipl>(fNz-1)) {
      Error("GetRmax","ipl=%i out of range (0,%i) in shape %s",ipl,fNz-1,GetName());
      return 0.;
   }
   return fRmax[ipl];
}      

//_____________________________________________________________________________
Double_t TGeoPcon::GetZ(Int_t ipl) const
{
// Returns Z for segment IPL.
   if (ipl<0 || ipl>(fNz-1)) {
      Error("GetZ","ipl=%i out of range (0,%i) in shape %s",ipl,fNz-1,GetName());
      return 0.;
   }
   return fZ[ipl];
}      

//_____________________________________________________________________________
void TGeoPcon::InspectShape() const
{
// print shape parameters
   printf("*** Shape %s: TGeoPcon ***\n", GetName());
   printf("    Nz    = %i\n", fNz);
   printf("    phi1  = %11.5f\n", fPhi1);
   printf("    dphi  = %11.5f\n", fDphi);
   for (Int_t ipl=0; ipl<fNz; ipl++)
      printf("     plane %i: z=%11.5f Rmin=%11.5f Rmax=%11.5f\n", ipl, fZ[ipl], fRmin[ipl], fRmax[ipl]);
   printf(" Bounding box:\n");
   TGeoBBox::InspectShape();
}

//_____________________________________________________________________________
TBuffer3D *TGeoPcon::MakeBuffer3D() const
{ 
   // Creates a TBuffer3D describing *this* shape.
   // Coordinates are in local reference frame.

   const Int_t n = gGeoManager->GetNsegments()+1;
   Int_t nz = GetNz();
   if (nz < 2) return 0;
   Int_t nbPnts = nz*2*n;
   if (nbPnts <= 0) return 0;
   Double_t dphi = GetDphi();

   Bool_t specialCase = kFALSE;
   if (TGeoShape::IsSameWithinTolerance(dphi,360)) specialCase = kTRUE;

   Int_t nbSegs = 4*(nz*n-1+(specialCase == kTRUE));
   Int_t nbPols = 2*(nz*n-1+(specialCase == kTRUE));
   TBuffer3D* buff = new TBuffer3D(TBuffer3DTypes::kGeneric,
                                   nbPnts, 3*nbPnts, nbSegs, 3*nbSegs, nbPols, 6*nbPols);
   if (buff)
   {
      SetPoints(buff->fPnts);
      SetSegsAndPols(*buff);
   }

   return buff;  
}

//_____________________________________________________________________________
void TGeoPcon::SetSegsAndPols(TBuffer3D &buff) const
{
// Fill TBuffer3D structure for segments and polygons.
   Int_t i, j;
   const Int_t n = gGeoManager->GetNsegments()+1;
   Int_t nz = GetNz();
   if (nz < 2) return;
   Int_t nbPnts = nz*2*n;
   if (nbPnts <= 0) return;
   Double_t dphi = GetDphi();

   Bool_t specialCase = kFALSE;
   if (TGeoShape::IsSameWithinTolerance(dphi,360)) specialCase = kTRUE;
   Int_t c = GetBasicColor();

   Int_t indx, indx2, k;
   indx = indx2 = 0;

   //inside & outside circles, number of segments: 2*nz*(n-1)
   //             special case number of segments: 2*nz*n
   for (i = 0; i < nz*2; i++) {
      indx2 = i*n;
      for (j = 1; j < n; j++) {
         buff.fSegs[indx++] = c;
         buff.fSegs[indx++] = indx2+j-1;
         buff.fSegs[indx++] = indx2+j;
      }
      if (specialCase) {
         buff.fSegs[indx++] = c;
         buff.fSegs[indx++] = indx2+j-1;
         buff.fSegs[indx++] = indx2;
      }
   }

   //bottom & top lines, number of segments: 2*n
   for (i = 0; i < 2; i++) {
      indx2 = i*(nz-1)*2*n;
      for (j = 0; j < n; j++) {
         buff.fSegs[indx++] = c;
         buff.fSegs[indx++] = indx2+j;
         buff.fSegs[indx++] = indx2+n+j;
      }
   }

   //inside & outside cilindres, number of segments: 2*(nz-1)*n
   for (i = 0; i < (nz-1); i++) {
      //inside cilinder
      indx2 = i*n*2;
      for (j = 0; j < n; j++) {
         buff.fSegs[indx++] = c+2;
         buff.fSegs[indx++] = indx2+j;
         buff.fSegs[indx++] = indx2+n*2+j;
      }
      //outside cilinder
      indx2 = i*n*2+n;
      for (j = 0; j < n; j++) {
         buff.fSegs[indx++] = c+3;
         buff.fSegs[indx++] = indx2+j;
         buff.fSegs[indx++] = indx2+n*2+j;
      }
   }

   //left & right sections, number of segments: 2*(nz-2)
   //          special case number of segments: 0
   if (!specialCase) {
      for (i = 1; i < (nz-1); i++) {
         for (j = 0; j < 2; j++) {
            buff.fSegs[indx++] = c;
            buff.fSegs[indx++] =  2*i    * n + j*(n-1);
            buff.fSegs[indx++] = (2*i+1) * n + j*(n-1);
         }
      }
   }

   Int_t m = n - 1 + (specialCase == kTRUE);
   indx = 0;

   //bottom & top, number of polygons: 2*(n-1)
   // special case number of polygons: 2*n
   for (j = 0; j < n-1; j++) {
      buff.fPols[indx++] = c+3;
      buff.fPols[indx++] = 4;
      buff.fPols[indx++] = 2*nz*m+j;
      buff.fPols[indx++] = m+j;
      buff.fPols[indx++] = 2*nz*m+j+1;
      buff.fPols[indx++] = j;
   }
   for (j = 0; j < n-1; j++) {
      buff.fPols[indx++] = c+3;
      buff.fPols[indx++] = 4;
      buff.fPols[indx++] = 2*nz*m+n+j;
      buff.fPols[indx++] = (nz*2-2)*m+j;
      buff.fPols[indx++] = 2*nz*m+n+j+1;
      buff.fPols[indx++] = (nz*2-2)*m+m+j;
   }
   if (specialCase) {
      buff.fPols[indx++] = c+3;
      buff.fPols[indx++] = 4;
      buff.fPols[indx++] = 2*nz*m+j;
      buff.fPols[indx++] = m+j;
      buff.fPols[indx++] = 2*nz*m;
      buff.fPols[indx++] = j;
 
      buff.fPols[indx++] = c+3;
      buff.fPols[indx++] = 4;
      buff.fPols[indx++] = 2*nz*m+n+j;
      buff.fPols[indx++] = (nz*2-2)*m+m+j;
      buff.fPols[indx++] = 2*nz*m+n;
      buff.fPols[indx++] = (nz*2-2)*m+j;
   }

   //inside & outside, number of polygons: (nz-1)*2*(n-1)
   for (k = 0; k < (nz-1); k++) {
      for (j = 0; j < n-1; j++) {
         buff.fPols[indx++] = c;
         buff.fPols[indx++] = 4;
         buff.fPols[indx++] = 2*k*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+2)*n+j+1;
         buff.fPols[indx++] = (2*k+2)*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+2)*n+j;
      }
      for (j = 0; j < n-1; j++) {
         buff.fPols[indx++] = c+1;
         buff.fPols[indx++] = 4;
         buff.fPols[indx++] = (2*k+1)*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+3)*n+j;
         buff.fPols[indx++] = (2*k+3)*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+3)*n+j+1;
      }
      if (specialCase) {
         buff.fPols[indx++] = c;
         buff.fPols[indx++] = 4;
         buff.fPols[indx++] = 2*k*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+2)*n;
         buff.fPols[indx++] = (2*k+2)*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+2)*n+j;
 
         buff.fPols[indx++] = c+1;
         buff.fPols[indx++] = 4;
         buff.fPols[indx++] = (2*k+1)*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+3)*n+j;
         buff.fPols[indx++] = (2*k+3)*m+j;
         buff.fPols[indx++] = nz*2*m+(2*k+3)*n;
      }
   }

   //left & right sections, number of polygons: 2*(nz-1)
   //          special case number of polygons: 0
   if (!specialCase) {
      indx2 = nz*2*(n-1);
      for (k = 0; k < (nz-1); k++) {
         buff.fPols[indx++] = c+2;
         buff.fPols[indx++] = 4;
         buff.fPols[indx++] = k==0 ? indx2 : indx2+2*nz*n+2*(k-1);
         buff.fPols[indx++] = indx2+2*(k+1)*n;
         buff.fPols[indx++] = indx2+2*nz*n+2*k;
         buff.fPols[indx++] = indx2+(2*k+3)*n;
 
         buff.fPols[indx++] = c+2;
         buff.fPols[indx++] = 4;
         buff.fPols[indx++] = k==0 ? indx2+n-1 : indx2+2*nz*n+2*(k-1)+1;
         buff.fPols[indx++] = indx2+(2*k+3)*n+n-1;
         buff.fPols[indx++] = indx2+2*nz*n+2*k+1;
         buff.fPols[indx++] = indx2+2*(k+1)*n+n-1;
      }
      buff.fPols[indx-8] = indx2+n;
      buff.fPols[indx-2] = indx2+2*n-1;
   }
}   

//_____________________________________________________________________________
Double_t TGeoPcon::SafetyToSegment(Double_t *point, Int_t ipl, Bool_t in, Double_t safmin) const
{
// Compute safety from POINT to segment between planes ipl, ipl+1 within safmin.

   Double_t safe = TGeoShape::Big();
   if (ipl<0 || ipl>fNz-2) return (safmin+1.); // error in input plane
// Get info about segment.
   Double_t dz = 0.5*(fZ[ipl+1]-fZ[ipl]);
   if (dz<1E-9) return 1E9; // radius-changing segment
   Double_t ptnew[3];
   memcpy(ptnew, point, 3*sizeof(Double_t));
   ptnew[2] -= 0.5*(fZ[ipl]+fZ[ipl+1]);
   safe = TMath::Abs(ptnew[2])-dz;
   if (safe>safmin) return TGeoShape::Big(); // means: stop checking further segments
   Double_t rmin1 = fRmin[ipl];
   Double_t rmax1 = fRmax[ipl];
   Double_t rmin2 = fRmin[ipl+1];
   Double_t rmax2 = fRmax[ipl+1];
   Bool_t   is_tube = (TGeoShape::IsSameWithinTolerance(rmin1,rmin2) && TGeoShape::IsSameWithinTolerance(rmax1,rmax2))?kTRUE:kFALSE;
   Bool_t   is_seg  = (fDphi<360)?kTRUE:kFALSE;
   if (is_seg) {
      if (is_tube) safe = TGeoTubeSeg::SafetyS(ptnew,in,rmin1,rmax1, dz,fPhi1,fPhi1+fDphi,0);
      else         safe = TGeoConeSeg::SafetyS(ptnew,in,dz,rmin1,rmax1,rmin2,rmax2,fPhi1,fPhi1+fDphi,0);
   } else {
      if (is_tube) safe = TGeoTube::SafetyS(ptnew,in,rmin1,rmax1,dz,0);
      else         safe = TGeoCone::SafetyS(ptnew,in,dz,rmin1,rmax1,rmin2,rmax2,0);
   }
   if (safe<0) safe=0;
   return safe;   
}

//_____________________________________________________________________________
Double_t TGeoPcon::Safety(Double_t *point, Bool_t in) const
{
// computes the closest distance from given point to this shape, according
// to option. The matching point on the shape is stored in spoint.
   //---> localize the Z segment
   
   Double_t safmin, saftmp;
   Double_t dz;
   Int_t ipl, iplane;

   if (in) {
   //---> point is inside pcon
      ipl = TMath::BinarySearch(fNz, fZ, point[2]);
      if (ipl==(fNz-1)) return 0;   // point on last Z boundary
      if (ipl<0) return 0;          // point on first Z boundary
      if (ipl>0 && TGeoShape::IsSameWithinTolerance(fZ[ipl-1],fZ[ipl]) && TGeoShape::IsSameWithinTolerance(point[2],fZ[ipl-1])) ipl--;
      dz = 0.5*(fZ[ipl+1]-fZ[ipl]);
      if (dz<1E-8) {
         // Point on a segment-changing plane
         safmin = TMath::Min(point[2]-fZ[ipl-1],fZ[ipl+2]-point[2]);
         saftmp = TGeoShape::Big();
         if (fDphi<360) saftmp = TGeoShape::SafetyPhi(point,in,fPhi1,fPhi1+fDphi);
         if (saftmp<safmin) safmin = saftmp;
         Double_t radius = TMath::Sqrt(point[0]*point[0]+point[1]*point[1]);
         if (fRmin[ipl]>0) safmin = TMath::Min(safmin, radius-fRmin[ipl]);
         if (fRmin[ipl+1]>0) safmin = TMath::Min(safmin, radius-fRmin[ipl+1]);
         safmin = TMath::Min(safmin, fRmax[ipl]-radius);
         safmin = TMath::Min(safmin, fRmax[ipl+1]-radius);
         if (safmin<0) safmin = 0;
         return safmin;
      }   
      // Check safety for current segment
      safmin = SafetyToSegment(point, ipl);
      if (safmin>1E10) {
         //  something went wrong - point is not inside current segment
         return TGeoShape::Big();
      }
      if (safmin<1E-6) return TMath::Abs(safmin); // point on radius-changing plane
      // check increasing iplanes
      iplane = ipl+1;
      saftmp = 0.;
      while ((iplane<fNz-1) && saftmp<1E10) {
         saftmp = TMath::Abs(SafetyToSegment(point,iplane,kFALSE,safmin));
         if (saftmp<safmin) safmin=saftmp;
         iplane++;
      }   
      // now decreasing nplanes
      iplane = ipl-1;
      saftmp = 0.;
      while ((iplane>=0) && saftmp<1E10) {
         saftmp = TMath::Abs(SafetyToSegment(point,iplane,kFALSE,safmin));
         if (saftmp<safmin) safmin=saftmp;
         iplane--;
      }   
      return safmin;
   }   
   //---> point is outside pcon
   ipl = TMath::BinarySearch(fNz, fZ, point[2]);
   if (ipl<0) ipl=0;
   else if (ipl==fNz-1) ipl=fNz-2;
   dz = 0.5*(fZ[ipl+1]-fZ[ipl]);
   if (dz<1E-8 && (ipl+2<fNz)) {
      ipl++;
      dz = 0.5*(fZ[ipl+1]-fZ[ipl]);
   }   
   // Check safety for current segment
   safmin = SafetyToSegment(point, ipl, kFALSE);
   if (safmin<1E-6) return TMath::Abs(safmin); // point on radius-changing plane
   saftmp = 0.;
   // check increasing iplanes
   iplane = ipl+1;
   saftmp = 0.;
   while ((iplane<fNz-1) && saftmp<1E10) {
      saftmp = TMath::Abs(SafetyToSegment(point,iplane,kFALSE,safmin));
      if (saftmp<safmin) safmin=saftmp;
      iplane++;
   }   
   // now decreasing nplanes
   iplane = ipl-1;
   saftmp = 0.;
   while ((iplane>=0) && saftmp<1E10) {
      saftmp = TMath::Abs(SafetyToSegment(point,iplane,kFALSE,safmin));
      if (saftmp<safmin) safmin=saftmp;
      iplane--;
   }   
   return safmin;
}

//_____________________________________________________________________________
void TGeoPcon::SavePrimitive(ostream &out, Option_t * /*option*/ /*= ""*/)
{
// Save a primitive as a C++ statement(s) on output stream "out".
   if (TObject::TestBit(kGeoSavePrimitive)) return;
   out << "   // Shape: " << GetName() << " type: " << ClassName() << endl;
   out << "   phi1  = " << fPhi1 << ";" << endl;
   out << "   dphi  = " << fDphi << ";" << endl;
   out << "   nz    = " << fNz << ";" << endl;
   out << "   TGeoPcon *pcon = new TGeoPcon(\"" << GetName() << "\",phi1,dphi,nz);" << endl;
   for (Int_t i=0; i<fNz; i++) {
      out << "      z     = " << fZ[i] << ";" << endl;
      out << "      rmin  = " << fRmin[i] << ";" << endl;
      out << "      rmax  = " << fRmax[i] << ";" << endl;
      out << "   pcon->DefineSection(" << i << ", z,rmin,rmax);" << endl;
   }
   out << "   TGeoShape *" << GetPointerName() << " = pcon;" << endl;
   TObject::SetBit(TGeoShape::kGeoSavePrimitive);
}
         
//_____________________________________________________________________________
void TGeoPcon::SetDimensions(Double_t *param)
{
// Set polycone dimensions starting from an array.
   fPhi1    = param[0];
   fDphi    = param[1];
   fNz      = (Int_t)param[2];
   if (fNz<2) {
      Error("SetDimensions","Pcon %s: Number of Z sections must be > 2", GetName());
      return;
   } 
   if (fRmin) delete [] fRmin;  
   if (fRmax) delete [] fRmax;  
   if (fZ) delete [] fZ;  
   fRmin = new Double_t [fNz];
   fRmax = new Double_t [fNz];
   fZ    = new Double_t [fNz];
   memset(fRmin, 0, fNz*sizeof(Double_t));
   memset(fRmax, 0, fNz*sizeof(Double_t));
   memset(fZ, 0, fNz*sizeof(Double_t));
   for (Int_t i=0; i<fNz; i++) 
      DefineSection(i, param[3+3*i], param[4+3*i], param[5+3*i]);
}   

//_____________________________________________________________________________
void TGeoPcon::SetPoints(Double_t *points) const
{
// create polycone mesh points
   Double_t phi, dphi;
   Int_t n = gGeoManager->GetNsegments() + 1;
   dphi = fDphi/(n-1);
   Int_t i, j;
   Int_t indx = 0;

   if (points) {
      for (i = 0; i < fNz; i++) {
         for (j = 0; j < n; j++) {
            phi = (fPhi1+j*dphi)*TMath::DegToRad();
            points[indx++] = fRmin[i] * TMath::Cos(phi);
            points[indx++] = fRmin[i] * TMath::Sin(phi);
            points[indx++] = fZ[i];
         }
         for (j = 0; j < n; j++) {
            phi = (fPhi1+j*dphi)*TMath::DegToRad();
            points[indx++] = fRmax[i] * TMath::Cos(phi);
            points[indx++] = fRmax[i] * TMath::Sin(phi);
            points[indx++] = fZ[i];
         }
      }
   }
}

//_____________________________________________________________________________
void TGeoPcon::SetPoints(Float_t *points) const
{
// create polycone mesh points
   Double_t phi, dphi;
   Int_t n = gGeoManager->GetNsegments() + 1;
   dphi = fDphi/(n-1);
   Int_t i, j;
   Int_t indx = 0;

   if (points) {
      for (i = 0; i < fNz; i++) {
         for (j = 0; j < n; j++) {
            phi = (fPhi1+j*dphi)*TMath::DegToRad();
            points[indx++] = fRmin[i] * TMath::Cos(phi);
            points[indx++] = fRmin[i] * TMath::Sin(phi);
            points[indx++] = fZ[i];
         }
         for (j = 0; j < n; j++) {
            phi = (fPhi1+j*dphi)*TMath::DegToRad();
            points[indx++] = fRmax[i] * TMath::Cos(phi);
            points[indx++] = fRmax[i] * TMath::Sin(phi);
            points[indx++] = fZ[i];
         }
      }
   }
}
//_____________________________________________________________________________
Int_t TGeoPcon::GetNmeshVertices() const
{
// Return number of vertices of the mesh representation
   Int_t n = gGeoManager->GetNsegments()+1;
   Int_t numPoints = fNz*2*n;
   return numPoints;
}   

//_____________________________________________________________________________
void TGeoPcon::Sizeof3D() const
{
///// fill size of this 3-D object
///   TVirtualGeoPainter *painter = gGeoManager->GetGeomer();
///   if (!painter) return;
///    Int_t n;
   ///
///    n = gGeoManager->GetNsegments()+1;
   ///
///    Int_t numPoints = fNz*2*n;
///    Int_t numSegs   = 4*(fNz*n-1+(fDphi == 360));
///    Int_t numPolys  = 2*(fNz*n-1+(fDphi == 360));
///    painter->AddSize3D(numPoints, numSegs, numPolys);
}

//_____________________________________________________________________________
void TGeoPcon::GetMeshNumbers(Int_t &nvert, Int_t &nsegs, Int_t &npols) const
{
// Returns numbers of vertices, segments and polygons composing the shape mesh.
   Int_t n = gGeoManager->GetNsegments()+1;
   Int_t nz = GetNz();
   nvert = nz*2*n;
   Bool_t specialCase = TGeoShape::IsSameWithinTolerance(GetDphi(),360);
   nsegs = 4*(nz*n-1+(specialCase == kTRUE));
   npols = 2*(nz*n-1+(specialCase == kTRUE));
}

//_____________________________________________________________________________
const TBuffer3D & TGeoPcon::GetBuffer3D(Int_t reqSections, Bool_t localFrame) const
{
// Fills a static 3D buffer and returns a reference.
   static TBuffer3D buffer(TBuffer3DTypes::kGeneric);

   TGeoBBox::FillBuffer3D(buffer, reqSections, localFrame);

   if (reqSections & TBuffer3D::kRawSizes) {
      const Int_t n = gGeoManager->GetNsegments()+1;
      Int_t nz = GetNz();
      Int_t nbPnts = nz*2*n;
      if (nz >= 2 && nbPnts > 0) {
         Bool_t specialCase = TGeoShape::IsSameWithinTolerance(GetDphi(),360);
         Int_t nbSegs = 4*(nz*n-1+(specialCase == kTRUE));
         Int_t nbPols = 2*(nz*n-1+(specialCase == kTRUE));
         if (buffer.SetRawSizes(nbPnts, 3*nbPnts, nbSegs, 3*nbSegs, nbPols, 6*nbPols)) {
            buffer.SetSectionsValid(TBuffer3D::kRawSizes);
         }
      }
   }
   // TODO: Push down to TGeoShape?? Wuld have to do raw sizes set first..
   // can rest of TGeoShape be defered until after this?
   if ((reqSections & TBuffer3D::kRaw) && buffer.SectionsValid(TBuffer3D::kRawSizes)) {
      SetPoints(buffer.fPnts);
      if (!buffer.fLocalFrame) {
         TransformPoints(buffer.fPnts, buffer.NbPnts());
      }

      SetSegsAndPols(buffer);      
      buffer.SetSectionsValid(TBuffer3D::kRaw);
   }
      
   return buffer;
}
