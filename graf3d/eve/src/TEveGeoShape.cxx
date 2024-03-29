// @(#)root/eve:$Id$
// Author: Matevz Tadel 2007

/*************************************************************************
 * Copyright (C) 1995-2007, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TEveGeoShape.h"
#include "TEveTrans.h"
#include "TEveManager.h"
#include "TEvePolygonSetProjected.h"
#include "TEveProjections.h"
#include "TEveProjectionManager.h"

#include "TEveGeoShapeExtract.h"
#include "TEveGeoPolyShape.h"

#include "TROOT.h"
#include "TBuffer3D.h"
#include "TBuffer3DTypes.h"
#include "TVirtualViewer3D.h"
#include "TColor.h"
#include "TFile.h"

#include "TGeoShape.h"
#include "TGeoVolume.h"
#include "TGeoNode.h"
#include "TGeoShapeAssembly.h"
#include "TGeoCompositeShape.h"
#include "TGeoBoolNode.h"
#include "TGeoManager.h"
#include "TGeoMatrix.h"
#include "TVirtualGeoPainter.h"

namespace
{
   TGeoManager* init_geo_mangeur()
   {
      // Create a phony geo manager that can be used for storing free
      // shapes. Otherwise shapes register themselves to current
      // geo-manager (or even create one).

      TGeoManager  *old    = gGeoManager;
      TGeoIdentity *old_id = gGeoIdentity;
      gGeoManager = 0;
      TGeoManager* mgr = new TGeoManager();
      mgr->SetNameTitle("TEveGeoShape::fgGeoMangeur",
                        "Static geo manager used for wrapped TGeoShapes.");
      gGeoIdentity = new TGeoIdentity("Identity");
      gGeoManager  = old;
      gGeoIdentity = old_id;
      return mgr;
   }
}

//==============================================================================
//==============================================================================
// TEveGeoShape
//==============================================================================

//______________________________________________________________________________
//
// Wrapper for TGeoShape with absolute positioning and color
// attributes allowing display of extracted TGeoShape's (without an
// active TGeoManager) and simplified geometries (needed for non-linear
// projections).
//
// TGeoCompositeShapes and TGeoAssemblies are supported.
//
// If fNSegments data-member is < 2 (0 by default), the default number of
// segments is used for tesselation and special GL objects are
// instantiated for selected shapes (spheres, tubes). If fNSegments is > 2,
// it gets forwarded to geo-manager and this tesselation detail is
// used when creating the buffer passed to GL.

ClassImp(TEveGeoShape);

TGeoManager* TEveGeoShape::fgGeoMangeur = init_geo_mangeur();

//______________________________________________________________________________
TGeoManager* TEveGeoShape::GetGeoMangeur()
{
   // Return static geo-manager that is used intenally to make shapes
   // lead a happy life.
   // Set gGeoManager to this object when creating TGeoShapes to be
   // passed into TEveGeoShapes.

   return fgGeoMangeur;
}

//______________________________________________________________________________
TEveGeoShape::TEveGeoShape(const char* name, const char* title) :
   TEveShape       (name, title),
   fNSegments      (0),
   fShape          (0),
   fCompositeShape (0)
{
   // Constructor.

   InitMainTrans();
}

//______________________________________________________________________________
TEveGeoShape::~TEveGeoShape()
{
   // Destructor.

   SetShape(0);
}

//______________________________________________________________________________
TGeoShape* TEveGeoShape::MakePolyShape()
{
   // Create derived TEveGeoShape form a TGeoCompositeShape.

   return TEveGeoPolyShape::Construct(fCompositeShape, fNSegments);
}

//______________________________________________________________________________
void TEveGeoShape::SetNSegments(Int_t s)
{
   // Set number of segments.

   if (s != fNSegments && fCompositeShape != 0)
   {
      delete fShape;
      fShape = MakePolyShape();
   }
   fNSegments = s;
}

//______________________________________________________________________________
void TEveGeoShape::SetShape(TGeoShape* s)
{
   // Set TGeoShape shown by this object.
   //
   // The shape is owned by TEveGeoShape but TGeoShape::fUniqueID is
   // used for reference counting so you can pass the same shape to
   // several TEveGeoShapes.
   //
   // If it if is taken from an existing TGeoManager, manually
   // increase the fUniqueID before passing it to TEveGeoShape.

   TEveGeoManagerHolder gmgr(fgGeoMangeur);

   if (fCompositeShape)
   {
      delete fShape;
      fShape = fCompositeShape;
   }
   if (fShape)
   {
      fShape->SetUniqueID(fShape->GetUniqueID() - 1);
      if (fShape->GetUniqueID() == 0)
      {
         delete fShape;
      }
   }
   fShape = s;
   if (fShape)
   {
      fShape->SetUniqueID(fShape->GetUniqueID() + 1);
      fCompositeShape = dynamic_cast<TGeoCompositeShape*>(fShape);
      if (fCompositeShape)
      {
         fShape = MakePolyShape();
      }
   }
}

/******************************************************************************/

