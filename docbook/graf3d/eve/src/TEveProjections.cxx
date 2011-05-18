// @(#)root/eve:$Id$
// Authors: Matevz Tadel & Alja Mrak-Tadel: 2006, 2007

/*************************************************************************
 * Copyright (C) 1995-2007, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TEveProjections.h"
#include "TEveTrans.h"
#include "TEveUtil.h"

#include <limits>

//==============================================================================
//==============================================================================
// TEveProjection
//==============================================================================

//______________________________________________________________________________
//
// Base-class for non-linear projections.
//
// Enables to define an external center of distortion and a scale to
// fixate a bounding box of a projected point.

ClassImp(TEveProjection);

Float_t TEveProjection::fgEps    = 0.005f;
Float_t TEveProjection::fgEpsSqr = 0.000025f;

//______________________________________________________________________________
TEveProjection::TEveProjection() :
   fType          (kPT_Unknown),
   fGeoMode       (kGM_Unknown),
   fName          (0),
   fCenter        (),
   fUsePreScale   (kFALSE),
   fDistortion    (0.0f),
   fFixR          (300), fFixZ          (400),
   fPastFixRFac   (0),   fPastFixZFac   (0),
   fScaleR        (1),   fScaleZ        (1),
   fPastFixRScale (1),   fPastFixZScale (1),
   fMaxTrackStep  (5),
   fLowLimit(-std::numeric_limits<Float_t>::infinity(),
             -std::numeric_limits<Float_t>::infinity(),
             -std::numeric_limits<Float_t>::infinity()),
   fUpLimit ( std::numeric_limits<Float_t>::infinity(),
              std::numeric_limits<Float_t>::infinity(),
              std::numeric_limits<Float_t>::infinity())

{
   // Constructor.
}

//______________________________________________________________________________
void TEveProjection::ProjectPointfv(Float_t* v, Float_t d)
{
   // Project float array.

   ProjectPoint(v[0], v[1], v[2], d);
}

//______________________________________________________________________________
void TEveProjection::ProjectPointdv(Double_t* v, Float_t d)
{
   // Project double array.
   // This is a bit piggish as we convert the doubles to floats and back.

   Float_t x = v[0], y = v[1], z = v[2];
   ProjectPoint(x, y, z, d);
   v[0] = x; v[1] = y; v[2] = z;
}

//______________________________________________________________________________
void TEveProjection::ProjectVector(TEveVector& v, Float_t d)
{
   // Project TEveVector.

   ProjectPoint(v.fX, v.fY, v.fZ, d);
}

//______________________________________________________________________________
void TEveProjection::ProjectPointfv(const TEveTrans* t, const Float_t* p, Float_t* v, Float_t d)
{
   // Project float array, converting it to global coordinate system first if
   // transformation matrix is set.

   v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
   if (t)
   {
      t->MultiplyIP(v);
   }
   ProjectPoint(v[0], v[1], v[2], d);
}

//______________________________________________________________________________
void TEveProjection::ProjectPointdv(const TEveTrans* t, const Double_t* p, Double_t* v, Float_t d)
{
   // Project double array, converting it to global coordinate system first if
   // transformation matrix is set.
   // This is a bit piggish as we convert the doubles to floats and back.

   Float_t x, y, z;
   if (t)
   {
      t->Multiply(p, v);
      x = v[0]; y = v[1]; z = v[2];
   }
   else
   {
      x = p[0]; y = p[1]; z = p[2];
   }
   ProjectPoint(x, y, z, d);
   v[0] = x; v[1] = y; v[2] = z;
}

//______________________________________________________________________________
void TEveProjection::ProjectVector(const TEveTrans* t, TEveVector& v, Float_t d)
{
   // Project TEveVector, converting it to global coordinate system first if
   // transformation matrix is set.

   if (t)
   {
      t->MultiplyIP(v);
   }
   ProjectPoint(v.fX, v.fY, v.fZ, d);
}

//______________________________________________________________________________
void TEveProjection::PreScaleVariable(Int_t dim, Float_t& v)
{
   // Pre-scale single variable with pre-scale entry dim.

   if (!fPreScales[dim].empty())
   {
      Bool_t invp = kFALSE;
      if (v < 0) {
         v    = -v;
         invp = kTRUE;
      }
      vPreScale_i i = fPreScales[dim].begin();
      while (v > i->fMax)
         ++i;
      v = i->fOffset + (v - i->fMin)*i->fScale;
      if (invp)
         v = -v;
   }
}

//______________________________________________________________________________
void TEveProjection::PreScalePoint(Float_t& x, Float_t& y)
{
   // Pre-scale point (x, y) in projected coordinates for 2D projections:
   //   RhoZ ~ (rho, z)
   //   RPhi ~ (r, phi), scaling phi doesn't make much sense.

   PreScaleVariable(0, x);
   PreScaleVariable(1, y);
}

//______________________________________________________________________________
void TEveProjection::PreScalePoint(Float_t& x, Float_t& y, Float_t& z)
{
   // Pre-scale point (x, y, z) in projected coordinates for 3D projection.

   PreScaleVariable(0, x);
   PreScaleVariable(1, y);
   PreScaleVariable(2, z);
}

//______________________________________________________________________________
void TEveProjection::AddPreScaleEntry(Int_t coord, Float_t value, Float_t scale)
{
   // Add new scaling range for given coordinate.
   // Arguments:
   //  coord    0 ~ x, 1 ~ y, 2 ~ z
   //  value    value of input coordinate from which to apply this scale;
   //  scale    the scale to apply from value onwards.
   //
   // NOTE: If pre-scaling is combined with center-displaced then
   // the scale of the central region should be 1. This limitation
   // can be removed but will cost CPU.

   static const TEveException eh("TEveProjection::AddPreScaleEntry ");

   if (coord < 0 || coord > 2)
      throw (eh + "coordinate out of range.");

   const Float_t infty  = std::numeric_limits<Float_t>::infinity();

   vPreScale_t& vec = fPreScales[coord];

   if (vec.empty())
   {
      if (value == 0)
      {
         vec.push_back(PreScaleEntry_t(0, infty, 0, scale));
      }
      else
      {
         vec.push_back(PreScaleEntry_t(0, value, 0, 1));
         vec.push_back(PreScaleEntry_t(value, infty, value, scale));
      }
   }
   else
   {
      PreScaleEntry_t& prev = vec.back();
      if (value <= prev.fMin)
         throw (eh + "minimum value not larger than previous one.");

      prev.fMax = value;
      Float_t offset =  prev.fOffset + (prev.fMax - prev.fMin)*prev.fScale;
      vec.push_back(PreScaleEntry_t(value, infty, offset, scale));
   }
}

//______________________________________________________________________________
void TEveProjection::ChangePreScaleEntry(Int_t   coord, Int_t entry,
                                         Float_t new_scale)
{
   // Change scale for given entry and coordinate.
   //
   // NOTE: If the first entry you created used other value than 0,
   // one entry (covering range from 0 to this value) was created
   // automatically.

   static const TEveException eh("TEveProjection::ChangePreScaleEntry ");

   if (coord < 0 || coord > 2)
      throw (eh + "coordinate out of range.");

   vPreScale_t& vec = fPreScales[coord];
   Int_t        vs  = vec.size();
   if (entry < 0 || entry >= vs)
      throw (eh + "entry out of range.");

   vec[entry].fScale = new_scale;
   Int_t i0 = entry, i1 = entry + 1;
   while (i1 < vs)
   {
      PreScaleEntry_t e0 = vec[i0];
      vec[i1].fOffset = e0.fOffset + (e0.fMax - e0.fMin)*e0.fScale;
      i0 = i1++;
   }
}

//______________________________________________________________________________
void TEveProjection::ClearPreScales()
{
   // Clear all pre-scaling information.

   fPreScales[0].clear();
   fPreScales[1].clear();
   fPreScales[2].clear();
}

//______________________________________________________________________________
void TEveProjection::UpdateLimit()
{
   // Update convergence in +inf and -inf.

   if (fDistortion == 0.0f)
      return;

   Float_t lim = 1.0f/fDistortion + fFixR;
   Float_t *c  = GetProjectedCenter();
   fUpLimit .Set( lim + c[0],  lim + c[1], c[2]);
   fLowLimit.Set(-lim + c[0], -lim + c[1], c[2]);
}

//______________________________________________________________________________
void TEveProjection::SetDistortion(Float_t d)
{
   // Set distortion.

   fDistortion    = d;
   fScaleR        = 1.0f + fFixR*fDistortion;
   fScaleZ        = 1.0f + fFixZ*fDistortion;
   fPastFixRScale = TMath::Power(10.0f, fPastFixRFac) / fScaleR;
   fPastFixZScale = TMath::Power(10.0f, fPastFixZFac) / fScaleZ;
   UpdateLimit();
}

//______________________________________________________________________________
void TEveProjection::SetFixR(Float_t r)
{
   // Set fixed radius.

   fFixR          = r;
   fScaleR        = 1 + fFixR*fDistortion;
   fPastFixRScale = TMath::Power(10.0f, fPastFixRFac) / fScaleR;
   UpdateLimit();
}

//______________________________________________________________________________
void TEveProjection::SetFixZ(Float_t z)
{
   // Set fixed radius.

   fFixZ          = z;
   fScaleZ        = 1 + fFixZ*fDistortion;
   fPastFixZScale = TMath::Power(10.0f, fPastFixZFac) / fScaleZ;
   UpdateLimit();
}

//______________________________________________________________________________
void TEveProjection::SetPastFixRFac(Float_t x)
{
   // Set 2's-exponent for relative scaling beyond FixR.

   fPastFixRFac   = x;
   fPastFixRScale = TMath::Power(10.0f, fPastFixRFac) / fScaleR;
}

//______________________________________________________________________________
void TEveProjection::SetPastFixZFac(Float_t x)
{
   // Set 2's-exponent for relative scaling beyond FixZ.

   fPastFixZFac   = x;
   fPastFixZScale = TMath::Power(10.0f, fPastFixZFac) / fScaleZ;
}

//______________________________________________________________________________
void TEveProjection::BisectBreakPoint(TEveVector& vL, TEveVector& vR, Float_t eps_sqr)
{
   // Find break-point on both sides of the discontinuity.
   // They still need to be projected.

   TEveVector vM, vLP, vMP;
   while ((vL-vR).Mag2() > eps_sqr)
   {
      vM.Mult(vL+vR, 0.5f);
      vLP.Set(vL); ProjectPoint(vLP.fX, vLP.fY, vLP.fZ, 0);
      vMP.Set(vM); ProjectPoint(vMP.fX, vMP.fY, vMP.fZ, 0);

      if (IsOnSubSpaceBoundrary(vMP))
      {
         vL.Set(vM);
         vR.Set(vM);
         return;
      }

      if (AcceptSegment(vLP, vMP, 0.0f))
         vL.Set(vM);
      else
         vR.Set(vM);
   }
}

//______________________________________________________________________________
void TEveProjection::SetDirectionalVector(Int_t screenAxis, TEveVector& vec)
{
   // Get vector for axis in a projected space.

   for (Int_t i=0; i<3; i++)
   {
      vec[i] = (i==screenAxis) ? 1.0f : 0.0f;
   }
}

//______________________________________________________________________________
Float_t TEveProjection::GetValForScreenPos(Int_t i, Float_t sv)
{
   // Inverse projection.

   static const TEveException eH("TEveProjection::GetValForScreenPos ");

   Float_t xL, xM, xR;
   TEveVector vec;
   TEveVector dirVec;
   SetDirectionalVector(i, dirVec);
   if (fDistortion > 0.0f && ((sv > 0 && sv > fUpLimit[i]) || (sv < 0 && sv < fLowLimit[i])))
      throw(eH + Form("screen value '%f' out of limit '%f'.", sv, sv > 0 ? fUpLimit[i] : fLowLimit[i]));

   TEveVector zero; ProjectVector(zero, 0);
   // search from -/+ infinity according to sign of screen value
   if (sv > zero[i])
   {
      xL = 0; xR = 1000;
      while (1)
      {
         vec.Mult(dirVec, xR); ProjectVector(vec, 0);
         // printf("positive projected %f, value %f,xL, xR ( %f, %f)\n", vec[i], sv, xL, xR);
         if (vec[i] > sv || vec[i] == sv) break;
         xL = xR; xR *= 2;
      }
   }
   else if (sv < zero[i])
   {
      xR = 0; xL = -1000;
      while (1)
      {
         vec.Mult(dirVec, xL); ProjectVector(vec, 0);
         // printf("negative projected %f, value %f,xL, xR ( %f, %f)\n", vec[i], sv, xL, xR);
         if (vec[i] < sv || vec[i] == sv) break;
         xR = xL; xL *= 2;
      }
   }
   else
   {
      return 0.0f;
   }

   do
   {
      xM = 0.5f * (xL + xR);
      vec.Mult(dirVec, xM);
      ProjectVector(vec, 0);
      // printf("safr xL=%f, xR=%f; vec[i]=%f, sv=%f\n", xL, xR, vec[i], sv);
      if (vec[i] > sv)
         xR = xM;
      else
         xL = xM;
   } while (TMath::Abs(vec[i] - sv) >= fgEps);

   return xM;
}

//______________________________________________________________________________
Float_t TEveProjection::GetScreenVal(Int_t i, Float_t x)
{
   // Project point on given axis and return projected value.

   TEveVector dv;
   SetDirectionalVector(i, dv); dv = dv*x;
   ProjectVector(dv, 0);
   return dv[i];
}


//==============================================================================
//==============================================================================
// TEveRhoZProjection
//==============================================================================

//______________________________________________________________________________
//
// Transformation from 3D to 2D. X axis represent Z coordinate. Y axis have value of
// radius with a sign of Y coordinate.

ClassImp(TEveRhoZProjection);

//______________________________________________________________________________
TEveRhoZProjection::TEveRhoZProjection() :
   TEveProjection()
{
   // Constructor.

   fType = kPT_RhoZ;
   fName = "RhoZ";
}

//______________________________________________________________________________
void TEveRhoZProjection::ProjectPoint(Float_t& x, Float_t& y, Float_t& z,
                                      Float_t  d, EPProc_e proc)
{
   // Project point.

   using namespace TMath;

   if (proc == kPP_Plane || proc == kPP_Full)
   {
      // project
      y = Sign((Float_t)Sqrt(x*x+y*y), y);
      x = z;
   }
   if (proc == kPP_Distort || proc == kPP_Full)
   {
      if (fUsePreScale)
         PreScalePoint(y, x);

      // move to center
      x -= fProjectedCenter.fX;
      y -= fProjectedCenter.fY;

      // distort
      if (x > fFixZ)
         x =  fFixZ + fPastFixZScale*(x - fFixZ);
      else if (x < -fFixZ)
         x = -fFixZ + fPastFixZScale*(x + fFixZ);
      else
         x =  x * fScaleZ / (1.0f + Abs(x)*fDistortion);

      if (y > fFixR)
         y =  fFixR + fPastFixRScale*(y - fFixR);
      else if (y < -fFixR)
         y = -fFixR + fPastFixRScale*(y + fFixR);
      else
         y =  y * fScaleR / (1.0f + Abs(y)*fDistortion);

      // move back from center
      x += fProjectedCenter.fX;
      y += fProjectedCenter.fY;
   }
   z = d;
}

//______________________________________________________________________________
void TEveRhoZProjection::SetCenter(TEveVector& v)
{
   // Set center of distortion (virtual method).

   fCenter = v;

   Float_t r = TMath::Sqrt(v.fX*v.fX + v.fY*v.fY);
   fProjectedCenter.fX = fCenter.fZ;
   fProjectedCenter.fY = TMath::Sign(r, fCenter.fY);
   fProjectedCenter.fZ = 0;
   UpdateLimit();
}

//______________________________________________________________________________
void TEveRhoZProjection::UpdateLimit()
{
   // Update convergence in +inf and -inf.

   if (fDistortion == 0.0f)
      return;

   Float_t limR = 1.0f/fDistortion + fFixR;
   Float_t limZ = 1.0f/fDistortion + fFixZ;
   Float_t *c   = GetProjectedCenter();
   fUpLimit .Set( limZ + c[0],  limR + c[1], c[2]);
   fLowLimit.Set(-limZ + c[0], -limR + c[1], c[2]);
}

//______________________________________________________________________________
void TEveRhoZProjection::SetDirectionalVector(Int_t screenAxis, TEveVector& vec)
{
   // Get direction in the unprojected space for axis index in the
   // projected space.
   // This is virtual method from base-class TEveProjection.

   if (screenAxis == 0)
      vec.Set(0.0f, 0.0f, 1.0f);
   else if (screenAxis == 1)
      vec.Set(0.0f, 1.0f, 0.0f);

}
//______________________________________________________________________________
Bool_t TEveRhoZProjection::AcceptSegment(TEveVector& v1, TEveVector& v2,
                                         Float_t tolerance) const
{
   // Check if segment of two projected points is valid.
   //
   // Move slightly one of the points if by shifting it by no more than
   // tolearance the segment can become acceptable.

   Float_t a = fProjectedCenter.fY;
   Bool_t val = kTRUE;
   if ((v1.fY <  a && v2.fY > a) || (v1.fY > a && v2.fY < a))
   {
      val = kFALSE;
      if (tolerance > 0)
      {
         Float_t a1 = TMath::Abs(v1.fY - a), a2 = TMath::Abs(v2.fY - a);
         if (a1 < a2)
         {
            if (a1 < tolerance) { v1.fY = a; val = kTRUE; }
         }
         else
         {
            if (a2 < tolerance) { v2.fY = a; val = kTRUE; }
         }
      }
   }
   return val;
}

//______________________________________________________________________________
Int_t TEveRhoZProjection::SubSpaceId(const TEveVector& v) const
{
   // Return sub-space id for the point.
   // 0 - upper half-space
   // 1 - lowwer half-space

   return v.fY > fProjectedCenter.fY ? 0 : 1;
}

//______________________________________________________________________________
Bool_t TEveRhoZProjection::IsOnSubSpaceBoundrary(const TEveVector& v) const
{
   // Checks if point is on sub-space boundrary.

   return v.fY == fProjectedCenter.fY;
}

//==============================================================================
//==============================================================================
// TEveRPhiProjection
//==============================================================================

//______________________________________________________________________________
//
// XY projection with distortion around given center.

ClassImp(TEveRPhiProjection);

//______________________________________________________________________________
TEveRPhiProjection::TEveRPhiProjection() :
   TEveProjection()
{
   // Constructor.

   fType    = kPT_RPhi;
   fGeoMode = kGM_Polygons;
   fName    = "RhoPhi";
}

//______________________________________________________________________________
void TEveRPhiProjection::ProjectPoint(Float_t& x, Float_t& y, Float_t& z,
                                      Float_t d, EPProc_e proc)
{
   // Project point.

   using namespace TMath;

   if (proc != kPP_Plane)
   {
      Float_t r, phi;
      if (fUsePreScale)
      {
         r   = Sqrt(x*x + y*y);
         phi = (x == 0.0f && y == 0.0f) ? 0.0f : ATan2(y, x);
         PreScalePoint(r, phi);
         x = r*Cos(phi);
         y = r*Sin(phi);
      }

      x  -= fCenter.fX;
      y  -= fCenter.fY;
      r   = Sqrt(x*x + y*y);
      phi = (x == 0.0f && y == 0.0f) ? 0.0f : ATan2(y, x);

      if (r > fFixR)
         r =  fFixR + fPastFixRScale*(r - fFixR);
      else if (r < -fFixR)
         r = -fFixR + fPastFixRScale*(r + fFixR);
      else
         r =  r * fScaleR / (1.0f + r*fDistortion);

      x = r*Cos(phi) + fCenter.fX;
      y = r*Sin(phi) + fCenter.fY;
   }
   z = d;
}


//==============================================================================
//==============================================================================
// TEve3DProjection
//==============================================================================

//______________________________________________________________________________
//
// 3D scaling projection. One has to use pre-scaling to make any ise of this.

ClassImp(TEve3DProjection);

//______________________________________________________________________________
TEve3DProjection::TEve3DProjection() :
   TEveProjection()
{
   // Constructor.

   fType    = kPT_3D;
   fGeoMode = kGM_Unknown;
   fName    = "3D";
}

//______________________________________________________________________________
void TEve3DProjection::ProjectPoint(Float_t& x, Float_t& y, Float_t& z,
                                    Float_t /*d*/, EPProc_e proc)
{
   // Project point.

   using namespace TMath;

   if (proc != kPP_Plane)
   {
      if (fUsePreScale)
      {
         PreScalePoint(x, y, z);
      }

      x -= fCenter.fX;
      y -= fCenter.fY;
      z -= fCenter.fZ;
   }
}