//______________________________________________________________________________
void TEveGeoShape::ComputeBBox()
{
   // Compute bounding-box.

   TGeoBBox *bb = dynamic_cast<TGeoBBox*>(fShape);
   if (bb)
   {
      BBoxInit();
      const Double_t *o = bb->GetOrigin();
      BBoxCheckPoint(o[0] - bb->GetDX(), o[0] - bb->GetDY(), o[0] - bb->GetDZ());
      BBoxCheckPoint(o[0] + bb->GetDX(), o[0] + bb->GetDY(), o[0] + bb->GetDZ());
   }
   else
   {
      BBoxZero();
   }
}

//______________________________________________________________________________
void TEveGeoShape::Paint(Option_t* /*option*/)
{
   // Paint object.

   static const TEveException eh("TEveGeoShape::Paint ");

   if (fShape == 0)
      return;

   TEveGeoManagerHolder gmgr(fgGeoMangeur, fNSegments);

   if (fCompositeShape)
   {
      Double_t halfLengths[3] = { fCompositeShape->GetDX(), fCompositeShape->GetDY(), fCompositeShape->GetDZ() };

      TBuffer3D buff(TBuffer3DTypes::kComposite);
      buff.fID           = this;
      buff.fColor        = GetMainColor();
      buff.fTransparency = GetMainTransparency();
      RefMainTrans().SetBuffer3D(buff);
      buff.fLocalFrame   = kTRUE; // Always enforce local frame (no geo manager).
      buff.SetAABoundingBox(fCompositeShape->GetOrigin(), halfLengths);
      buff.SetSectionsValid(TBuffer3D::kCore|TBuffer3D::kBoundingBox);

      Bool_t paintComponents = kTRUE;

      // Start a composite shape, identified by this buffer
      if (TBuffer3D::GetCSLevel() == 0)
         paintComponents = gPad->GetViewer3D()->OpenComposite(buff);

      TBuffer3D::IncCSLevel();

      // Paint the boolean node - will add more buffers to viewer
      TGeoHMatrix xxx;
      TGeoMatrix *gst = TGeoShape::GetTransform();
      TGeoShape::SetTransform(&xxx);
      if (paintComponents) fCompositeShape->GetBoolNode()->Paint("");
      TGeoShape::SetTransform(gst);
      // Close the composite shape
      if (TBuffer3D::DecCSLevel() == 0)
         gPad->GetViewer3D()->CloseComposite();
   }
   else
   {
      TBuffer3D& buff = (TBuffer3D&) fShape->GetBuffer3D
         (TBuffer3D::kCore, kFALSE);

      buff.fID           = this;
      buff.fColor        = GetMainColor();
      buff.fTransparency = GetMainTransparency();
      RefMainTrans().SetBuffer3D(buff);
      buff.fLocalFrame   = kTRUE; // Always enforce local frame (no geo manager).

      Int_t sections = TBuffer3D::kBoundingBox | TBuffer3D::kShapeSpecific;
      if (fNSegments > 2)
         sections |= TBuffer3D::kRawSizes | TBuffer3D::kRaw;
      fShape->GetBuffer3D(sections, kTRUE);

      Int_t reqSec = gPad->GetViewer3D()->AddObject(buff);

      if (reqSec != TBuffer3D::kNone) {
         // This shouldn't happen, but I suspect it does sometimes.
         if (reqSec & TBuffer3D::kCore)
            Warning(eh, "Core section required again for shape='%s'. This shouldn't happen.", GetName());
         fShape->GetBuffer3D(reqSec, kTRUE);
         reqSec = gPad->GetViewer3D()->AddObject(buff);
      }

      if (reqSec != TBuffer3D::kNone)
         Warning(eh, "Extra section required: reqSec=%d, shape=%s.", reqSec, GetName());
   }
}

//==============================================================================

//______________________________________________________________________________
void TEveGeoShape::Save(const char* file, const char* name)
{
   // Save the shape tree as TEveGeoShapeExtract.
   // File is always recreated.
   // This function is obsolete, use SaveExtractInstead().

   Warning("Save()", "This function is deprecated, use SaveExtract() instead.");
   SaveExtract(file, name);
}

//______________________________________________________________________________
void TEveGeoShape::SaveExtract(const char* file, const char* name)
{
   // Save the shape tree as TEveGeoShapeExtract.
   // File is always recreated.

   TEveGeoShapeExtract* gse = DumpShapeTree(this, 0);

   TFile f(file, "RECREATE");
   gse->Write(name);
   f.Close();
}

//______________________________________________________________________________
void TEveGeoShape::WriteExtract(const char* name)
{
   // Write the shape tree as TEveGeoShapeExtract to current directory.

   TEveGeoShapeExtract* gse = DumpShapeTree(this, 0);
   gse->Write(name);
}

/******************************************************************************/

//______________________________________________________________________________
TEveGeoShapeExtract* TEveGeoShape::DumpShapeTree(TEveGeoShape* gsre,
                                                 TEveGeoShapeExtract* parent)
{
   // Export this shape and its descendants into a geoshape-extract.

   TEveGeoShapeExtract* she = new TEveGeoShapeExtract(gsre->GetName(), gsre->GetTitle());
   she->SetTrans(gsre->RefMainTrans().Array());
   {
      Int_t   ci = gsre->GetFillColor();
      TColor *c  = gROOT->GetColor(ci);
      Float_t rgba[4] = { 1, 0, 0, Float_t(1 - gsre->GetMainTransparency()/100.) };
      if (c)
      {
         rgba[0] = c->GetRed();
         rgba[1] = c->GetGreen();
         rgba[2] = c->GetBlue();
      }
      she->SetRGBA(rgba);
   }
   {
      Int_t   ci = gsre->GetLineColor();
      TColor *c  = gROOT->GetColor(ci);
      Float_t rgba[4] = { 1, 0, 0, 1 };
      if (c)
      {
         rgba[0] = c->GetRed();
         rgba[1] = c->GetGreen();
         rgba[2] = c->GetBlue();
      }
      she->SetRGBALine(rgba);
   }
   she->SetRnrSelf(gsre->GetRnrSelf());
   she->SetRnrElements(gsre->GetRnrChildren());
   she->SetRnrFrame(gsre->GetDrawFrame());
   she->SetMiniFrame(gsre->GetMiniFrame());
   she->SetShape(gsre->GetShape());
   if (gsre->HasChildren())
   {
      TList* ele = new TList();
      she->SetElements(ele);
      she->GetElements()->SetOwner(true);
      TEveElement::List_i i = gsre->BeginChildren();
      while (i != gsre->EndChildren()) {
         TEveGeoShape* l = dynamic_cast<TEveGeoShape*>(*i);
         DumpShapeTree(l, she);
         i++;
      }
   }
   if (parent)
      parent->GetElements()->Add(she);

   return she;
}

//______________________________________________________________________________
TEveGeoShape* TEveGeoShape::ImportShapeExtract(TEveGeoShapeExtract* gse,
                                               TEveElement*         parent)
{
   // Import a shape extract 'gse' under element 'parent'.

   TEveGeoManagerHolder gmgr(fgGeoMangeur);
   TEveManager::TRedrawDisabler redrawOff(gEve);
   TEveGeoShape* gsre = SubImportShapeExtract(gse, parent);
   gsre->ElementChanged();
   return gsre;
}


//______________________________________________________________________________
TEveGeoShape* TEveGeoShape::SubImportShapeExtract(TEveGeoShapeExtract* gse,
                                                  TEveElement*         parent)
{
   // Recursive version for importing a shape extract tree.

   TEveGeoShape* gsre = new TEveGeoShape(gse->GetName(), gse->GetTitle());
   gsre->RefMainTrans().SetFromArray(gse->GetTrans());
   const Float_t* rgba = gse->GetRGBA();
   gsre->SetMainColorRGB(rgba[0], rgba[1], rgba[2]);
   gsre->SetMainAlpha(rgba[3]);
   rgba = gse->GetRGBALine();
   gsre->SetLineColor(TColor::GetColor(rgba[0], rgba[1], rgba[2]));
   gsre->SetRnrSelf(gse->GetRnrSelf());
   gsre->SetRnrChildren(gse->GetRnrElements());
   gsre->SetDrawFrame(gse->GetRnrFrame());
   gsre->SetMiniFrame(gse->GetMiniFrame());
   gsre->SetShape(gse->GetShape());

   if (parent)
      parent->AddElement(gsre);

   if (gse->HasElements())
   {
      TIter next(gse->GetElements());
      TEveGeoShapeExtract* chld;
      while ((chld = (TEveGeoShapeExtract*) next()) != 0)
         SubImportShapeExtract(chld, gsre);
   }

   return gsre;
}

/******************************************************************************/

//______________________________________________________________________________
TClass* TEveGeoShape::ProjectedClass(const TEveProjection* p) const
{
   // Return class for projected objects:
   //  - 2D projections: TEvePolygonSetProjected,
   //  - 3D projections: TEveGeoShapeProjected.
   // Virtual from TEveProjectable.

   if (p->Is2D())
      return TEvePolygonSetProjected::Class();
   else
      return TEveGeoShapeProjected::Class();
}

/******************************************************************************/

//______________________________________________________________________________
TBuffer3D* TEveGeoShape::MakeBuffer3D()
{
   // Create a TBuffer3D suitable for presentation of the shape.
   // Transformation matrix is also applied.

   if (fShape == 0) return 0;

   if (dynamic_cast<TGeoShapeAssembly*>(fShape)) {
      // TGeoShapeAssembly makes a bad TBuffer3D.
      return 0;
   }

   TEveGeoManagerHolder gmgr(fgGeoMangeur, fNSegments);

   TBuffer3D* buff  = fShape->MakeBuffer3D();
   TEveTrans& mx    = RefMainTrans();
   if (mx.GetUseTrans())
   {
      Int_t n = buff->NbPnts();
      Double_t* pnts = buff->fPnts;
      for(Int_t k = 0; k < n; ++k)
      {
         mx.MultiplyIP(&pnts[3*k]);
      }
   }
   return buff;
}


//==============================================================================
//==============================================================================
// TEveGeoShapeProjected
//==============================================================================

//______________________________________________________________________________
//
// A 3D projected TEveGeoShape.

ClassImp(TEveGeoShapeProjected);

//______________________________________________________________________________
TEveGeoShapeProjected::TEveGeoShapeProjected() :
   TEveShape("TEveGeoShapeProjected"),
   fBuff(0)
{
   // Constructor.
}

//______________________________________________________________________________
void TEveGeoShapeProjected::SetDepthLocal(Float_t /*d*/)
{
   // This should never be called as this class is only used for 3D
   // projections.
   // The implementation is required as this metod is abstract.
   // Just emits a warning if called.

   Warning("SetDepthLocal", "This function only exists to fulfill an abstract interface.");
}

//______________________________________________________________________________
void TEveGeoShapeProjected::SetProjection(TEveProjectionManager* mng,
                                          TEveProjectable* model)
{
   // This is virtual method from base-class TEveProjected.

   TEveProjected::SetProjection(mng, model);

   TEveGeoShape* gre = dynamic_cast<TEveGeoShape*>(fProjectable);
   CopyVizParams(gre);
}

//______________________________________________________________________________
void TEveGeoShapeProjected::UpdateProjection()
{
   // This is virtual method from base-class TEveProjected.

   TEveGeoShape   *gre = dynamic_cast<TEveGeoShape*>(fProjectable);
   TEveProjection *prj = fManager->GetProjection();

   delete fBuff;
   fBuff = gre->MakeBuffer3D();

   if (fBuff)
   {
      fBuff->SetSectionsValid(TBuffer3D::kCore | TBuffer3D::kRawSizes | TBuffer3D::kRaw);

      Double_t *p = fBuff->fPnts;
      for (UInt_t i = 0; i < fBuff->NbPnts(); ++i, p+=3)
      {
         prj->ProjectPointdv(p, 0);
      }
   }

   ResetBBox();
}

//______________________________________________________________________________
void TEveGeoShapeProjected::ComputeBBox()
{
   // Override of virtual method from TAttBBox.

   if (fBuff && fBuff->NbPnts() > 0)
   {
      BBoxInit();

      Double_t *p = fBuff->fPnts;
      for (UInt_t i = 0; i < fBuff->NbPnts(); ++i, p+=3)
      {
         BBoxCheckPoint(p[0], p[1], p[2]);
      }
   }
   else
   {
      BBoxZero();
   }
}
