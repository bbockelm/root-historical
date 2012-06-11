// @(#)root/graf2d:$Id$
// Author: Timur Pocheptsov   22/11/2011

/*************************************************************************
 * Copyright (C) 1995-2012, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//#define NDEBUG

#include <algorithm>
#include <stdexcept>
#include <cassert>
#include <cstring>
#include <cstddef>
#include <limits>

#include <OpenGL/OpenGL.h>
#include <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>

#include "ROOTOpenGLView.h"
#include "CocoaPrivate.h"
#include "QuartzWindow.h"
#include "QuartzPixmap.h"
#include "QuartzUtils.h"
#include "X11Drawable.h"
#include "QuartzText.h"
#include "CocoaUtils.h"
#include "X11Events.h"
#include "X11Buffer.h"
#include "TGLFormat.h"
#include "TGClient.h"
#include "TGWindow.h"
#include "TGFrame.h"
#include "TGCocoa.h"
#include "TError.h"
#include "TColor.h"
#include "TROOT.h"

ClassImp(TGCocoa)

//Style notes: I'm using a lot of asserts to check pre-conditions - mainly function parameters.
//In asserts, expression always looks like 'p != 0' for "C++ pointer" (either object of built-in type
//or C++ class), and 'p != nil' for object from Objective-C. There is no difference, this is to make
//asserts more explicit. In conditional statement, it'll always be 'if (p)'  or 'if (!p)' for both
//C++ and Objective-C.
//I never use const qualifier for pointers to Objective-C objects since they are useless:
//there are no cv-qualified methods (member-functions in C++) in Objective-C, and I do not use
//'->' operator to access instance variables (data-members in C++) of Objective-C's object.

namespace Details = ROOT::MacOSX::Details;
namespace Util = ROOT::MacOSX::Util;
namespace X11 = ROOT::MacOSX::X11;
namespace Quartz = ROOT::Quartz;
namespace OpenGL = ROOT::MacOSX::OpenGL;

namespace {

//Aux. functions called from GUI-rendering part.

//______________________________________________________________________________
void SetStrokeForegroundColorFromX11Context(CGContextRef ctx, const GCValues_t &gcVals)
{
   assert(ctx != 0 && "SetStrokeForegroundColorFromX11Context, ctx parameter is null");
   
   CGFloat rgb[3] = {};
   if (gcVals.fMask & kGCForeground)
      X11::PixelToRGB(gcVals.fForeground, rgb);
   else
      ::Warning("SetStrokeForegroundColorFromX11Context", "x11 context does not have line color information");

   CGContextSetRGBStrokeColor(ctx, rgb[0], rgb[1], rgb[2], 1.);
}

//______________________________________________________________________________
void SetStrokeDashFromX11Context(CGContextRef ctx, const GCValues_t &gcVals)
{
   //Set line dash pattern (X11's LineOnOffDash line style).
   assert(ctx != 0 && "SetStrokeDashFromX11Context, ctx parameter is null");

   SetStrokeForegroundColorFromX11Context(ctx, gcVals);
   
   static const std::size_t maxLength = sizeof gcVals.fDashes / sizeof gcVals.fDashes[0];   
   assert(maxLength >= std::size_t(gcVals.fDashLen) && "SetStrokeDashFromX11Context, x11 context has bad dash length > sizeof(fDashes)");

   CGFloat dashes[maxLength] = {};
   for (Int_t i = 0; i < gcVals.fDashLen; ++i)
      dashes[i] = gcVals.fDashes[i];
   
   CGContextSetLineDash(ctx, gcVals.fDashOffset, dashes, gcVals.fDashLen);
}

//______________________________________________________________________________
void SetStrokeDoubleDashFromX11Context(CGContextRef /*ctx*/, const GCValues_t & /*gcVals*/)
{
   //assert(ctx != 0 && "SetStrokeDoubleDashFromX11Context, ctx parameter is null");
   ::Warning("SetStrokeDoubleDashFromX11Context", "Not implemented yet, kick tpochep!");
}

//______________________________________________________________________________
void SetStrokeParametersFromX11Context(CGContextRef ctx, const GCValues_t &gcVals)
{
   //Set line width and color from GCValues_t object.
   //(GUI rendering).
   assert(ctx != 0 && "SetStrokeParametersFromX11Context, ctx parameter is null");

   const Mask_t mask = gcVals.fMask;
   if ((mask & kGCLineWidth) && gcVals.fLineWidth > 1)
      CGContextSetLineWidth(ctx, gcVals.fLineWidth);
   else
      CGContextSetLineWidth(ctx, 1.);

   CGContextSetLineDash(ctx, 0., 0, 0);

   if (mask & kGCLineStyle) {
      if (gcVals.fLineStyle == kLineSolid)
         SetStrokeForegroundColorFromX11Context(ctx, gcVals);
      else if (gcVals.fLineStyle == kLineOnOffDash)
         SetStrokeDashFromX11Context(ctx, gcVals);
      else if (gcVals.fLineStyle == kLineDoubleDash)
         SetStrokeDoubleDashFromX11Context(ctx ,gcVals);
      else {
         ::Warning("SetStrokeParametersFromX11Context", "line style bit is set, but line style is unknown");
         SetStrokeForegroundColorFromX11Context(ctx, gcVals);
      }
   } else 
      SetStrokeForegroundColorFromX11Context(ctx, gcVals);
}

//______________________________________________________________________________
void SetFilledAreaColorFromX11Context(CGContextRef ctx, const GCValues_t &gcVals)
{
   //Set fill color from "foreground" pixel color.
   //(GUI rendering).
   assert(ctx != 0 && "SetFilledAreaColorFromX11Context, context parameter is null");
   
   CGFloat rgb[3] = {};
   if (gcVals.fMask & kGCForeground)
      X11::PixelToRGB(gcVals.fForeground, rgb);
   else
      ::Warning("SetFilledAreaColorFromX11Context", "no fill color found in x11 context");
   
   CGContextSetRGBFillColor(ctx, rgb[0], rgb[1], rgb[2], 1.);
}

struct PatternContext {
   Mask_t       fMask;
   ULong_t      fForeground;
   ULong_t      fBackground;
   QuartzImage *fImage;
   CGSize       fPhase;
};

//______________________________________________________________________________
void DrawPattern(void *info, CGContextRef ctx)
{
   //Pattern callback, either use foreground (and background, if any)
   //color and stipple mask to draw a pattern, or use pixmap
   //as a pattern image.
   //(GUI rendering).
   assert(info != 0 && "DrawPattern, info parameter is null");
   assert(ctx != 0 && "DrawPattern, ctx parameter is null");

   const PatternContext *patternContext = (PatternContext *)info;
   assert(patternContext->fImage != nil && "DrawPatter, pattern image is nil");

   QuartzImage *patternImage = patternContext->fImage;
   const CGRect patternRect = CGRectMake(0, 0, patternImage.fWidth, patternImage.fHeight);

   if (patternImage.fIsStippleMask) {
      if (patternContext->fMask & kGCBackground) {
         CGFloat rgb[3] = {};
         X11::PixelToRGB(patternContext->fBackground, rgb);
         CGContextSetRGBFillColor(ctx, rgb[0], rgb[1], rgb[2], 1.);
         CGContextFillRect(ctx, patternRect);
      }
      
      if (patternContext->fMask & kGCForeground) {
         CGFloat rgb[3] = {};
         X11::PixelToRGB(patternContext->fForeground, rgb);
         CGContextSetRGBFillColor(ctx, rgb[0], rgb[1], rgb[2], 1.);
         CGContextClipToMask(ctx, patternRect, patternImage.fImage);
         CGContextFillRect(ctx, patternRect);
      }
   } else {
      CGContextDrawImage(ctx, patternRect, patternImage.fImage);
   }
}

//______________________________________________________________________________
void SetFillPattern(CGContextRef ctx, const PatternContext *patternContext)
{
   //Create CGPatternRef to fill GUI elements with pattern.
   //Pattern is a QuartzImage object, it can be either a mask,
   //or pattern image itself.
   //(GUI-rendering).
   assert(ctx != 0 && "SetFillPattern, ctx parameter is null");
   assert(patternContext != 0 && "SetFillPattern, patternContext parameter is null");
   assert(patternContext->fImage != nil && "SetFillPattern, pattern image is nil");

   const Util::CFScopeGuard<CGColorSpaceRef> patternColorSpace(CGColorSpaceCreatePattern(0));
   CGContextSetFillColorSpace(ctx, patternColorSpace.Get());

   CGPatternCallbacks callbacks = {};
   callbacks.drawPattern = DrawPattern;
   const CGRect patternRect = CGRectMake(0, 0, patternContext->fImage.fWidth, patternContext->fImage.fHeight);
   const Util::CFScopeGuard<CGPatternRef> pattern(CGPatternCreate((void *)patternContext, patternRect, CGAffineTransformIdentity, 
                                                                  patternContext->fImage.fWidth, patternContext->fImage.fHeight, 
                                                                  kCGPatternTilingNoDistortion, true, &callbacks));
   const CGFloat alpha = 1.;
   CGContextSetFillPattern(ctx, pattern.Get(), &alpha);
   CGContextSetPatternPhase(ctx, patternContext->fPhase);
}

//______________________________________________________________________________
bool ParentRendersToChild(NSView<X11Window> *child)
{
   assert(child != nil && "ParentRendersToChild, child parameter is nil");

   //Adovo poluchaetsia, tashhem-ta! ;)
   return X11::ViewIsTextViewFrame(child, true) && !child.fContext && 
          child.fMapState == kIsViewable && child.fParentView.fContext &&
          !child.fIsOverlapped;
}

}

//______________________________________________________________________________
TGCocoa::TGCocoa()
            : fSelectedDrawable(0),
              fCocoaDraw(0),
              fDrawMode(kCopy),
              fDirectDraw(false),
              fForegroundProcess(false),
              fCurrentMessageID(1)
{
   fPimpl.reset(new Details::CocoaPrivate);
}

//______________________________________________________________________________
TGCocoa::TGCocoa(const char *name, const char *title)
            : TVirtualX(name, title),
              fSelectedDrawable(0),
              fCocoaDraw(0),
              fDrawMode(kCopy),
              fDirectDraw(false),
              fForegroundProcess(false),
              fCurrentMessageID(1)
{
   fPimpl.reset(new Details::CocoaPrivate);
}

//______________________________________________________________________________
TGCocoa::~TGCocoa()
{
   //
}

//General part (empty, since it's not an X server.

//______________________________________________________________________________
Bool_t TGCocoa::Init(void * /*display*/)
{
   //Nothing to initialize here, return true to make
   //a caller happy.
   return kTRUE;
}


//______________________________________________________________________________
Int_t TGCocoa::OpenDisplay(const char * /*dpyName*/)
{
   //Noop.
   return 0;
}

//______________________________________________________________________________
const char *TGCocoa::DisplayName(const char *)
{
   //Noop.
   return "dummy";
}

//______________________________________________________________________________
void TGCocoa::CloseDisplay()
{
   //Noop.
}

//______________________________________________________________________________
Display_t TGCocoa::GetDisplay() const
{
   //Noop.
   return 0;
}

//______________________________________________________________________________
Visual_t TGCocoa::GetVisual() const
{
   //Noop.
   return 0;
}

//______________________________________________________________________________
Int_t TGCocoa::GetScreen() const
{
   //Noop.
   return 0;
}

//______________________________________________________________________________
Int_t TGCocoa::GetDepth() const
{
   NSArray *screens = [NSScreen screens];
   assert(screens != nil && "screens array is nil");
   
   NSScreen *mainScreen = [screens objectAtIndex : 0];
   assert(mainScreen != nil && "screen with index 0 is nil");

   return NSBitsPerPixelFromDepth([mainScreen depth]);
}

//______________________________________________________________________________
void TGCocoa::Update(Int_t mode)
{
   //Mode == 2 - force widgets to redraw,
   //Mode == 3 - execute graphics requests.
   if (mode == 2) {
      gClient->DoRedraw();//Call DoRedraw for all widgets, who need to be updated.
   } else if (mode > 0) {
      fPimpl->fX11CommandBuffer.Flush(fPimpl.get());
   }
}

//Window management part.

//______________________________________________________________________________
Window_t TGCocoa::GetDefaultRootWindow() const
{
   //Index, fixed and used only by 'root' window.
   return fPimpl->GetRootWindowID();
}

//______________________________________________________________________________
Int_t TGCocoa::InitWindow(ULong_t parentID)
{
   //InitWindow is a strange name, since this function
   //creates a window, but this name is in TVirtualX interface.
   //Actually, there is no special need in this function, 
   //it's a kind of simplified CreateWindow (with only
   //one parameter). This function is called by TRootCanvas,
   //to create a special window inside TGCanvas.
   WindowAttributes_t attr = {};

   if (fPimpl->IsRootWindow(parentID)) {
      ROOT::MacOSX::X11::GetRootWindowAttributes(&attr);   
   } else {
      [fPimpl->GetWindow(parentID) getAttributes : &attr];
   }

   return CreateWindow(parentID, 0, 0, attr.fWidth, attr.fHeight, 0, attr.fDepth, attr.fClass, 0, 0, 0);
}

//______________________________________________________________________________
Window_t TGCocoa::GetWindowID(Int_t wid)
{
   //In case of X11, there is a mixture of 
   //casted X11 ids (Window_t) and index in some internal array (in TGX11), which
   //contains such an id. On Mac I always have indices. Yes, I'm smart.
   return wid;
}

//______________________________________________________________________________
void TGCocoa::SelectWindow(Int_t wid)
{
   //This function can be called from pad/canvas, both for window and for pixmap.
   assert(wid > fPimpl->GetRootWindowID() && "SelectWindow, called for 'root' window");

   fSelectedDrawable = wid;
}

//______________________________________________________________________________
void TGCocoa::ClearWindow()
{
   //Clear the selected drawable (can be window or pixmap, so the name is ambiguous).
   assert(fSelectedDrawable > fPimpl->GetRootWindowID() && "ClearWindow, fSelectedDrawable is invalid");

   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(fSelectedDrawable);
   if (drawable.fIsPixmap) {
      //Pixmaps are white by default.
      CGContextRef pixmapCtx = drawable.fContext;
      const Quartz::CGStateGuard ctxGuard(pixmapCtx);
      CGContextSetRGBFillColor(pixmapCtx, 1., 1., 1., 1.);
      CGContextFillRect(pixmapCtx, CGRectMake(0, 0, drawable.fWidth, drawable.fHeight));
   } else {
      ClearArea(fSelectedDrawable, 0, 0, 0, 0);
   }
}

//______________________________________________________________________________
void TGCocoa::GetGeometry(Int_t wid, Int_t & x, Int_t &y, UInt_t &w, UInt_t &h)
{
   //In TGX11, GetGeometry works with special windows, created by InitWindow
   //(so this function is called from TCanvas/TGCanvas/TRootCanvas).
   //It also translates x and y from parent's coordinates into screen coordinates.
   if (wid < 0 || fPimpl->IsRootWindow(wid)) {
      //Comment in TVirtualX suggests, that wid can be < 0.
      //This will be screen's geometry.
      WindowAttributes_t attr = {};
      ROOT::MacOSX::X11::GetRootWindowAttributes(&attr);
      x = attr.fX;
      y = attr.fY;
      w = attr.fWidth;
      h = attr.fHeight;
   } else {
      NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
      x = drawable.fX;
      y = drawable.fY;
      w = drawable.fWidth;
      h = drawable.fHeight;

      if (!drawable.fIsPixmap) {
         NSObject<X11Window> *window = (NSObject<X11Window> *)drawable;
         NSPoint srcPoint = {};
         srcPoint.x = x;
         srcPoint.y = y;
         NSView<X11Window> *view = window.fContentView.fParentView ? window.fContentView.fParentView : window.fContentView;
         //View parameter for TranslateToScreen call must 
         //be parent view, since x and y are in parent's
         //coordinate system.
         const NSPoint dstPoint = X11::TranslateToScreen(view, srcPoint);
         x = dstPoint.x;
         y = dstPoint.y;
      }
   }
}

//______________________________________________________________________________
void TGCocoa::MoveWindow(Int_t wid, Int_t x, Int_t y)
{
   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "MoveWindow, called for 'root' window");

   [fPimpl->GetWindow(wid) setX : x Y : y];
}

//______________________________________________________________________________
void TGCocoa::RescaleWindow(Int_t /*wid*/, UInt_t /*w*/, UInt_t /*h*/)
{
   //
}

//______________________________________________________________________________
void TGCocoa::ResizeWindow(Int_t wid)
{
   //This function does not resize window (it was done already), 
   //it resizes "back buffer" if any.

   if (!wid)//From TGX11.
      return;
   
   assert(wid != fPimpl->GetRootWindowID() && "ResizeWindow, called for root window");
   
   NSObject<X11Window> *window = fPimpl->GetWindow(wid);
   if (window.fBackBuffer) {
      const int currentDrawable = fSelectedDrawable;
      fSelectedDrawable = wid;
      SetDoubleBufferON();
      fSelectedDrawable = currentDrawable;
   }
}

//______________________________________________________________________________
void TGCocoa::UpdateWindow(Int_t /*mode*/)
{
   //This function is used by TCanvas/TPad:
   //draw "back buffer" image into the view.
   
   //Basic es-guarantee: X11Buffer::AddUpdateWindow modifies vector with commands,
   //if the following call to TGCocoa::Update will produce an exception dusing X11Buffer::Flush,
   //initial state of X11Buffer can not be restored, but it still must be in some valid state.
   
   assert(fSelectedDrawable > fPimpl->GetRootWindowID() && "UpdateWindow, no window was selected, can not update 'root' window");
   
   NSObject<X11Window> *window = fPimpl->GetWindow(fSelectedDrawable);

   if (QuartzPixmap *pixmap = window.fBackBuffer) {
      assert([window.fContentView isKindOfClass : [QuartzView class]] && "UpdateWindow, content view is not a QuartzView");
      
      QuartzView *dstView = (QuartzView *)window.fContentView;
      assert(dstView != nil && "UpdateWindow, destination view is nil");
      
      if (dstView.fIsOverlapped)
         return;
      
      if (dstView.fContext) {
         //We can draw directly.
         const Util::CFScopeGuard<CGImageRef> image([pixmap createImageFromPixmap]);
         if (image.Get()) {
            const CGRect imageRect = CGRectMake(0, 0, pixmap.fWidth, pixmap.fHeight);
            CGContextDrawImage(dstView.fContext, imageRect, image.Get());
         }
      } else {
         //Have to wait.
         fPimpl->fX11CommandBuffer.AddUpdateWindow(dstView);
         Update(1);
      }
   }
}

//______________________________________________________________________________
Window_t TGCocoa::GetCurrentWindow() const
{
   //Window, which was selected by SelectWindow.
   return fSelectedDrawable;
}

//______________________________________________________________________________
void TGCocoa::CloseWindow()
{
   //Deletes selected window.
}

//______________________________________________________________________________
Int_t TGCocoa::AddWindow(ULong_t /*qwid*/, UInt_t /*w*/, UInt_t /*h*/)
{
   //Should register a window created by Qt as a ROOT window,
   //but since Qt-ROOT does not work on Mac and will never work,
   //especially with version 4.8 - this implementation will always
   //be empty.
   return 0;
}

//______________________________________________________________________________
void TGCocoa::RemoveWindow(ULong_t /*qwid*/)
{
   //Remove window, created by Qt.
}

//______________________________________________________________________________
Window_t TGCocoa::CreateWindow(Window_t parentID, Int_t x, Int_t y, UInt_t w, UInt_t h, UInt_t border, Int_t depth,
                               UInt_t clss, void *visual, SetWindowAttributes_t *attr, UInt_t wtype)
{
   //Create new window (top-level == QuartzWindow + QuartzView, or child == QuartzView)
   
   //Strong es-guarantee - exception can be only during registration, class state will remain
   //unchanged, no leaks (scope guards).
   
   const Util::AutoreleasePool pool;
   
   if (fPimpl->IsRootWindow(parentID)) {//parent == root window.
      QuartzWindow *newWindow = X11::CreateTopLevelWindow(x, y, w, h, border, depth, clss, visual, attr, wtype);//Can throw.
      const Util::NSScopeGuard<QuartzWindow> winGuard(newWindow);
      const Window_t result = fPimpl->RegisterDrawable(newWindow);//Can throw.
      newWindow.fID = result;

      [newWindow setAcceptsMouseMovedEvents : YES];

      return result;
   } else {
      NSObject<X11Window> *parentWin = fPimpl->GetWindow(parentID);
      //OpenGL view can not have children.
      assert([parentWin.fContentView isKindOfClass : [QuartzView class]] && "CreateWindow, parent view must be QuartzView");
      QuartzView *childView = X11::CreateChildView((QuartzView *)parentWin.fContentView, x, y, w, h, border, depth, clss, visual, attr, wtype);//Can throw.
      const Util::NSScopeGuard<QuartzView> viewGuard(childView);
      const Window_t result = fPimpl->RegisterDrawable(childView);//Can throw.
      childView.fID = result;
      [parentWin addChild : childView];
      
      return result;
   }
}

//______________________________________________________________________________
void TGCocoa::DestroyWindow(Window_t wid)
{
   //The XDestroyWindow function destroys the specified window as well as all of its subwindows
   //and causes the X server to generate a DestroyNotify event for each window.  The window 
   //should never be referenced again.  If the window specified by the w argument is mapped, 
   //it is unmapped automatically.  The ordering of the
   //DestroyNotify events is such that for any given window being destroyed, DestroyNotify is generated 
   //on any inferiors of the window before being generated on the window itself.  The ordering 
   //among siblings and across subhierarchies is not otherwise constrained.  
   //If the window you specified is a root window, no windows are destroyed. Destroying a mapped window 
   //will generate Expose events on other windows that were obscured by the window being destroyed. 
   
   //I have NO idea why ROOT's GUI calls DestroyWindow with illegal
   //window id, but it does.
   
   //No-throw guarantee???
   
   if (!wid)
      return;

   if (fPimpl->IsRootWindow(wid))
      return;
   
   const Util::AutoreleasePool pool;//TODO: check.

   fPimpl->fX11EventTranslator.CheckUnmappedView(wid);

   assert(fPimpl->GetDrawable(wid).fIsPixmap == NO &&  "DestroyWindow, can not be called for QuartzPixmap or QuartzImage object");
   
   NSObject<X11Window> *window = fPimpl->GetWindow(wid);
   if (window.fBackBuffer) {
      if (fPimpl->fX11CommandBuffer.BufferSize())
         fPimpl->fX11CommandBuffer.RemoveOperationsForDrawable(window.fBackBuffer.fID);
      fPimpl->DeleteDrawable(window.fBackBuffer.fID);
   }
   
   if (fPimpl->fX11CommandBuffer.BufferSize())
      fPimpl->fX11CommandBuffer.RemoveOperationsForDrawable(wid);

   DestroySubwindows(wid);
   if (window.fEventMask & kStructureNotifyMask)
      fPimpl->fX11EventTranslator.GenerateDestroyNotify(wid);

   //Interrupt modal loop (TGClient::WaitFor).
   //TODO: check nested WaitFors.
   if (gClient->GetWaitForEvent() == kDestroyNotify && wid == gClient->GetWaitForWindow())
      gClient->SetWaitForWindow(kNone);

   RemoveEventsForWindow(wid);
   fPimpl->DeleteDrawable(wid);
}

//______________________________________________________________________________
void TGCocoa::DestroySubwindows(Window_t wid)
{
   // The DestroySubwindows function destroys all inferior windows of the
   // specified window, in bottom-to-top stacking order.
   
   //No-throw guarantee??
   
   if (!wid)//From TGX11.
      return;
   
   if (fPimpl->IsRootWindow(wid))
      return;
   
   const Util::AutoreleasePool pool;

   assert(fPimpl->GetDrawable(wid).fIsPixmap == NO && "DestroySubwindows, can not be called for QuartzPixmap or QuartzImage object");

   NSObject<X11Window> *window = fPimpl->GetWindow(wid);
   
   //I can not iterate on subviews array directly, since it'll be modified
   //during this iteration - create a copy (and it'll also increase references,
   //which will be decreased by guard's dtor).
   const Util::NSScopeGuard<NSArray> children([[window.fContentView subviews] copy]);

   for (NSView<X11Window> *child in children.Get())
      DestroyWindow(child.fID);
}

//______________________________________________________________________________
void TGCocoa::GetWindowAttributes(Window_t wid, WindowAttributes_t &attr)
{
   //No-throw guarantee.

   if (!wid)//X11's None?
      return;

   if (fPimpl->IsRootWindow(wid)) {
      //'root' window.
      ROOT::MacOSX::X11::GetRootWindowAttributes(&attr);
   } else {
      [fPimpl->GetWindow(wid) getAttributes : &attr];
   }
}

//______________________________________________________________________________
void TGCocoa::ChangeWindowAttributes(Window_t wid, SetWindowAttributes_t *attr)
{
   //No-throw guarantee.

   if (!wid)//From TGX11
      return;

   assert(!fPimpl->IsRootWindow(wid) && "ChangeWindowAttributes, called for the 'root' window");
   assert(attr != 0 && "ChangeWindowAttributes, attr parameter is null");
   
   [fPimpl->GetWindow(wid) setAttributes : attr];
}

//______________________________________________________________________________
void TGCocoa::SelectInput(Window_t wid, UInt_t evmask)
{
   //No-throw guarantee.

   // Defines which input events the window is interested in. By default
   // events are propageted up the window stack. This mask can also be
   // set at window creation time via the SetWindowAttributes_t::fEventMask
   // attribute.
   
   assert(!fPimpl->IsRootWindow(wid) && "SelectInput, called for 'root' window");
   
   NSObject<X11Window> *window = fPimpl->GetWindow(wid);
   //XSelectInput overrides previous mask.
   window.fEventMask = evmask;
}

//______________________________________________________________________________
void TGCocoa::ReparentChild(Window_t wid, Window_t pid, Int_t x, Int_t y)
{
   //Reparent view.

   assert(!fPimpl->IsRootWindow(wid) && "ReparentChild, can not re-parent 'root' window");
   //TODO: does ROOT cares about reparent X11 events?

   const ROOT::MacOSX::Util::AutoreleasePool pool;//TODO: check?

   NSView<X11Window> *view = fPimpl->GetWindow(wid).fContentView;
   if (fPimpl->IsRootWindow(pid)) {
      //Make a top-level view from a child view.
      [view retain];
      [view removeFromSuperview];
      view.fParentView = nil;
      
      NSRect frame = view.frame;
      frame.origin = CGPointZero;
      const NSUInteger styleMask = NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask;
      QuartzWindow *newTopLevel = [[QuartzWindow alloc] initWithContentRect : frame styleMask : styleMask backing : NSBackingStoreBuffered defer : NO];
      
      [view setX : x Y : y];
      [newTopLevel addChild : view];
      fPimpl->ReplaceDrawable(wid, newTopLevel);

      [view updateLevel : 0];

      [view release];
      [newTopLevel release];
   } else {
      [view retain];
      [view removeFromSuperview];
      //
      NSObject<X11Window> *newParent = fPimpl->GetWindow(pid);
      assert(newParent.fIsPixmap == NO && "ReparentChild, pixmap can not be a new parent");
      [view setX : x Y : y];
      [newParent addChild : view];//It'll also update view's level, no need to call updateLevel.
      [view release];
   }
}

//______________________________________________________________________________
void TGCocoa::ReparentTopLevel(Window_t wid, Window_t pid, Int_t x, Int_t y)
{
   //Reparent top-level window.
   //I have to delete QuartzWindow here and place in its slot view + 
   //reparent this view into pid.
   if (fPimpl->IsRootWindow(pid))//Nothing to do, wid is already a top-level window.
      return;
   
   const Util::AutoreleasePool pool;//TODO: check?
   
   NSView<X11Window> *contentView = fPimpl->GetWindow(wid).fContentView;
   [contentView retain];
   [contentView removeFromSuperview];
   QuartzWindow *topLevel = (QuartzWindow *)[contentView window];
   [topLevel setContentView : nil];
   fPimpl->ReplaceDrawable(wid, contentView);
   [contentView setX : x Y : y];
   [fPimpl->GetWindow(pid) addChild : contentView];//Will also replace view's level.
   [contentView release];
}

//______________________________________________________________________________
void TGCocoa::ReparentWindow(Window_t wid, Window_t pid, Int_t x, Int_t y)
{
   //Change window's parent (possibly creating new top-level window or destroying top-level window).

   if (!wid)//From TGX11.
      return;
   
   assert(!fPimpl->IsRootWindow(wid) && "ReparentWindow, can not re-parent 'root' window");

   NSView<X11Window> *view = fPimpl->GetWindow(wid).fContentView;
   if (view.fParentView) {
      ReparentChild(wid, pid, x, y);
   } else {
      //wid is a top-level window (or content view of such a window).
      ReparentTopLevel(wid, pid, x, y);
   }
}

//______________________________________________________________________________
void TGCocoa::MapWindow(Window_t wid)
{
   // Maps the window "wid" and all of its subwindows that have had map
   // requests. This function has no effect if the window is already mapped.
   
   assert(!fPimpl->IsRootWindow(wid) && "MapWindow, called for 'root' window");
   
   if (MakeProcessForeground())
      [fPimpl->GetWindow(wid) mapWindow];
}

//______________________________________________________________________________
void TGCocoa::MapSubwindows(Window_t wid)
{
   // Maps all subwindows for the specified window "wid" in top-to-bottom
   // stacking order.   
   
   assert(!fPimpl->IsRootWindow(wid) && "MapSubwindows, called for 'root' window");
   
   if (MakeProcessForeground())
      [fPimpl->GetWindow(wid) mapSubwindows];
}

//______________________________________________________________________________
void TGCocoa::MapRaised(Window_t wid)
{
   // Maps the window "wid" and all of its subwindows that have had map
   // requests on the screen and put this window on the top of of the
   // stack of all windows.
   
   assert(!fPimpl->IsRootWindow(wid) && "MapRaised, called for 'root' window");
   //ROOT::MacOSX::Util::AutoreleasePool pool;//TODO

   if (MakeProcessForeground())
      [fPimpl->GetWindow(wid) mapRaised];
}

//______________________________________________________________________________
void TGCocoa::UnmapWindow(Window_t wid)
{
   // Unmaps the specified window "wid". If the specified window is already
   // unmapped, this function has no effect. Any child window will no longer
   // be visible (but they are still mapped) until another map call is made
   // on the parent.
   assert(!fPimpl->IsRootWindow(wid) && "UnmapWindow, called for 'root' window");
   
   const Util::AutoreleasePool pool;//TODO
   
   //If this window is a grab window or a parent of a grab window.
   fPimpl->fX11EventTranslator.CheckUnmappedView(wid);
   [fPimpl->GetWindow(wid) unmapWindow];

   //if (window.fEventMask & kStructureNotifyMask)
   //   fPimpl->fX11EventTranslator.GenerateUnmapNotify(wid);//??? TODO
   
   //Interrupt modal loop (TGClient::WaitForUnmap).
   if (gClient->GetWaitForEvent() == kUnmapNotify && gClient->GetWaitForWindow() == wid)
      gClient->SetWaitForWindow(kNone);
   
   //RemoveEventsForWindow(wid);//????
}

//______________________________________________________________________________
void TGCocoa::RaiseWindow(Window_t wid)
{
   // Raises the specified window to the top of the stack so that no
   // sibling window obscures it.
   
   if (!wid)//From TGX11.
      return;
   
   assert(!fPimpl->IsRootWindow(wid) && "RaiseWindow, called for 'root' window");
   
   if (!fPimpl->GetWindow(wid).fParentView)
      return;
      
   [fPimpl->GetWindow(wid) raiseWindow];
}

//______________________________________________________________________________
void TGCocoa::LowerWindow(Window_t wid)
{
   // Lowers the specified window "wid" to the bottom of the stack so
   // that it does not obscure any sibling windows.
   
   if (!wid)//From TGX11.
      return;
   
   assert(!fPimpl->IsRootWindow(wid) && "LowerWindow, called for 'root' window");
   
   if (!fPimpl->GetWindow(wid).fParentView)
      return;
      
   [fPimpl->GetWindow(wid) lowerWindow];
}

//______________________________________________________________________________
void TGCocoa::MoveWindow(Window_t wid, Int_t x, Int_t y)
{
   // Moves the specified window to the specified x and y coordinates.
   // It does not change the window's size, raise the window, or change
   // the mapping state of the window.
   //
   // x, y - coordinates, which define the new position of the window
   //        relative to its parent.
   
   if (!wid)//From TGX11.
      return;
      
   assert(!fPimpl->IsRootWindow(wid) && "MoveWindow, called for 'root' window");
   
   [fPimpl->GetWindow(wid) setX : x Y : y];
}

//______________________________________________________________________________
void TGCocoa::MoveResizeWindow(Window_t wid, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   // Changes the size and location of the specified window "wid" without
   // raising it.
   //
   // x, y - coordinates, which define the new position of the window
   //        relative to its parent.
   // w, h - the width and height, which define the interior size of
   //        the window
   
   if (!wid)//From TGX11.
      return;
   
   assert(!fPimpl->IsRootWindow(wid) && "MoveResizeWindow, called for 'root' window");
   
   [fPimpl->GetWindow(wid) setX : x Y : y width : w height : h];
}

//______________________________________________________________________________
void TGCocoa::ResizeWindow(Window_t wid, UInt_t w, UInt_t h)
{
   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "ResizeWindow, called for 'root' window");
   
   NSSize newSize = {};
   newSize.width = w;
   newSize.height = h;

   [fPimpl->GetWindow(wid) setDrawableSize : newSize];
}

//______________________________________________________________________________
void TGCocoa::IconifyWindow(Window_t wid)
{
   // Iconifies the window "wid".
   if (!wid)
      return;
}

//______________________________________________________________________________
void TGCocoa::TranslateCoordinates(Window_t srcWin, Window_t dstWin, Int_t srcX, Int_t srcY, Int_t &dstX, Int_t &dstY, Window_t &child)
{
   // Translates coordinates in one window to the coordinate space of another
   // window. It takes the "src_x" and "src_y" coordinates relative to the
   // source window's origin and returns these coordinates to "dest_x" and
   // "dest_y" relative to the destination window's origin.

   // child          - returns the child of "dest" if the coordinates
   //                  are contained in a mapped child of the destination
   //                  window; otherwise, child is set to 0
   child = 0;   
   if (!srcWin || !dstWin)//This is from TGX11, looks like this can happen.
      return;
   
   const bool srcIsRoot = fPimpl->IsRootWindow(srcWin);
   const bool dstIsRoot = fPimpl->IsRootWindow(dstWin);
   
   if (srcIsRoot && dstIsRoot) {
      //This can happen with ROOT's GUI. Set dstX/Y equal to srcX/Y.
      //From man for XTranslateCoordinates it's not clear, what should be in child.
      dstX = srcX;
      dstY = srcY;
      child = 0;//TODO: check, if X11 searches for any window.
      return;
   }
   
   NSPoint srcPoint = {};
   srcPoint.x = srcX;
   srcPoint.y = srcY;

   NSPoint dstPoint = {};


   if (dstIsRoot) {
      NSView<X11Window> *srcView = fPimpl->GetWindow(srcWin).fContentView;
      dstPoint = X11::TranslateToScreen(srcView, srcPoint);
   } else if (srcIsRoot) {
      NSView<X11Window> *dstView = fPimpl->GetWindow(dstWin).fContentView;
      dstPoint = X11::TranslateFromScreen(srcPoint, dstView);

      if ([dstView superview]) {
         //hitTest requires a point in a superview's coordinate system.
         //Even contentView of QuartzWindow has a superview (NSThemeFrame),
         //so this should always work.
         dstPoint = [[dstView superview] convertPoint : dstPoint fromView : dstView];
         if (NSView<X11Window> *view = (NSView<X11Window> *)[dstView hitTest : dstPoint]) {
            if (view != dstView && view.fMapState == kIsViewable)
               child = view.fID;
         }
      }
   } else {
      NSView<X11Window> *srcView = fPimpl->GetWindow(srcWin).fContentView;
      NSView<X11Window> *dstView = fPimpl->GetWindow(dstWin).fContentView;

      dstPoint = X11::TranslateCoordinates(srcView, dstView, srcPoint);
      if ([dstView superview]) {
         //hitTest requires a point in a view's superview coordinate system.
         //Even contentView of QuartzWindow has a superview (NSThemeFrame),
         //so this should always work.
         dstPoint = [[dstView superview] convertPoint : dstPoint fromView : dstView];
         if (NSView<X11Window> *view = (NSView<X11Window> *)[dstView hitTest : dstPoint]) {
            if (view != dstView && view.fMapState == kIsViewable)
               child = view.fID;
         }
      }
   }

   dstX = dstPoint.x;
   dstY = dstPoint.y;
}

//______________________________________________________________________________
void TGCocoa::GetWindowSize(Drawable_t wid, Int_t &x, Int_t &y, UInt_t &w, UInt_t &h)
{
   // Returns the location and the size of window "wid"
   //
   // x, y - coordinates of the upper-left outer corner relative to the
   //        parent window's origin
   // w, h - the size of the window, not including the border.
   
   //GUI classes can use rootID and 0?
   if (!wid)//From GX11Gui.cxx.
      return;
   
   if (fPimpl->IsRootWindow(wid)) {
      WindowAttributes_t attr = {};
      ROOT::MacOSX::X11::GetRootWindowAttributes(&attr);
      x = attr.fX;
      y = attr.fY;
      w = attr.fWidth;
      h = attr.fHeight;
   } else {
      NSObject<X11Drawable> *window = fPimpl->GetDrawable(wid);
      //ROOT can ask window size for ... non-window drawable.
      if (!window.fIsPixmap) {
         x = window.fX;
         y = window.fY;
      } else {
         x = 0;
         y = 0;
      }

      w = window.fWidth;
      h = window.fHeight;
   }
}

//______________________________________________________________________________
void TGCocoa::SetWindowBackground(Window_t wid, ULong_t color)
{
   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "SetWindowBackground, can not set color for 'root' window");

   fPimpl->GetWindow(wid).fBackgroundPixel = color;
}

//______________________________________________________________________________
void TGCocoa::SetWindowBackgroundPixmap(Window_t wid, Pixmap_t /*pxm*/)
{
   // Sets the background pixmap of the window "wid" to the specified
   // pixmap "pxm".
   if (!wid)//From TGX11.
      return;
}

//______________________________________________________________________________
Window_t TGCocoa::GetParent(Window_t wid) const
{
   // Returns the parent of the window "wid".
   if (wid <= (Window_t)fPimpl->GetRootWindowID())
      return wid;
   
   NSView<X11Window> *view = fPimpl->GetWindow(wid).fContentView;
   return view.fParentView ? view.fParentView.fID : fPimpl->GetRootWindowID();
}

//______________________________________________________________________________
void TGCocoa::SetWindowName(Window_t wid, char *name)
{
   if (!wid || !name)//From TGX11.
      return;
   
   const Util::AutoreleasePool pool;
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   
   if ([(NSObject *)drawable isKindOfClass : [NSWindow class]]) {
      NSString *windowTitle = [NSString stringWithCString : name encoding : NSASCIIStringEncoding];
      [(NSWindow *)drawable setTitle : windowTitle];
   } 

   //else: before I had a warning message here, but looks like 
   //ROOT's GUI can call this method on non-toplevel window. 
   //Just ignore this case.
}

//______________________________________________________________________________
void TGCocoa::SetIconName(Window_t /*wid*/, char * /*name*/)
{
   // Sets the window icon name.
}

//______________________________________________________________________________
void TGCocoa::SetIconPixmap(Window_t /*wid*/, Pixmap_t /*pix*/)
{
   // Sets the icon name pixmap.
}

//______________________________________________________________________________
void TGCocoa::SetClassHints(Window_t /*wid*/, char * /*className*/,
                              char * /*resourceName*/)
{
   // Sets the windows class and resource name.
}

/////////////////////////////////////////
//GUI-rendering part.
//______________________________________________________________________________
void TGCocoa::DrawLineAux(Drawable_t wid, const GCValues_t &gcVals, Int_t x1, Int_t y1, Int_t x2, Int_t y2)
{
   //Can be called directly of when flushing command buffer.
   assert(!fPimpl->IsRootWindow(wid) && "DrawLineAux, called for 'root' window");
   
   CGContextRef ctx = fPimpl->GetDrawable(wid).fContext;
   assert(ctx != 0 && "DrawLineAux, ctx is null");

   const Quartz::CGStateGuard ctxGuard(ctx);//Will restore state back.
   //Draw a line.
   //This draw line is a special GUI method, it's used not by ROOT's graphics, but
   //widgets. The problem is:
   //-I have to switch off anti-aliasing, since if anti-aliasing is on,
   //the line is thick and has different color.
   //-As soon as I switch-off anti-aliasing, and line is precise, I can not
   //draw a line [0, 0, -> w, 0].
   //I use a small translation, after all, this is ONLY gui method and it
   //will not affect anything except GUI.
   CGContextTranslateCTM(ctx, 0.f, 1.);
   CGContextSetAllowsAntialiasing(ctx, false);//Smoothed line is of wrong color and in a wrong position - this is bad for GUI.

   SetStrokeParametersFromX11Context(ctx, gcVals);
   CGContextBeginPath(ctx);
   CGContextMoveToPoint(ctx, x1, y1);
   CGContextAddLineToPoint(ctx, x2, y2);
   CGContextStrokePath(ctx);
   
   CGContextSetAllowsAntialiasing(ctx, true);//Somehow, it's not saved/restored, this affects ... window's titlebar.
}

//______________________________________________________________________________
void TGCocoa::DrawLine(Drawable_t wid, GContext_t gc, Int_t x1, Int_t y1, Int_t x2, Int_t y2)
{
   //This function can be called:
   //a)'normal' way - from view's drawRect method.
   //b) for 'direct rendering' - operation was initiated by ROOT's GUI, not by 
   //   drawRect.

   if (!wid) //From TGX11.
      return;
   
   assert(!fPimpl->IsRootWindow(wid) && "DrawLine, called for 'root' window");   
   assert(gc > 0 && gc <= fX11Contexts.size() && "DrawLine, strange context index");

   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   if (!drawable.fIsPixmap) {
      NSObject<X11Window> *window = (NSObject<X11Window> *)drawable;
      QuartzView *view = (QuartzView *)window.fContentView;
      
      if (ParentRendersToChild(view)) {
         if (X11::LockFocus(view)) {
            DrawLineAux(view.fID, gcVals, x1, y1, x2, y2);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddDrawLine(wid, gcVals, x1, y1, x2, y2);
         else
            DrawLineAux(wid, gcVals, x1, y1, x2, y2);
      }
   } else {
      if (!IsCocoaDraw()) {
         fPimpl->fX11CommandBuffer.AddDrawLine(wid, gcVals, x1, y1, x2, y2);
      } else {
         DrawLineAux(wid, gcVals, x1, y1, x2, y2);
      }
   }
}

//______________________________________________________________________________
void TGCocoa::DrawSegmentsAux(Drawable_t wid, const GCValues_t &gcVals, const Segment_t *segments, Int_t nSegments)
{
   assert(!fPimpl->IsRootWindow(wid) && "DrawSegmentsAux, called for 'root' window");
   assert(segments != 0 && "DrawSegmentsAux, segments parameter is null");
   assert(nSegments > 0 && "DrawSegmentsAux, nSegments <= 0");
   
   for (Int_t i = 0; i < nSegments; ++i)
      DrawLineAux(wid, gcVals, segments[i].fX1, segments[i].fY1 - 3, segments[i].fX2, segments[i].fY2 - 3);
}

//______________________________________________________________________________
void TGCocoa::DrawSegments(Drawable_t wid, GContext_t gc, Segment_t *segments, Int_t nSegments)
{
   //Draw multiple line segments. Each line is specified by a pair of points.
   if (!wid)//From TGX11.
      return;
      
   assert(!fPimpl->IsRootWindow(wid) && "DrawSegments, called for 'root' window");
   assert(gc > 0 && gc <= fX11Contexts.size() && "DrawSegments, bad GContext_t");
   assert(segments != 0 && "DrawSegments, segments parameter is null");
   assert(nSegments > 0 && "DrawSegments, number of segments <= 0");

   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   
   if (!drawable.fIsPixmap) {
      QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;
      
      if (ParentRendersToChild(view)) {
         if (X11::LockFocus(view)) {
            DrawSegmentsAux(view.fID, gcVals, segments, nSegments);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddDrawSegments(wid, gcVals, segments, nSegments);
         else
            DrawSegmentsAux(wid, gcVals, segments, nSegments);
      }
   } else {
      if (!IsCocoaDraw())
         fPimpl->fX11CommandBuffer.AddDrawSegments(wid, gcVals, segments, nSegments);
      else
         DrawSegmentsAux(wid, gcVals, segments, nSegments);
   }
}

//______________________________________________________________________________
void TGCocoa::DrawRectangleAux(Drawable_t wid, const GCValues_t &gcVals, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   //Can be called directly or during flushing command buffer.
   assert(!fPimpl->IsRootWindow(wid) && "DrawRectangleAux, called for 'root' window");

   //I can not draw a line at y == 0, shift the rectangle to 1 pixel (and reduce its height).
   if (!y) {
      y = 1;
      if (h)
         h -= 1;
   }

   CGContextRef ctx = fPimpl->GetDrawable(wid).fContext;
   assert(ctx && "DrawRectangleAux, ctx is null");
   const Quartz::CGStateGuard ctxGuard(ctx);//Will restore context state.

   CGContextSetAllowsAntialiasing(ctx, false);
   //Line color from X11 context.
   SetStrokeParametersFromX11Context(ctx, gcVals);
      
   const CGRect rect = CGRectMake(x, y, w, h);
   CGContextStrokeRect(ctx, rect);

   CGContextSetAllowsAntialiasing(ctx, true);
}

//______________________________________________________________________________
void TGCocoa::DrawRectangle(Drawable_t wid, GContext_t gc, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   //Can be called in a 'normal way' - from drawRect method (QuartzView)
   //or directly by ROOT.
   
   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "DrawRectangle, called for 'root' window");
   assert(gc > 0 && gc <= fX11Contexts.size() && "DrawRectangle, bad GContext_t");

   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);

   if (!drawable.fIsPixmap) {
      NSObject<X11Window> *window = (NSObject<X11Window> *)drawable;
      QuartzView *view = (QuartzView *)window.fContentView;
      
      if (ParentRendersToChild(view)) {
         if (X11::LockFocus(view)) {
            DrawRectangleAux(view.fID, gcVals, x, y, w, h);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddDrawRectangle(wid, gcVals, x, y, w, h);
         else
            DrawRectangleAux(wid, gcVals, x, y, w, h);
      }
   } else {
      if (!IsCocoaDraw())
         fPimpl->fX11CommandBuffer.AddDrawRectangle(wid, gcVals, x, y, w, h);
      else
         DrawRectangleAux(wid, gcVals, x, y, w, h);
   }
}

//______________________________________________________________________________
void TGCocoa::FillRectangleAux(Drawable_t wid, const GCValues_t &gcVals, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   //Can be called directly or when flushing command buffer.
   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "FillRectangleAux, called for 'root' window");
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   CGContextRef ctx = drawable.fContext;
   CGSize patternPhase = {};

   const CGRect fillRect = CGRectMake(x, y, w, h);

   if (!drawable.fIsPixmap) {
      QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;
      const CGPoint origin = [view convertPoint : view.frame.origin toView : nil];
      patternPhase.width = origin.x;
      patternPhase.height = origin.y;
   }

   const Quartz::CGStateGuard ctxGuard(ctx);//Will restore context state.

   if (gcVals.fMask & kGCStipple) {
      assert(fPimpl->GetDrawable(gcVals.fStipple).fIsPixmap == YES && "FillRectangleAux, stipple is not a pixmap");
      PatternContext patternContext = {gcVals.fMask, gcVals.fForeground, gcVals.fBackground, 
                                       (QuartzImage *)fPimpl->GetDrawable(gcVals.fStipple), 
                                       patternPhase};
      SetFillPattern(ctx, &patternContext);
      CGContextFillRect(ctx, fillRect);
      return;
   }

   SetFilledAreaColorFromX11Context(ctx, gcVals);
   CGContextFillRect(ctx, fillRect);
}

//______________________________________________________________________________
void TGCocoa::FillRectangle(Drawable_t wid, GContext_t gc, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   //Can be called in a 'normal way' - from drawRect method (QuartzView)
   //or directly by ROOT.

   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "FillRectangle, called for 'root' window");
   assert(gc > 0 && gc <= fX11Contexts.size() && "FillRectangle, bad GContext_t");

   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   
   if (!drawable.fIsPixmap) {
      NSObject<X11Window> *window = (NSObject<X11Window> *)drawable;
      QuartzView *view = (QuartzView *)window.fContentView;
      
      if (ParentRendersToChild(view)) {
         if (X11::LockFocus(view)) {
            FillRectangleAux(view.fID, gcVals, x, y, w, h);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddFillRectangle(wid, gcVals, x, y, w, h);
         else
            FillRectangleAux(wid, gcVals, x, y, w, h);
      }
   } else {
      if (!IsCocoaDraw())
         fPimpl->fX11CommandBuffer.AddFillRectangle(wid, gcVals, x, y, w, h);
      else
         FillRectangleAux(wid, gcVals, x, y, w, h);
   }   
}

//______________________________________________________________________________
void TGCocoa::FillPolygonAux(Window_t wid, const GCValues_t &gcVals, const Point_t *polygon, Int_t nPoints) 
{
   //Can be called directly or when flushing command buffer.
   if (!wid)//From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "FillPolygonAux, called for 'root' window");
   assert(polygon != 0 && "FillPolygonAux, polygon parameter is null");
   assert(nPoints > 0 && "FillPolygonAux, nPoints <= 0");
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   CGContextRef ctx = drawable.fContext;

   CGSize patternPhase = {};

   if (!drawable.fIsPixmap) {
      QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;
      const CGPoint origin = [view convertPoint : view.frame.origin toView : nil];
      patternPhase.width = origin.x;
      patternPhase.height = origin.y;
   }

   const Quartz::CGStateGuard ctxGuard(ctx);//Will restore context state.
   
   CGContextSetAllowsAntialiasing(ctx, false);

   if (gcVals.fMask & kGCStipple) {
      assert(fPimpl->GetDrawable(gcVals.fStipple).fIsPixmap == YES && "FillRectangleAux, stipple is not a pixmap");
      PatternContext patternContext = {gcVals.fMask, gcVals.fForeground, gcVals.fBackground, 
                                       (QuartzImage *)fPimpl->GetDrawable(gcVals.fStipple), 
                                       patternPhase};
      SetFillPattern(ctx, &patternContext);
   } else
      SetFilledAreaColorFromX11Context(ctx, gcVals);
      
   CGContextBeginPath(ctx);
   CGContextMoveToPoint(ctx, polygon[0].fX, polygon[0].fY - 2);
   for (Int_t i = 1; i < nPoints; ++i)
      CGContextAddLineToPoint(ctx, polygon[i].fX, polygon[i].fY - 2);
   CGContextFillPath(ctx);
   
   CGContextSetAllowsAntialiasing(ctx, true);
}

//______________________________________________________________________________
void TGCocoa::FillPolygon(Window_t wid, GContext_t gc, Point_t *polygon, Int_t nPoints) 
{
   // Fills the region closed by the specified path. The path is closed
   // automatically if the last point in the list does not coincide with the
   // first point.
   //
   // Point_t *points - specifies an array of points
   // Int_t npnt      - specifies the number of points in the array
   //
   // GC components in use: function, plane-mask, fill-style, fill-rule,
   // subwindow-mode, clip-x-origin, clip-y-origin, and clip-mask.  GC
   // mode-dependent components: foreground, background, tile, stipple,
   // tile-stipple-x-origin, and tile-stipple-y-origin.
   // (see also the GCValues_t structure)
   
   if (!wid)//from TGX11.
      return;
   
   assert(polygon != 0 && "FillPolygon, 'points' array is null");
   assert(nPoints > 0 && "FillPolygon, bad number of points");
   assert(gc > 0 && gc <= fX11Contexts.size() && "FillPolygon, bad CGContext_t");
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   
   if (!drawable.fIsPixmap) {
      QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;
      
      if (ParentRendersToChild(view)) {
         if (X11::LockFocus(view)) {
            FillPolygonAux(view.fID, gcVals, polygon, nPoints);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddFillPolygon(wid, gcVals, polygon, nPoints);
         else
            FillPolygonAux(wid, gcVals, polygon, nPoints);
      }
   } else {
      if (!IsCocoaDraw())
         fPimpl->fX11CommandBuffer.AddFillPolygon(wid, gcVals, polygon, nPoints);
      else
         FillPolygonAux(wid, gcVals, polygon, nPoints);
   }
}

//______________________________________________________________________________
void TGCocoa::CopyAreaAux(Drawable_t src, Drawable_t dst, const GCValues_t &gcVals, Int_t srcX, Int_t srcY, UInt_t width, UInt_t height, Int_t dstX, Int_t dstY)
{
   //Called directly or when flushing command buffer.
   if (!src || !dst)//Can this happen? From TGX11.
      return;
   
   assert(!fPimpl->IsRootWindow(src) && "CopyAreaAux, src parameter is 'root' window");
   assert(!fPimpl->IsRootWindow(dst) && "CopyAreaAux, dst parameter is 'root' window");
   
   //Some copy operations create autoreleased cocoa objects,
   //I do not want them to wait till run loop's iteration end to die.
   const Util::AutoreleasePool pool;
   
   NSObject<X11Drawable> *srcDrawable = fPimpl->GetDrawable(src);
   NSObject<X11Drawable> *dstDrawable = fPimpl->GetDrawable(dst);

   Point_t dstPoint = {};
   dstPoint.fX = dstX;
   dstPoint.fY = dstY;

   Rectangle_t copyArea = {};
   copyArea.fX = srcX;
   copyArea.fY = srcY; 
   copyArea.fWidth = (UShort_t)width;//TODO: check size?
   copyArea.fHeight = (UShort_t)height;//TODO: check size?

   QuartzImage *mask = nil;
   if ((gcVals.fMask & kGCClipMask) && gcVals.fClipMask) {
      assert(fPimpl->GetDrawable(gcVals.fClipMask).fIsPixmap == YES && "CopyArea, mask is not a pixmap");
      mask = (QuartzImage *)fPimpl->GetDrawable(gcVals.fClipMask);
   }
   
   Point_t clipOrigin = {};
   if (gcVals.fMask & kGCClipXOrigin)
      clipOrigin.fX = gcVals.fClipXOrigin;
   if (gcVals.fMask & kGCClipYOrigin)
      clipOrigin.fY = gcVals.fClipYOrigin;

   [dstDrawable copy : srcDrawable area : copyArea withMask : mask clipOrigin : clipOrigin toPoint : dstPoint];
}

//______________________________________________________________________________
void TGCocoa::CopyArea(Drawable_t src, Drawable_t dst, GContext_t gc, Int_t srcX, Int_t srcY, UInt_t width, UInt_t height, Int_t dstX, Int_t dstY)
{
   if (!src || !dst)//Can this happen? From TGX11.
      return;
      
   assert(!fPimpl->IsRootWindow(src) && "CopyArea, src parameter is 'root' window");
   assert(!fPimpl->IsRootWindow(dst) && "CopyArea, dst parameter is 'root' window");
   assert(gc > 0 && gc <= fX11Contexts.size() && "CopyArea, bad GContext_t");

   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(dst);
   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   
   if (!drawable.fIsPixmap) {
      QuartzView *view = (QuartzView *)fPimpl->GetWindow(dst).fContentView;
      
      if (ParentRendersToChild(view)) {
         if (X11::LockFocus(view)) {
            CopyAreaAux(src, dst, gcVals, srcX, srcY, width, height, dstX, dstY);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddCopyArea(src, dst, gcVals, srcX, srcY, width, height, dstX, dstY);
         else
            CopyAreaAux(src, dst, gcVals, srcX, srcY, width, height, dstX, dstY);
      }
   } else {
      if (!IsCocoaDraw())
         fPimpl->fX11CommandBuffer.AddCopyArea(src, dst, gcVals, srcX, srcY, width, height, dstX, dstY);
      else
         CopyAreaAux(src, dst, gcVals, srcX, srcY, width, height, dstX, dstY);
   }  
}

//______________________________________________________________________________
void TGCocoa::DrawStringAux(Drawable_t wid, const GCValues_t &gcVals, Int_t x, Int_t y, const char *text, Int_t len)
{
   //Can be called by ROOT directly, or indirectly by AppKit.  
   assert(!fPimpl->IsRootWindow(wid) && "DrawStringAux, called for the 'root' window");

   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   CGContextRef ctx = drawable.fContext;
   assert(ctx != 0 && "DrawStringAux, ctx is null");

   const Quartz::CGStateGuard ctxGuard(ctx);//Will reset parameters back.

   CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
   
   //View is flipped, I have to transform for text to work.
   CGContextTranslateCTM(ctx, 0., drawable.fHeight);
   CGContextScaleCTM(ctx, 1., -1.);   

   //Text must be antialiased.
   CGContextSetAllowsAntialiasing(ctx, true);
      
   assert(gcVals.fMask & kGCFont && "DrawString, font is not set in a context");

   if (len < 0)//Negative length can come from caller.
      len = std::strlen(text);
   const std::string substr(text, len);
   //Text can be not black, for example, highlighted label.
   CGFloat textColor[4] = {0., 0., 0., 1.};//black by default.
   //I do not check the results here, it's ok to have a black text.
   if (gcVals.fMask & kGCForeground)
      X11::PixelToRGB(gcVals.fForeground, textColor);

   CGContextSetRGBFillColor(ctx, textColor[0], textColor[1], textColor[2], textColor[3]);

   //Do a simple text layout using CGGlyphs.
   std::vector<UniChar> unichars(text, text + len);
   Quartz::DrawTextLineNoKerning(ctx, (CTFontRef)gcVals.fFont, unichars, x,  X11::LocalYROOTToCocoa(drawable, y));
}

//______________________________________________________________________________
void TGCocoa::DrawString(Drawable_t wid, GContext_t gc, Int_t x, Int_t y, const char *text, Int_t len)
{
   //Can be called by ROOT directly, or indirectly by AppKit.
   if (!wid)//from TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "DrawString, called for the 'root' window");
   assert(gc > 0 && gc <= fX11Contexts.size() && "DrawString, bad GContext_t");

   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   const GCValues_t &gcVals = fX11Contexts[gc - 1];
   assert(gcVals.fMask & kGCFont && "DrawString, font is not set in a context");

   if (!drawable.fIsPixmap) {   
      QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;

      if (ParentRendersToChild(view)) {//Ufff.
         if (X11::LockFocus(view)) {
            DrawStringAux(view.fID, gcVals, x, y, text, len);
            X11::UnlockFocus(view);
            return;
         }
      }
      
      if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
         if (!view.fContext)
            fPimpl->fX11CommandBuffer.AddDrawString(wid, gcVals, x, y, text, len);
         else
            DrawStringAux(wid, gcVals, x, y, text, len);
      }

   } else {
      if (!IsCocoaDraw())
         fPimpl->fX11CommandBuffer.AddDrawString(wid, gcVals, x, y, text, len);
      else 
         DrawStringAux(wid, gcVals, x, y, text, len);
   }
}

//______________________________________________________________________________
void TGCocoa::ClearAreaAux(Window_t wid, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   assert(!fPimpl->IsRootWindow(wid) && "ClearAreaAux, called for the 'root' window");
   
   QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;
   assert(view.fContext != 0 && "ClearAreaAux, view.fContext is null");

   //w and h can be 0 (comment from TGX11) - clear the entire window.
   if (!w)
      w = view.fWidth;
   if (!h)
      h = view.fHeight;
   
   CGFloat rgb[3] = {};
   X11::PixelToRGB(view.fBackgroundPixel, rgb);

   const Quartz::CGStateGuard ctxGuard(view.fContext);
   CGContextSetRGBFillColor(view.fContext, rgb[0], rgb[1], rgb[2], 1.);//alpha can be also used.
   CGContextFillRect(view.fContext, CGRectMake(x, y, w, h));
}

//______________________________________________________________________________
void TGCocoa::ClearArea(Window_t wid, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   //Can be called from drawRect method and also by ROOT's GUI directly.
   //Should not be called for pixmap?
   
   if (!wid) //From TGX11.
      return;

   assert(!fPimpl->IsRootWindow(wid) && "ClearArea, called for the 'root' window");
   
   QuartzView *view = (QuartzView *)fPimpl->GetWindow(wid).fContentView;//If wid is pixmap or image, this will crush.

   if (ParentRendersToChild(view)) {
      if (X11::LockFocus(view)) {
         ClearAreaAux(view.fID, x, y, w, h);
         X11::UnlockFocus(view);
         return;
      }
   }
   
   if (!view.fIsOverlapped && view.fMapState == kIsViewable) {
      if (!view.fContext)
         fPimpl->fX11CommandBuffer.AddClearArea(wid, x, y, w, h);
      else
         ClearAreaAux(wid, x, y, w, h);
   }
}

//______________________________________________________________________________
void TGCocoa::ClearWindow(Window_t wid)
{
   // Clears the entire area in the specified window. Comment from TGX11.

   if (!wid)//From TGX11.
      return;

   ClearArea(wid, 0, 0, 0, 0);
}

//Pixmap management.

//______________________________________________________________________________
Int_t TGCocoa::OpenPixmap(UInt_t w, UInt_t h)
{
   //Two stage creation.
   NSSize newSize = {};
   newSize.width = w;
   newSize.height = h;

   Util::NSScopeGuard<QuartzPixmap> obj([QuartzPixmap alloc]);
   if (QuartzPixmap *pixmap = [obj.Get() initWithW : w H : h]) {
      obj.Reset(pixmap);
      pixmap.fID = fPimpl->RegisterDrawable(pixmap);//Can throw.
      return (Int_t)pixmap.fID;
   } else {
      Error("OpenPixmap", "Pixmap initialization failed");
      return -1;
   }
}

//______________________________________________________________________________
Int_t TGCocoa::ResizePixmap(Int_t wid, UInt_t w, UInt_t h)
{
   assert(!fPimpl->IsRootWindow(wid) && "ResizePixmap, called for 'root' window");

   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   assert(drawable.fIsPixmap == YES && "ResizePixmap, object is not a pixmap");

   QuartzPixmap *pixmap = (QuartzPixmap *)drawable;
   if (w == pixmap.fWidth && h == pixmap.fHeight)
      return 1;
   
   if ([pixmap resizeW : w H : h])
      return 1;

   return -1;
}

//______________________________________________________________________________
void TGCocoa::SelectPixmap(Int_t pixid)
{
   assert(pixid > fPimpl->GetRootWindowID() && "SelectPixmap, 'root' window can not be selected");

   fSelectedDrawable = pixid;
}

//______________________________________________________________________________
void TGCocoa::CopyPixmap(Int_t wid, Int_t xpos, Int_t ypos)
{
   //const ROOT::MacOSX::Util::AutoreleasePool pool;
   
   NSObject<X11Drawable> *source = fPimpl->GetDrawable(wid);
   assert(source.fIsPixmap == YES && "CopyPixmap, source is not a pixmap");
   
   QuartzPixmap *pixmap = (QuartzPixmap *)source;
   
   NSObject<X11Window> *window = fPimpl->GetWindow(fSelectedDrawable);
   if (window.fBackBuffer) {
      const Util::CFScopeGuard<CGImageRef> image([pixmap createImageFromPixmap]);
      if (image.Get()) {
         CGContextRef dstCtx = window.fBackBuffer.fContext;
         assert(dstCtx != 0 && "CopyPixmap, destination context is null");

         const CGRect imageRect = CGRectMake(xpos, ypos, pixmap.fWidth, pixmap.fHeight);

         CGContextDrawImage(dstCtx, imageRect, image.Get());
         CGContextFlush(dstCtx);
      }
   } else {
      Warning("CopyPixmap", "Operation skipped, since destination window is not double buffered");
   }
}

//______________________________________________________________________________
void TGCocoa::ClosePixmap()
{

   // Deletes current pixmap.
}

//Different functions to create pixmap from different data sources. Used by GUI.
//These functions implement TVirtualX interface, some of them repeat each other.

//______________________________________________________________________________
Pixmap_t TGCocoa::CreatePixmap(Drawable_t /*wid*/, UInt_t w, UInt_t h)
{
   //
   return OpenPixmap(w, h);
}

//______________________________________________________________________________
Pixmap_t TGCocoa::CreatePixmap(Drawable_t /*wid*/, const char *bitmap, UInt_t width, UInt_t height,
                               ULong_t foregroundPixel, ULong_t backgroundPixel, Int_t depth)
{
   //Create QuartzImage, using bitmap and foregroundPixel/backgroundPixel,
   //if depth is one - create an image mask instead.

   assert(bitmap != 0 && "CreatePixmap, bitmap parameter is null");
   assert(width > 0 && "CreatePixmap, width parameter is 0");
   assert(height > 0 && "CreatePixmap, height parameter is 0");
   
   unsigned char *imageData = 0;      
   if (depth > 1)
      imageData = new unsigned char[width * height * 4]();
   else
      imageData = new unsigned char[width * height];

   X11::FillPixmapBuffer((unsigned char*)bitmap, width, height, foregroundPixel, backgroundPixel, depth, imageData);

   //Now we can create CGImageRef.
   Util::NSScopeGuard<QuartzImage> mem([QuartzImage alloc]);
   if (!mem.Get()) {
      Error("CreatePixmap", "[QuartzImage alloc] failed");
      delete [] imageData;
      return Pixmap_t();
   }

   QuartzImage *image = nil;
   
   if (depth > 1)
      image = [mem.Get() initWithW : width H : height data: imageData];
   else
      image = [mem.Get() initMaskWithW : width H : height bitmapMask : imageData];

   if (!image) {
      delete [] imageData;
      Error("CreatePixmap", "[QuartzImage initWithW:H:data:] failed");
      return Pixmap_t();
   }

   mem.Reset(image);
   //Now imageData is owned by image.
   image.fID = fPimpl->RegisterDrawable(image);//This can throw.

   return image.fID;      
}

//______________________________________________________________________________
Pixmap_t TGCocoa::CreatePixmapFromData(unsigned char *bits, UInt_t width, UInt_t height)
{
   //Create QuartzImage, using "bits" (data in bgra format).   
   assert(bits != 0 && "CreatePixmapFromData, data parameter is null");
   assert(width != 0 && "CreatePixmapFromData, width parameter is 0");
   assert(height != 0 && "CreatePixmapFromData, height parameter is 0");

   //I'm not using vector here, since I have to pass this pointer to Obj-C code
   //(and Obj-C object will own this memory later).
   unsigned char *imageData = new unsigned char[width * height * 4];
   std::copy(bits, bits + width * height * 4, imageData);

   //Convert bgra to rgba.
   unsigned char *p = imageData;
   for (unsigned i = 0, e = width * height; i < e; ++i, p += 4)
      std::swap(p[0], p[2]);

   //Now we can create CGImageRef.
   Util::NSScopeGuard<QuartzImage> mem([QuartzImage alloc]);
   if (!mem.Get()) {
      Error("CreatePixmapFromData", "[QuartzImage alloc] failed");
      delete [] imageData;
      return Pixmap_t();
   }

   QuartzImage *image = [mem.Get() initWithW : width H : height data : imageData];
   if (!image) {
      delete [] imageData;
      Error("CreatePixmapFromData", "[QuartzImage initWithW:H:data:] failed");
      return Pixmap_t();
   }
   
   mem.Reset(image);
   //Now imageData is owned by image.
   image.fID = fPimpl->RegisterDrawable(image);//This can throw.
   
   return image.fID;      
}

//______________________________________________________________________________
Pixmap_t TGCocoa::CreateBitmap(Drawable_t /*wid*/, const char *bitmap, UInt_t width, UInt_t height)
{
   //Create QuartzImage with image mask.
   assert(std::numeric_limits<unsigned char>::digits == 8 && "CreateBitmap, ASImage requires octets");

   //I'm not using vector here, since I have to pass this pointer to Obj-C code
   //(and Obj-C object will own this memory later).
   
   //TASImage has a bug, it calculates size in pixels (making a with to multiple-of eight and 
   //allocates memory as each bit occupies one byte, and later packs bits into bytes.
   //Posylaiu luchi ponosa avtoru.

   unsigned char *imageData = new unsigned char[width * height]();
   for (unsigned i = 0, j = 0, e = width / 8 * height; i < e; ++i) {//TASImage supposes 8-bit bytes and packs mask bits.
      for(unsigned bit = 0; bit < 8; ++bit, ++j) {
         if (bitmap[i] & (1 << bit))
            imageData[j] = 0;//Opaque.
         else
            imageData[j] = 255;//Masked out bit.
      }
   }

   //Now we can create CGImageRef.
   Util::NSScopeGuard<QuartzImage> mem([QuartzImage alloc]);
   if (!mem.Get()) {
      Error("CreateBitmap", "[QuartzImage alloc] failed");
      delete [] imageData;
      return Pixmap_t();
   }

   QuartzImage *image = [mem.Get() initMaskWithW : width H : height bitmapMask: imageData];
   if (!image) {//Error is already reported by QuartzImage.
      delete [] imageData;
      return Pixmap_t();
   }
   
   mem.Reset(image);
   //Now, imageData is owned by image.
   image.fID = fPimpl->RegisterDrawable(image);//This can throw.      
   return image.fID;      
}

//______________________________________________________________________________
void TGCocoa::DeletePixmapAux(Pixmap_t pixmapID)
{
   fPimpl->DeleteDrawable(pixmapID);
}

//______________________________________________________________________________
void TGCocoa::DeletePixmap(Pixmap_t pixmapID)
{
   // Explicitely deletes the pixmap resource "pmap".
   assert(fPimpl->GetDrawable(pixmapID).fIsPixmap == YES && "DeletePixmap, object is not a pixmap");   
   fPimpl->fX11CommandBuffer.AddDeletePixmap(pixmapID);
}

//______________________________________________________________________________
Int_t TGCocoa::AddPixmap(ULong_t /*pixind*/, UInt_t /*w*/, UInt_t /*h*/)
{
   // Registers a pixmap created by TGLManager as a ROOT pixmap
   //
   // w, h - the width and height, which define the pixmap size
   return 0;
}

//______________________________________________________________________________
unsigned char *TGCocoa::GetColorBits(Drawable_t wid, Int_t x, Int_t y, UInt_t w, UInt_t h)
{
   //Can be also in a window management part, since window is also drawable.
   if (fPimpl->IsRootWindow(wid)) {
      Warning("GetColorBits", "Called for root window");
   } else {
      assert(x >= 0 && "GetColorBits, x parameter is negative");
      assert(y >= 0 && "GetColorBits, y parameter is negative");
      assert(w != 0 && "GetColorBits, w parameter is 0");
      assert(h != 0 && "GetColorBits, h parameter is 0");

      Rectangle_t area = {};
      area.fX = x, area.fY = y, area.fWidth = w, area.fHeight = h;
      return [fPimpl->GetDrawable(wid) readColorBits : area];
   }

   return 0;
}

//Mouse related code.

//______________________________________________________________________________
void TGCocoa::GrabButton(Window_t wid, EMouseButton button, UInt_t keyModifiers, UInt_t eventMask,
                         Window_t /*confine*/, Cursor_t /*cursor*/, Bool_t grab)
{
   //Emulate "passive grab" feature of X11 (similar to "implicit grab" in Cocoa
   //and implicit grab on X11, the difference is that "implicit grab" works as
   //if owner_events parameter for XGrabButton was False, but in ROOT
   //owner_events for XGrabButton is _always_ True.
   //Confine will never be used - no such feature on MacOSX and
   //I'm not going to emulate it..
   //This function also does ungrab.
   
   assert(!fPimpl->IsRootWindow(wid) && "GrabButton, called for 'root' window");
   
   NSObject<X11Window> *widget = fPimpl->GetWindow(wid);
   
   if (grab) {
      widget.fOwnerEvents = YES;   //This is how TGX11 works.
      widget.fGrabButton = button;
      widget.fGrabButtonEventMask = eventMask;
      widget.fGrabKeyModifiers = keyModifiers;
      //Set the cursor.
   } else {
      widget.fOwnerEvents = NO;
      widget.fGrabButton = -1;//0 is kAnyButton.
      widget.fGrabButtonEventMask = 0;
      widget.fGrabKeyModifiers = 0;
   }
}

//______________________________________________________________________________
void TGCocoa::GrabPointer(Window_t wid, UInt_t eventMask, Window_t /*confine*/, Cursor_t /*cursor*/, Bool_t grab, Bool_t ownerEvents)
{
   //Emulate pointer grab from X11.
   //Confine will never be used - no such feature on MacOSX and
   //I'm not going to emulate it..
   //This function also does ungrab.

   if (grab) {
      NSView<X11Window> *view = fPimpl->GetWindow(wid).fContentView;
      assert(!fPimpl->IsRootWindow(wid) && "GrabPointer, called for 'root' window");
      //set the cursor.
      //set active grab.
      fPimpl->fX11EventTranslator.SetPointerGrab(view, eventMask, ownerEvents);
   } else {
      //unset cursor?
      //cancel grab.
      fPimpl->fX11EventTranslator.CancelPointerGrab();
   }
}

//Font management.

//______________________________________________________________________________
FontStruct_t TGCocoa::LoadQueryFont(const char *fontName)
{
   //fontName is in XLFD format:
   //-foundry-family- ..... etc., some components can be omitted and replaced by *.
   assert(fontName != 0 && "LoadQueryFont, fontName is null");

   X11::XLFDName xlfd;
   if (ParseXLFDName(fontName, xlfd)) {
      //Make names more flexible: fFamilyName can be empty or '*'.
      if (!xlfd.fFamilyName.length() || xlfd.fFamilyName == "*")
         xlfd.fFamilyName = "Courier";//Up to me, right?
      if (!xlfd.fPixelSize)
         xlfd.fPixelSize = 11;//Again, up to me.
      return fPimpl->fFontManager.LoadFont(xlfd);
   }

   return FontStruct_t();
}

//______________________________________________________________________________
FontH_t TGCocoa::GetFontHandle(FontStruct_t fs)
{
   return (FontH_t)fs;
}

//______________________________________________________________________________
void TGCocoa::DeleteFont(FontStruct_t fs)
{
   fPimpl->fFontManager.UnloadFont(fs);
}

//______________________________________________________________________________
Bool_t TGCocoa::HasTTFonts() const
{
   // Returns True when TrueType fonts are used
   return kFALSE;
}

//______________________________________________________________________________
Int_t TGCocoa::TextWidth(FontStruct_t font, const char *s, Int_t len)
{
   // Return lenght of the string "s" in pixels. Size depends on font.
   return fPimpl->fFontManager.GetTextWidth(font, s, len);
}

//______________________________________________________________________________
void TGCocoa::GetFontProperties(FontStruct_t font, Int_t &maxAscent, Int_t &maxDescent)
{
   // Returns the font properties.
   fPimpl->fFontManager.GetFontProperties(font, maxAscent, maxDescent);
}

//______________________________________________________________________________
FontStruct_t TGCocoa::GetFontStruct(FontH_t fh)
{
   // Retrieves the associated font structure of the font specified font
   // handle "fh".
   //
   // Free returned FontStruct_t using FreeFontStruct().

   return (FontStruct_t)fh;
}

//______________________________________________________________________________
void TGCocoa::FreeFontStruct(FontStruct_t /*fs*/)
{
   // Frees the font structure "fs". The font itself will be freed when
   // no other resource references it.

   //
   // fFontManager->UnloadFont(fs);
}

//______________________________________________________________________________
char **TGCocoa::ListFonts(const char *fontName, Int_t maxNames, Int_t &count)
{
   count = 0;

   if (fontName && fontName[0]) {
      X11::XLFDName xlfd;
      if (X11::ParseXLFDName(fontName, xlfd))
         return fPimpl->fFontManager.ListFonts(xlfd, maxNames, count);
   }

   return 0;
}

//______________________________________________________________________________
void TGCocoa::FreeFontNames(char **fontList)
{
   // Frees the specified the array of strings "fontlist".
   if (!fontList)
      return;
      
   fPimpl->fFontManager.FreeFontNames(fontList);
}

//Color management.

//______________________________________________________________________________
Bool_t TGCocoa::ParseColor(Colormap_t /*cmap*/, const char *colorName, ColorStruct_t &color)
{
   //"Color" passed as colorName, can be one of the names, defined in X11/rgb.txt,
   //or rgb triplet, which looks like: #rgb #rrggbb #rrrgggbbb #rrrrggggbbbb,
   //where r, g, and b - are hex digits.
   return fPimpl->fX11ColorParser.ParseColor(colorName, color);
}

//______________________________________________________________________________
Bool_t TGCocoa::AllocColor(Colormap_t /*cmap*/, ColorStruct_t &color)
{
   const unsigned red = unsigned(double(color.fRed) / 0xFFFF * 0xFF);
   const unsigned green = unsigned(double(color.fGreen) / 0xFFFF * 0xFF);
   const unsigned blue = unsigned(double(color.fBlue) / 0xFFFF * 0xFF);
   color.fPixel = red << 16 | green << 8 | blue;
   return kTRUE;
}

//______________________________________________________________________________
void TGCocoa::QueryColor(Colormap_t /*cmap*/, ColorStruct_t & color)
{
   // Returns the current RGB value for the pixel in the "color" structure
   color.fRed = (color.fPixel >> 16 & 0xFF) * 0xFFFF / 0xFF;
   color.fGreen = (color.fPixel >> 8 & 0xFF) * 0xFFFF / 0xFF;
   color.fBlue = (color.fPixel & 0xFF) * 0xFFFF / 0xFF;
}

//______________________________________________________________________________
void TGCocoa::FreeColor(Colormap_t /*cmap*/, ULong_t /*pixel*/)
{
   // Frees color cell with specified pixel value.
}

//______________________________________________________________________________
ULong_t TGCocoa::GetPixel(Color_t rootColorIndex)
{
   ULong_t pixel = 0;
   if (const TColor *color = gROOT->GetColor(rootColorIndex)) {
      Float_t red = 0.f, green = 0.f, blue = 0.f;
      color->GetRGB(red, green, blue);
      pixel = unsigned(red * 255) << 16;
      pixel |= unsigned(green * 255) << 8;
      pixel |= unsigned(blue * 255);
   }

   return pixel;
}

//______________________________________________________________________________
void TGCocoa::GetPlanes(Int_t & /*nplanes*/)
{
   // Returns the maximum number of planes.
}

//______________________________________________________________________________
void TGCocoa::GetRGB(Int_t /*index*/, Float_t &/*r*/, Float_t &/*g*/, Float_t &/*b*/)
{
   // Returns RGB values for color "index".
}

//______________________________________________________________________________
void TGCocoa::SetRGB(Int_t /*cindex*/, Float_t /*r*/, Float_t /*g*/, Float_t /*b*/)
{
   // Sets color intensities the specified color index "cindex".
   //
   // cindex  - color index
   // r, g, b - the red, green, blue intensities between 0.0 and 1.0
}

//______________________________________________________________________________
Colormap_t TGCocoa::GetColormap() const
{
   return Colormap_t();
}

//"Context management".

//______________________________________________________________________________
GContext_t TGCocoa::CreateGC(Drawable_t /*wid*/, GCValues_t *gval)
{
   //Here I have to imitate graphics context that exists in X11.
   fX11Contexts.push_back(*gval);
   return fX11Contexts.size();
}

//______________________________________________________________________________
void TGCocoa::SetForeground(GContext_t gc, ULong_t foreground)
{
   // Sets the foreground color for the specified GC (shortcut for ChangeGC
   // with only foreground mask set).
   //
   // gc         - specifies the GC
   // foreground - the foreground you want to set
   // (see also the GCValues_t structure)
   
   assert(gc <= fX11Contexts.size() && gc > 0 && "ChangeGC - stange context id");
   
   GCValues_t &x11Context = fX11Contexts[gc - 1];
   x11Context.fMask |= kGCForeground;
   x11Context.fForeground = foreground;
}

//______________________________________________________________________________
void TGCocoa::ChangeGC(GContext_t gc, GCValues_t *gval)
{
   //
   assert(gc <= fX11Contexts.size() && gc > 0 && "ChangeGC - stange context id");
   assert(gval != 0 && "ChangeGC, gval parameter is null");
   
   GCValues_t &x11Context = fX11Contexts[gc - 1];
   const Mask_t &mask = gval->fMask;
   x11Context.fMask |= mask;
   
   //Not all of GCValues_t members are used, but
   //all can be copied/set without any problem.
   
   if (mask & kGCFunction)
      x11Context.fFunction = gval->fFunction;
   if (mask & kGCPlaneMask)
      x11Context.fPlaneMask = gval->fPlaneMask;
   if (mask & kGCForeground)
      x11Context.fForeground = gval->fForeground;
   if (mask & kGCBackground)
      x11Context.fBackground = gval->fBackground;
   if (mask & kGCLineWidth)
      x11Context.fLineWidth = gval->fLineWidth;
   if (mask & kGCLineStyle)
      x11Context.fLineStyle = gval->fLineStyle;
   if (mask & kGCCapStyle)//nobody uses
      x11Context.fCapStyle = gval->fCapStyle;
   if (mask & kGCJoinStyle)//nobody uses
      x11Context.fJoinStyle = gval->fJoinStyle;
   //
   
   if (mask & kGCFillRule)//nobody uses
      x11Context.fFillRule = gval->fFillRule;
   if (mask & kGCArcMode)//nobody uses
      x11Context.fArcMode = gval->fArcMode;

   if (mask & kGCFillStyle) {
      x11Context.fFillStyle = gval->fFillStyle;
      x11Context.fMask &= ~kGCStipple;
      x11Context.fMask &= ~kGCTile;
   }
   if (mask & kGCTile) {
      x11Context.fTile = gval->fTile;
      x11Context.fMask &= ~kGCStipple;
      x11Context.fMask &= ~kGCFillStyle;
   }
   if (mask & kGCStipple) {
      x11Context.fStipple = gval->fStipple;
      x11Context.fMask &= ~kGCFillStyle;
      x11Context.fMask &= ~kGCTile;
   }
   
   //
   if (mask & kGCTileStipXOrigin)
      x11Context.fTsXOrigin = gval->fTsXOrigin;
   if (mask & kGCTileStipYOrigin)
      x11Context.fTsYOrigin = gval->fTsYOrigin;
   if (mask & kGCFont)
      x11Context.fFont = gval->fFont;
   if (mask & kGCSubwindowMode)
      x11Context.fSubwindowMode = gval->fSubwindowMode;
   if (mask & kGCGraphicsExposures)
      x11Context.fGraphicsExposures = gval->fGraphicsExposures;
   if (mask & kGCClipXOrigin)
      x11Context.fClipXOrigin = gval->fClipXOrigin;
   if (mask & kGCClipYOrigin)
      x11Context.fClipYOrigin = gval->fClipYOrigin;
   if (mask & kGCClipMask)
      x11Context.fClipMask = gval->fClipMask;
   if (mask & kGCDashOffset)
      x11Context.fDashOffset = gval->fDashOffset;
   if (mask & kGCDashList) {
      const unsigned nDashes = sizeof x11Context.fDashes / sizeof x11Context.fDashes[0];
      for (unsigned i = 0; i < nDashes; ++i)
         x11Context.fDashes[i] = gval->fDashes[i];
      x11Context.fDashLen = gval->fDashLen;
   }
}

//______________________________________________________________________________
void TGCocoa::CopyGC(GContext_t src, GContext_t dst, Mask_t mask)
{
   assert(src <= fX11Contexts.size() && src > 0 && "CopyGC, bad source context");   
   assert(dst <= fX11Contexts.size() && dst > 0 && "CopyGC, bad destination context");
   
   GCValues_t srcContext = fX11Contexts[src - 1];
   srcContext.fMask = mask;
   
   ChangeGC(dst, &srcContext);
}

//______________________________________________________________________________
void TGCocoa::GetGCValues(GContext_t gc, GCValues_t &gval)
{
   // Returns the components specified by the mask in "gval" for the
   // specified GC "gc" (see also the GCValues_t structure)
   const GCValues_t &gcVal = fX11Contexts[gc - 1];
   gval = gcVal;
}

//______________________________________________________________________________
void TGCocoa::DeleteGC(GContext_t /*gc*/)
{
   // Deletes the specified GC "gc".
}

//Cursor management.

//______________________________________________________________________________
Cursor_t TGCocoa::CreateCursor(ECursor cursor)
{
   // Creates the specified cursor. (just return cursor from cursor pool).
   // The cursor can be:
   //
   // kBottomLeft, kBottomRight, kTopLeft,  kTopRight,
   // kBottomSide, kLeftSide,    kTopSide,  kRightSide,
   // kMove,       kCross,       kArrowHor, kArrowVer,
   // kHand,       kRotate,      kPointer,  kArrowRight,
   // kCaret,      kWatch

   return Cursor_t(cursor + 1);//HAHAHAHAHA!!! CREATED!!!
}

//______________________________________________________________________________
void TGCocoa::SetCursor(Int_t wid, ECursor cursor)
{
   // The cursor "cursor" will be used when the pointer is in the
   // window "wid".
   assert(!fPimpl->IsRootWindow(wid) && "SetCursor, called for 'root' window");
   
   NSView<X11Window> *view = fPimpl->GetWindow(wid).fContentView;
   view.fCurrentCursor = cursor;
}

//______________________________________________________________________________
void TGCocoa::SetCursor(Window_t wid, Cursor_t cursorID)
{
   // Sets the cursor "curid" to be used when the pointer is in the
   // window "wid".
   if (cursorID > 0)
      SetCursor(Int_t(wid), ECursor(cursorID - 1));
   else
      SetCursor(Int_t(wid), kPointer);
}

//______________________________________________________________________________
void TGCocoa::NextEvent(Event_t &/*event*/)
{
}

//______________________________________________________________________________
void TGCocoa::GetPasteBuffer(Window_t /*id*/, Atom_t /*atom*/, TString &/*text*/, Int_t &/*nchar*/, Bool_t /*del*/)
{
   // Gets contents of the paste buffer "atom" into the string "text".
   // (nchar = number of characters) If "del" is true deletes the paste
   // buffer afterwards.
}

//______________________________________________________________________________
Window_t TGCocoa::CreateOpenGLWindow(Window_t parentID, UInt_t width, UInt_t height, const std::vector<std::pair<UInt_t, Int_t> > &formatComponents)
{
   //ROOT never creates GL widgets with 'root' as a parent (so not top-level gl-windows).
   //If this change, assert must be deleted.
   typedef std::pair<UInt_t, Int_t> component_type;
   typedef std::vector<component_type>::size_type size_type;

   assert(!fPimpl->IsRootWindow(parentID) && "CreateOpenGLWindow, could not create top-level gl window");
   //Convert pairs into Cocoa's GL attributes.
   

   std::vector<NSOpenGLPixelFormatAttribute> attribs;
   for (size_type i = 0, e = formatComponents.size(); i < e; ++i) {
      const component_type &comp = formatComponents[i];
      
      if (comp.first == TGLFormat::kDoubleBuffer) {
         attribs.push_back(NSOpenGLPFADoubleBuffer);
      } else if (comp.first == TGLFormat::kDepth) {
         attribs.push_back(NSOpenGLPFADepthSize);
         attribs.push_back(comp.second > 0 ? comp.second : 32);
      } else if (comp.first == TGLFormat::kAccum) {
         attribs.push_back(NSOpenGLPFAAccumSize);
         attribs.push_back(comp.second > 0 ? comp.second : 1);
      } else if (comp.first == TGLFormat::kStencil) {
         attribs.push_back(NSOpenGLPFAStencilSize);
         attribs.push_back(comp.second > 0 ? comp.second : 8);
      } else if (comp.first == TGLFormat::kMultiSample) {
         attribs.push_back(NSOpenGLPFASampleBuffers);
         attribs.push_back(1);
         attribs.push_back(NSOpenGLPFASamples);
         attribs.push_back(comp.second ? comp.second : 4);
      }
   }
   
   attribs.push_back(NSOpenGLPFAAccelerated);//??? I think, TGLWidget always wants this.
   attribs.push_back(0);

   NSOpenGLPixelFormat *pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes : &attribs[0]];
   const Util::NSScopeGuard<NSOpenGLPixelFormat> formatGuard(pixelFormat);
   
   NSView<X11Window> *parentView = fPimpl->GetWindow(parentID).fContentView;
   assert([parentView isKindOfClass : [QuartzView class]] && "CreateOpenGLWindow, parent view must be QuartzView");
   
   NSRect viewFrame = {};
   viewFrame.size.width = width;
   viewFrame.size.height = height;

   ROOTOpenGLView *glView = [[ROOTOpenGLView alloc] initWithFrame : viewFrame pixelFormat : pixelFormat];
   const Util::NSScopeGuard<ROOTOpenGLView> viewGuard(glView);
   
   [parentView addChild : glView];
   const Window_t glID = fPimpl->RegisterDrawable(glView);
   glView.fID = glID;

   return glID;
}

//______________________________________________________________________________
Handle_t TGCocoa::CreateOpenGLContext(Window_t windowID, Handle_t sharedID)
{
   assert(!fPimpl->IsRootWindow(windowID) && "CreateOpenGLContext, windowID is a 'root' window");
   assert([fPimpl->GetWindow(windowID).fContentView isKindOfClass : [ROOTOpenGLView class]] && 
          "CreateOpenGLContext, view is not an OpenGL view");

   NSOpenGLContext *sharedContext = fPimpl->GetGLContextForHandle(sharedID);
   ROOTOpenGLView *glView = (ROOTOpenGLView *)fPimpl->GetWindow(windowID);

   const Util::NSScopeGuard<NSOpenGLContext> newContext([[NSOpenGLContext alloc] initWithFormat : glView.pixelFormat shareContext : sharedContext]);
   [glView setOpenGLContext : newContext.Get()];
  
   const Handle_t ctxID = fPimpl->RegisterGLContext(newContext.Get());

   return ctxID;
}

//______________________________________________________________________________
void TGCocoa::CreateOpenGLContext(Int_t /*wid*/)
{
   // Creates OpenGL context for window "wid"
}

//______________________________________________________________________________
Bool_t TGCocoa::MakeOpenGLContextCurrent(Handle_t ctxID, Window_t windowID)
{
   assert(ctxID > 0 && "MakeOpenGLContextCurrent, invalid context id");

   NSOpenGLContext *glContext = fPimpl->GetGLContextForHandle(ctxID);
   if (!glContext) {
      Error("MakeOpenGLContextCurrent", "No OpenGL context found for id %d", int(ctxID));

      return kFALSE;
   }

   assert([fPimpl->GetWindow(windowID) isKindOfClass : [ROOTOpenGLView class]] && "MakeOpenGLContextCurrent, view is not an OpenGL view");
   ROOTOpenGLView *glView = (ROOTOpenGLView *)fPimpl->GetWindow(windowID);

   if (OpenGL::GLViewIsValidDrawable(glView)) {
      if ([glContext view] != glView)
         [glContext setView : glView];

      if (glView.fUpdateContext) {
         [glContext update];
         glView.fUpdateContext = NO;
      }

      [glView setOpenGLContext : glContext];
      [glContext makeCurrentContext];
      
      return kTRUE;
   } else {
      //Oh, here's the real black magic.
      //Our brilliant GL code is sure that MakeCurrent always succeeds.
      //But it does not: if view is not visible, context can not be attached,
      //gl operations will fail.
      //Funny enough, but if you have invisible window with visible view,
      //this trick works.
      
      //TODO: this code is a total mess, refactor.

      NSView *fakeView = nil;      
      QuartzWindow *fakeWindow = fPimpl->GetFakeGLWindow();

      if (!fakeWindow) {
         //We did not find any window. Create a new one.
         SetWindowAttributes_t attr = {};
         const UInt_t width = std::max(glView.frame.size.width, CGFloat(100));//100 - is just a stupid hardcoded value.
         const UInt_t height = std::max(glView.frame.size.height, CGFloat(100));

         NSRect viewFrame = {};
         viewFrame.size.width = width;
         viewFrame.size.height = height;

         const NSUInteger styleMask = NSTitledWindowMask | NSClosableWindowMask | NSMiniaturizableWindowMask | NSResizableWindowMask;

         //NOTE: defer parameter is 'NO', otherwise this trick will not help.
         fakeWindow = [[QuartzWindow alloc] initWithContentRect : viewFrame styleMask : styleMask backing : NSBackingStoreBuffered defer : NO windowAttributes : &attr];
         Util::NSScopeGuard<QuartzWindow> winGuard(fakeWindow);

         fakeView = fakeWindow.fContentView;
         [fakeView setHidden : NO];//!

         fPimpl->SetFakeGLWindow(fakeWindow);//Can throw.
         winGuard.Release();
      } else {
         fakeView = fakeWindow.fContentView;
         [fakeView setHidden : NO];
      }

      [glContext setView : fakeView];
      [glContext makeCurrentContext];
   }

   return kTRUE;
}

//______________________________________________________________________________
Handle_t TGCocoa::GetCurrentOpenGLContext()
{
   NSOpenGLContext *currentContext = [NSOpenGLContext currentContext];
   if (!currentContext) {
      Error("GetCurrentOpenGLContext", "The current OpenGL context is null");
      return Handle_t();
   }
   
   const Handle_t contextID = fPimpl->GetHandleForGLContext(currentContext);
   if (!contextID)
      Error("GetCurrentOpenGLContext", "The current OpenGL context was not created/registered by TGCocoa");
   
   return contextID;
}

//______________________________________________________________________________
void TGCocoa::FlushOpenGLBuffer(Handle_t ctxID)
{
   assert(ctxID > 0 && "FlushOpenGLBuffer, invalid context id");
   
   NSOpenGLContext *glContext = fPimpl->GetGLContextForHandle(ctxID);
   assert(glContext != nil && "FlushOpenGLBuffer, bad context id");

   if (glContext != [NSOpenGLContext currentContext])//???
      return;

   glFlush();//???
   [glContext flushBuffer];
}

//______________________________________________________________________________
void TGCocoa::DeleteOpenGLContext(Int_t ctxID)
{
   //Historically, DeleteOpenGLContext was accepting window id,
   //now it's a context id. DeleteOpenGLContext is not used in ROOT,
   //only in TGLContext for Cocoa.
   NSOpenGLContext *glContext = fPimpl->GetGLContextForHandle(ctxID);
   
   if (NSView *v = [glContext view]) {
      if ([v isKindOfClass : [ROOTOpenGLView class]])
         [(ROOTOpenGLView *)v setOpenGLContext : nil];
   
      [glContext clearDrawable];
   }
   
   if (glContext == [NSOpenGLContext currentContext])
      [NSOpenGLContext clearCurrentContext];

   fPimpl->DeleteGLContext(ctxID);

}

//______________________________________________________________________________
UInt_t TGCocoa::ExecCommand(TGWin32Command * /*code*/)
{
   // Executes the command "code" coming from the other threads (Win32)
   return 0;
}

//______________________________________________________________________________
Int_t TGCocoa::GetDoubleBuffer(Int_t /*wid*/)
{
   // Queries the double buffer value for the window "wid".
   return 0;
}

//______________________________________________________________________________
void TGCocoa::GetCharacterUp(Float_t &chupx, Float_t &chupy)
{
   // Returns character up vector.
   chupx = chupy = 0;
}

//______________________________________________________________________________
Handle_t  TGCocoa::GetNativeEvent() const
{
   // Returns the current native event handle.
   return 0;
}

//______________________________________________________________________________
void TGCocoa::QueryPointer(Int_t & /*ix*/, Int_t &/*iy*/)
{
   // Returns the pointer position.
}

//______________________________________________________________________________
Pixmap_t TGCocoa::ReadGIF(Int_t /*x0*/, Int_t /*y0*/, const char * /*file*/, Window_t /*id*/)
{
   // If id is NULL - loads the specified gif file at position [x0,y0] in the
   // current window. Otherwise creates pixmap from gif file

   return 0;
}

//______________________________________________________________________________
Int_t TGCocoa::RequestLocator(Int_t /*mode*/, Int_t /*ctyp*/, Int_t &/*x*/, Int_t &/*y*/)
{
   // Requests Locator position.
   // x,y  - cursor position at moment of button press (output)
   // ctyp - cursor type (input)
   //        ctyp = 1 tracking cross
   //        ctyp = 2 cross-hair
   //        ctyp = 3 rubber circle
   //        ctyp = 4 rubber band
   //        ctyp = 5 rubber rectangle
   //
   // mode - input mode
   //        mode = 0 request
   //        mode = 1 sample
   //
   // The returned value is:
   //        in request mode:
   //                       1 = left is pressed
   //                       2 = middle is pressed
   //                       3 = right is pressed
   //        in sample mode:
   //                       11 = left is released
   //                       12 = middle is released
   //                       13 = right is released
   //                       -1 = nothing is pressed or released
   //                       -2 = leave the window
   //                     else = keycode (keyboard is pressed)

   return 0;
}

//______________________________________________________________________________
Int_t TGCocoa::RequestString(Int_t /*x*/, Int_t /*y*/, char * /*text*/)
{
   // Requests string: text is displayed and can be edited with Emacs-like
   // keybinding. Returns termination code (0 for ESC, 1 for RETURN)
   //
   // x,y  - position where text is displayed
   // text - displayed text (as input), edited text (as output)
   return 0;
}

//______________________________________________________________________________
void TGCocoa::SetCharacterUp(Float_t /*chupx*/, Float_t /*chupy*/)
{
   // Sets character up vector.
}

//______________________________________________________________________________
void TGCocoa::SetClipOFF(Int_t /*wid*/)
{
   // Turns off the clipping for the window "wid".
}

//______________________________________________________________________________
void TGCocoa::SetClipRegion(Int_t /*wid*/, Int_t /*x*/, Int_t /*y*/, UInt_t /*w*/, UInt_t /*h*/)
{
   // Sets clipping region for the window "wid".
   //
   // wid  - window indentifier
   // x, y - origin of clipping rectangle
   // w, h - the clipping rectangle dimensions

}

//______________________________________________________________________________
void TGCocoa::SetDoubleBuffer(Int_t wid, Int_t mode)
{
   //In ROOT, canvas has a "double buffer" - pixmap attached to 'wid'.
   assert(wid > fPimpl->GetRootWindowID() && "SetDoubleBuffer called for 'root' window");
   
   if (wid == 999) {//Comment in TVirtaulX suggests, that 999 means all windows.
      Warning("SetDoubleBuffer", "called with wid == 999");
   } else {
      fSelectedDrawable = wid;
      mode ? SetDoubleBufferON() : SetDoubleBufferOFF();
   }   
}

//______________________________________________________________________________
void TGCocoa::SetDoubleBufferOFF()
{
   fDirectDraw = true;
}

//______________________________________________________________________________
void TGCocoa::SetDoubleBufferON()
{
   //Attach pixmap to the selected window (view).
   fDirectDraw = false;
   
   assert(fSelectedDrawable > fPimpl->GetRootWindowID() && "SetDoubleBufferON, called, but no correct window was selected before");
   
   NSObject<X11Window> *window = fPimpl->GetWindow(fSelectedDrawable);
   
   assert(window.fIsPixmap == NO && "SetDoubleBufferON, selected drawable is a pixmap, can not attach pixmap to pixmap");
   
   const unsigned currW = window.fWidth;
   const unsigned currH = window.fHeight;
   
   if (QuartzPixmap *currentPixmap = window.fBackBuffer) {
      if (currH == currentPixmap.fHeight && currW == currentPixmap.fWidth)
         return;
   }

   Util::NSScopeGuard<QuartzPixmap> mem([QuartzPixmap alloc]);      
   if (QuartzPixmap *pixmap = [mem.Get() initWithW : currW H : currH]) {
      mem.Reset(pixmap);
      pixmap.fID = fPimpl->RegisterDrawable(pixmap);//Can throw.
      if (window.fBackBuffer) {//Now we can delete the old one, since the new was created.
         if (fPimpl->fX11CommandBuffer.BufferSize())
            fPimpl->fX11CommandBuffer.RemoveOperationsForDrawable(window.fBackBuffer.fID);
         fPimpl->DeleteDrawable(window.fBackBuffer.fID);
      }

      window.fBackBuffer = pixmap;
   } else {
      Error("SetDoubleBufferON", "Can't create a pixmap");
   }
}

//______________________________________________________________________________
void TGCocoa::SetDrawMode(EDrawMode mode)
{
   // Sets the drawing mode.
   //
   //EDrawMode{kCopy, kXor};
   fDrawMode = mode;
}

//______________________________________________________________________________
void TGCocoa::SetTextMagnitude(Float_t /*mgn*/)
{
   // Sets the current text magnification factor to "mgn"
}

//______________________________________________________________________________
void TGCocoa::Sync(Int_t /*mode*/)
{
   // Set synchronisation on or off.
   // mode : synchronisation on/off
   //    mode=1  on
   //    mode<>0 off
}

//______________________________________________________________________________
void TGCocoa::Warp(Int_t /*ix*/, Int_t /*iy*/, Window_t /*id*/)
{
   // Sets the pointer position.
   // ix - new X coordinate of pointer
   // iy - new Y coordinate of pointer
   // Coordinates are relative to the origin of the window id
   // or to the origin of the current window if id == 0.
}

//______________________________________________________________________________
Int_t TGCocoa::WriteGIF(char * /*name*/)
{
   // Writes the current window into GIF file.
   // Returns 1 in case of success, 0 otherwise.

   return 0;
}

//______________________________________________________________________________
void TGCocoa::WritePixmap(Int_t /*wid*/, UInt_t /*w*/, UInt_t /*h*/, char * /*pxname*/)
{
   // Writes the pixmap "wid" in the bitmap file "pxname".
   //
   // wid    - the pixmap address
   // w, h   - the width and height of the pixmap.
   // pxname - the file name
}

//______________________________________________________________________________
Bool_t TGCocoa::NeedRedraw(ULong_t /*tgwindow*/, Bool_t /*force*/)
{
   // Notify the low level GUI layer ROOT requires "tgwindow" to be
   // updated
   //
   // Returns kTRUE if the notification was desirable and it was sent
   //
   // At the moment only Qt4 layer needs that
   //
   // One needs explicitly cast the first parameter to TGWindow to make
   // it working in the implementation.
   //
   // One needs to process the notification to confine
   // all paint operations within "expose" / "paint" like low level event
   // or equivalent

   return kFALSE;
}

//______________________________________________________________________________
Atom_t  TGCocoa::InternAtom(const char *atomName, Bool_t /*only_if_exist*/)
{
   //X11 properties emulation.
   //TODO: this is a temporary hack to make
   //client message (close window) work.

   assert(atomName != 0 && "InternAtom, atomName is null");
   
   if (!std::strcmp(atomName, "WM_DELETE_WINDOW"))
      return kIA_DELETE_WINDOW;
   else if (!std::strcmp(atomName, "_ROOT_MESSAGE"))
      return kIA_ROOT_MESSAGE;
   
   return Atom_t();
}

//______________________________________________________________________________
Bool_t TGCocoa::CreatePictureFromFile(Drawable_t /*wid*/,
                                      const char * /*filename*/,
                                      Pixmap_t &/*pict*/,
                                      Pixmap_t &/*pict_mask*/,
                                      PictureAttributes_t &/*attr*/)
{
   // Creates a picture pict from data in file "filename". The picture
   // attributes "attr" are used for input and output. Returns kTRUE in
   // case of success, kFALSE otherwise. If the mask "pict_mask" does not
   // exist it is set to kNone.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t TGCocoa::CreatePictureFromData(Drawable_t /*wid*/, char ** /*data*/,
                                      Pixmap_t &/*pict*/,
                                      Pixmap_t &/*pict_mask*/,
                                      PictureAttributes_t & /*attr*/)
{
   // Creates a picture pict from data in bitmap format. The picture
   // attributes "attr" are used for input and output. Returns kTRUE in
   // case of success, kFALSE otherwise. If the mask "pict_mask" does not
   // exist it is set to kNone.

   return kFALSE;
}
//______________________________________________________________________________
Bool_t TGCocoa::ReadPictureDataFromFile(const char * /*filename*/, char *** /*ret_data*/)
{
   // Reads picture data from file "filename" and store it in "ret_data".
   // Returns kTRUE in case of success, kFALSE otherwise.

   return kFALSE;
}

//______________________________________________________________________________
void TGCocoa::DeletePictureData(void * /*data*/)
{
   // Delete picture data created by the function ReadPictureDataFromFile.
}

//______________________________________________________________________________
void TGCocoa::SetDashes(GContext_t /*gc*/, Int_t /*offset*/, const char * /*dash_list*/, Int_t /*n*/)
{
   // Sets the dash-offset and dash-list attributes for dashed line styles
   // in the specified GC. There must be at least one element in the
   // specified dash_list. The initial and alternating elements (second,
   // fourth, and so on) of the dash_list are the even dashes, and the
   // others are the odd dashes. Each element in the "dash_list" array
   // specifies the length (in pixels) of a segment of the pattern.
   //
   // gc        - specifies the GC (see GCValues_t structure)
   // offset    - the phase of the pattern for the dashed line-style you
   //             want to set for the specified GC.
   // dash_list - the dash-list for the dashed line-style you want to set
   //             for the specified GC
   // n         - the number of elements in dash_list
   // (see also the GCValues_t structure)
}

//______________________________________________________________________________
Int_t TGCocoa::EventsPending()
{
   // Returns the number of events that have been received from the X server
   // but have not been removed from the event queue.
   return 0;
}

//______________________________________________________________________________
void TGCocoa::Bell(Int_t /*percent*/)
{
   // Sets the sound bell. Percent is loudness from -100% .. 100%.
}

//______________________________________________________________________________
void TGCocoa::ChangeProperty(Window_t /*wid*/, Atom_t /*property*/,
                             Atom_t /*type*/, UChar_t * /*data*/,
                             Int_t /*len*/)
{
   // Alters the property for the specified window and causes the X server
   // to generate a PropertyNotify event on that window.
   //
   // wid       - the window whose property you want to change
   // property - specifies the property name
   // type     - the type of the property; the X server does not
   //            interpret the type but simply passes it back to
   //            an application that might ask about the window
   //            properties
   // data     - the property data
   // len      - the length of the specified data format
}

//______________________________________________________________________________
Bool_t TGCocoa::CheckEvent(Window_t /*wid*/, EGEventType /*type*/, Event_t & /*ev*/)
{
   //No need in this.
   return kFALSE;
}

/////////////////////////////////////////////////////////
// Next three functions are quite ugly piece of code
// for now. SendEvent is called by GUI classes to, for example,
// execute menu actions or for similar purposes (button was pressed). 
//
// Problems:
// 1. I can not dispatch them immediately: 
// some commands can call DestroWindow(DestroySubwindows),
// for example, on button release event in a dialog, and at the same time
// on button release event GUI wants to repaint unpressed button (for example). 
// But if window was deleted, repaint will cause a crash. So, event is "sent" - 
// I put it into NSApplication's event queue. This
// also has an obvious problem: 
// 2. I put it into NSApplication event queue, and it
// will be (probably) extracted and processed almost immediately after I put it here.
// So in case we execute something heavy and time-consuming, GUI (probably) will "hang".
// 3. Next problem, during execution of sent events, window can be destroyed before
// all events for this window were processed.
//
// For now the solution is the following:
// a) SendEvent creates NSEvent (of type NSApplicationDefined) and puts it into event queue.
// MessageID for this event is "allocated" and saved in data2 property of NSEvent.
// Map Window_t -> events_for_window is filled.
// b) TMacOSXSystem extracts user-defined event from the queue and tries to execute it, using messageID - 
// calls DispatchClientMessage function. DispatchClientMessage checks, if Event_t for messageID
// can be found, and if yes (this assumes window is alive yet) calls window->HandleEvent(clientMessage).
// If DestroyWindow is called for window, I also check, if any events for this window
// were queued and delete them, so if event queue still has events for this window (this can happen :( ) -
// they will not be executed.
//
// TODO: One thing I can try, instead of NSApplication's queue use queue data member in TGCocoa,
// and process queued messages at the end of event loop's iteration (this solution, probably,
// will have the same problems as NSApplication's queue).

//______________________________________________________________________________
void TGCocoa::SendEvent(Window_t wid, Event_t *event)
{
   if (fPimpl->IsRootWindow(wid))//ROOT's GUI can send events to 'root' window.
      return;
      
   if (!wid || !event) //From TGX11.
      return;

   UInt_t messageID = fCurrentMessageID;
   if (fFreeMessageIDs.size()) {
      messageID = fFreeMessageIDs.back();
      fFreeMessageIDs.pop_back();
   } else
      ++fCurrentMessageID;
   
   //Window 'wid' has an event with number 'messageID' in a NSApplication's queue.
   fClientMessagesToWindow[wid].push_back(messageID);

   const ClientMessage_t newMessage(wid, *event);
   assert(fClientMessages.find(messageID) == fClientMessages.end() && "SendEvent, messageID is already busy");
   fClientMessages[messageID] = newMessage;
   
   NSEvent *cocoaEvent = [NSEvent otherEventWithType : NSApplicationDefined location : NSMakePoint(0, 0) modifierFlags : 0
                          timestamp: 0. windowNumber : 0 context : nil subtype : 0 data1 : 0 data2 : NSInteger(messageID)];
   [NSApp postEvent : cocoaEvent atStart : NO];
}

//______________________________________________________________________________
void TGCocoa::DispatchClientMessage(UInt_t messageID)
{
   assert(messageID != 0 && "DispatchClientMessage, messageID parameter is 0");

   message_iterator messageIter = fClientMessages.find(messageID);
   if (messageIter == fClientMessages.end()) {
      //Window for such event was deleted already?
      return;
   }

   NSObject<X11Drawable> *widget = fPimpl->GetDrawable(messageIter->second.first);//:)
   assert(widget.fID != 0 && "DispatchClientMessage, widget.fID is 0");
   
   TGWindow *window = gClient->GetWindowById(widget.fID);
   Event_t clientMessage = messageIter->second.second;

   fClientMessages.erase(messageIter);   
   fFreeMessageIDs.push_back(messageID);

   //Many thanks to ROOT's GUI, TGWindow can be deleted, but QuartzViews is still alive
   //(DestroyWindow is never called for this window).
   if (window)
      window->HandleEvent(&clientMessage);
}

//______________________________________________________________________________
void TGCocoa::RemoveEventsForWindow(Window_t wid)
{
   //Window 'wid' will be deleted, do not process any events for it (if
   //we have any in NSApplication's queue).
   //Remove events for window 'wid', recycle event IDs for future use.
   //Remove entry for window 'wid' and all its event IDs.
   typedef std::vector<UInt_t>::size_type size_type;

   message_window_iterator iter = fClientMessagesToWindow.find(wid);
   if (iter != fClientMessagesToWindow.end()) {
      const std::vector<UInt_t> &messages = iter->second;
      for (size_type i = 0, e = messages.size(); i < e; ++i) {
         message_iterator messageIter = fClientMessages.find(messages[i]);
         if (messageIter != fClientMessages.end()) {//May be, it was deleted already as a result of some DispatchClientMessage??
            fClientMessages.erase(messageIter);
            fFreeMessageIDs.push_back(messages[i]);
         }
      }

      fClientMessagesToWindow.erase(iter);
   }
}

//______________________________________________________________________________
void TGCocoa::WMDeleteNotify(Window_t /*wid*/)
{
   // Tells WM to send message when window is closed via WM.
}

//______________________________________________________________________________
void TGCocoa::SetKeyAutoRepeat(Bool_t /*on = kTRUE*/)
{
   // Turns key auto repeat on (kTRUE) or off (kFALSE).
}

//______________________________________________________________________________
void TGCocoa::GrabKey(Window_t wid, Int_t keyCode, UInt_t rootKeyModifiers, Bool_t grab)
{
   // Establishes a passive grab on the keyboard. In the future, the
   // keyboard is actively grabbed, the last-keyboard-grab time is set
   // to the time at which the key was pressed (as transmitted in the
   // KeyPress event), and the KeyPress event is reported if all of the
   // following conditions are true:
   //    - the keyboard is not grabbed and the specified key (which can
   //      itself be a modifier key) is logically pressed when the
   //      specified modifier keys are logically down, and no other
   //      modifier keys are logically down;
   //    - either the grab window "id" is an ancestor of (or is) the focus
   //      window, or "id" is a descendant of the focus window and contains
   //      the pointer;
   //    - a passive grab on the same key combination does not exist on any
   //      ancestor of grab_window
   //
   // id       - window id
   // keycode  - specifies the KeyCode or AnyKey
   // modifier - specifies the set of keymasks or AnyModifier; the mask is
   //            the bitwise inclusive OR of the valid keymask bits
   // grab     - a switch between grab/ungrab key
   //            grab = kTRUE  grab the key and modifier
   //            grab = kFALSE ungrab the key and modifier

   //Key code already must be Cocoa's key code, this is done by GUI classes,
   //they call KeySymToKeyCode.

   assert(!fPimpl->IsRootWindow(wid) && "GrabKey, called for 'root' window");

   NSView<X11Window> *view = fPimpl->GetWindow(wid).fContentView;
   
   const NSUInteger cocoaKeyModifiers = X11::GetCocoaKeyModifiersFromROOTKeyModifiers(rootKeyModifiers);

   if (grab)
      [view addPassiveKeyGrab : keyCode modifiers : cocoaKeyModifiers];
   else
      [view removePassiveKeyGrab : keyCode modifiers : cocoaKeyModifiers];
}

//______________________________________________________________________________
void TGCocoa::SetMWMHints(Window_t wid, UInt_t value, UInt_t funcs, UInt_t /*input*/)
{
   // Sets decoration style.
   assert(!fPimpl->IsRootWindow(wid) && "SetMWMHints, called for 'root' window");
   
   QuartzWindow *qw = fPimpl->GetWindow(wid).fQuartzWindow;
   NSUInteger newMask = 0;
   
   if ([qw styleMask] & NSTitledWindowMask) {//Do not modify this.
      newMask |= NSTitledWindowMask;
      newMask |= NSClosableWindowMask;
   }

   if (value & kMWMFuncAll) {
      newMask |= NSMiniaturizableWindowMask | NSResizableWindowMask;
   } else {
      if (value & kMWMDecorMinimize)
         newMask |= NSMiniaturizableWindowMask;
      if (funcs & kMWMFuncResize)
         newMask |= NSResizableWindowMask;
   }

   [qw setStyleMask : newMask];
   
   if (funcs & kMWMDecorAll) {
      if (!qw.fMainWindow) {//Do not touch buttons for transient window.
         [[qw standardWindowButton : NSWindowZoomButton] setEnabled : YES];
         [[qw standardWindowButton : NSWindowMiniaturizeButton] setEnabled : YES];
      }
   } else {
      if (!qw.fMainWindow) {//Do not touch transient window's titlebar.
         [[qw standardWindowButton : NSWindowZoomButton] setEnabled : funcs & kMWMDecorMaximize];
         [[qw standardWindowButton : NSWindowMiniaturizeButton] setEnabled : funcs & kMWMDecorMinimize];
      }
   }
}

//______________________________________________________________________________
void TGCocoa::SetWMPosition(Window_t /*wid*/, Int_t /*x*/, Int_t /*y*/)
{
   // Tells the window manager the desired position [x,y] of window "wid".
}

//______________________________________________________________________________
void TGCocoa::SetWMSize(Window_t /*wid*/, UInt_t /*w*/, UInt_t /*h*/)
{
   // Tells window manager the desired size of window "wid".
   //
   // w - the width
   // h - the height
}

//______________________________________________________________________________
void TGCocoa::SetWMSizeHints(Window_t wid, UInt_t wMin, UInt_t hMin, UInt_t wMax, UInt_t hMax, UInt_t /*wInc*/, UInt_t /*hInc*/)
{
   //
   assert(!fPimpl->IsRootWindow(wid) && "SetWMSizeHints, called for 'root' window");

   QuartzWindow *qw = fPimpl->GetWindow(wid).fQuartzWindow;   
   //I can use CGSizeMake, but what if NSSize one bad day becomes something else? :)
   NSSize minSize = {}; minSize.width = wMin, minSize.height = hMin;
   [qw setMinSize : minSize];
   NSSize maxSize = {}; maxSize.width = wMax, maxSize.height = hMax;
   [qw setMaxSize : maxSize];
}

//______________________________________________________________________________
void TGCocoa::SetWMState(Window_t /*wid*/, EInitialState /*state*/)
{
   // Sets the initial state of the window "wid": either kNormalState
   // or kIconicState.
}

//______________________________________________________________________________
void TGCocoa::SetWMTransientHint(Window_t wid, Window_t mainWid)
{
   //Comment from TVirtualX:
   // Tells window manager that the window "wid" is a transient window
   // of the window "main_id". A window manager may decide not to decorate
   // a transient window or may treat it differently in other ways.
   //End of TVirtualX's comment.
   
   //TGTransientFrame uses this hint to attach a window to some "main" window,
   //so that transient window is alway above the main window. This is used for 
   //dialogs and dockable panels.
   assert(Int_t(wid) > fPimpl->GetRootWindowID() && "SetWMTransientHint, wid parameter is a root window");

   if (fPimpl->IsRootWindow(mainWid))
      return;
   
   QuartzWindow *mainWindow = fPimpl->GetWindow(mainWid).fQuartzWindow;
   
   if (![mainWindow isVisible])
      return;
   
   QuartzWindow *transientWindow = fPimpl->GetWindow(wid).fQuartzWindow;

   if (mainWindow != transientWindow) {
      [[transientWindow standardWindowButton : NSWindowZoomButton] setEnabled : NO];
      [mainWindow addTransientWindow : transientWindow];
   } else
      Warning("SetWMTransientHint", "transient and main windows are the same window");
}

//______________________________________________________________________________
Int_t TGCocoa::KeysymToKeycode(UInt_t keySym)
{
   // Converts the "keysym" to the appropriate keycode. For example,
   // keysym is a letter and keycode is the matching keyboard key (which
   // is dependend on the current keyboard mapping). If the specified
   // "keysym" is not defined for any keycode, returns zero.

   return X11::MapKeySymToKeyCode(keySym);
}

//______________________________________________________________________________
Window_t TGCocoa::GetInputFocus()
{
   // Returns the window id of the window having the input focus.

   return fPimpl->fX11EventTranslator.GetInputFocus();
}

//______________________________________________________________________________
void TGCocoa::SetInputFocus(Window_t wid)
{
   // Changes the input focus to specified window "wid".
   assert(!fPimpl->IsRootWindow(wid) && "SetInputFocus, called for 'root' window");
   
   if (wid == kNone)
      fPimpl->fX11EventTranslator.SetInputFocus(nil);
   else
      fPimpl->fX11EventTranslator.SetInputFocus(fPimpl->GetWindow(wid).fContentView);
}

//______________________________________________________________________________
Window_t TGCocoa::GetPrimarySelectionOwner()
{
   // Returns the window id of the current owner of the primary selection.
   // That is the window in which, for example some text is selected.

   return kNone;
}

//______________________________________________________________________________
void TGCocoa::SetPrimarySelectionOwner(Window_t /*wid*/)
{
   // Makes the window "wid" the current owner of the primary selection.
   // That is the window in which, for example some text is selected.
}

//______________________________________________________________________________
void TGCocoa::ConvertPrimarySelection(Window_t /*wid*/, Atom_t /*clipboard*/, Time_t /*when*/)
{
   // Causes a SelectionRequest event to be sent to the current primary
   // selection owner. This event specifies the selection property
   // (primary selection), the format into which to convert that data before
   // storing it (target = XA_STRING), the property in which the owner will
   // place the information (sel_property), the window that wants the
   // information (id), and the time of the conversion request (when).
   // The selection owner responds by sending a SelectionNotify event, which
   // confirms the selected atom and type.
}

//______________________________________________________________________________
void TGCocoa::LookupString(Event_t *event, char *buf, Int_t length, UInt_t &keysym)
{
   // Converts the keycode from the event structure to a key symbol (according
   // to the modifiers specified in the event structure and the current
   // keyboard mapping). In "buf" a null terminated ASCII string is returned
   // representing the string that is currently mapped to the key code.
   //
   // event  - specifies the event structure to be used
   // buf    - returns the translated characters
   // buflen - the length of the buffer
   // keysym - returns the "keysym" computed from the event
   //          if this argument is not NULL
   assert(buf != 0 && "LookupString, buf parameter is null");
   assert(length >= 2 && "LookupString, length parameter - not enough memory to return null-terminated ASCII string");

   X11::MapUnicharToKeySym(event->fCode, buf, length, keysym);
}

//______________________________________________________________________________
void TGCocoa::QueryPointer(Window_t /*wid*/, Window_t &/*rootw*/, Window_t &/*childw*/,
                           Int_t &/*root_x*/, Int_t &/*root_y*/, Int_t &/*win_x*/,
                           Int_t &/*win_y*/, UInt_t &/*mask*/)
{
   // Returns the root window the pointer is logically on and the pointer
   // coordinates relative to the root window's origin.
   //
   // id             - specifies the window
   // rotw           - the root window that the pointer is in
   // childw         - the child window that the pointer is located in, if any
   // root_x, root_y - the pointer coordinates relative to the root window's
   //                  origin
   // win_x, win_y   - the pointer coordinates relative to the specified
   //                  window "id"
   // mask           - the current state of the modifier keys and pointer
   //                  buttons
}

//______________________________________________________________________________
void TGCocoa::SetClipRectangles(GContext_t /*gc*/, Int_t /*x*/, Int_t /*y*/,
                                Rectangle_t * /*recs*/, Int_t /*n*/)
{
   // Sets clipping rectangles in graphics context. [x,y] specify the origin
   // of the rectangles. "recs" specifies an array of rectangles that define
   // the clipping mask and "n" is the number of rectangles.
   // (see also the GCValues_t structure)
}

//______________________________________________________________________________
Region_t TGCocoa::CreateRegion()
{
   // Creates a new empty region.

   return 0;
}

//______________________________________________________________________________
void TGCocoa::DestroyRegion(Region_t /*reg*/)
{
   // Destroys the region "reg".
}

//______________________________________________________________________________
void TGCocoa::UnionRectWithRegion(Rectangle_t * /*rect*/, Region_t /*src*/, Region_t /*dest*/)
{
   // Updates the destination region from a union of the specified rectangle
   // and the specified source region.
   //
   // rect - specifies the rectangle
   // src  - specifies the source region to be used
   // dest - returns the destination region
}

//______________________________________________________________________________
Region_t TGCocoa::PolygonRegion(Point_t * /*points*/, Int_t /*np*/, Bool_t /*winding*/)
{
   // Returns a region for the polygon defined by the points array.
   //
   // points  - specifies an array of points
   // np      - specifies the number of points in the polygon
   // winding - specifies the winding-rule is set (kTRUE) or not(kFALSE)

   return 0;
}

//______________________________________________________________________________
void TGCocoa::UnionRegion(Region_t /*rega*/, Region_t /*regb*/, Region_t /*result*/)
{
   // Computes the union of two regions.
   //
   // rega, regb - specify the two regions with which you want to perform
   //              the computation
   // result     - returns the result of the computation

}

//______________________________________________________________________________
void TGCocoa::IntersectRegion(Region_t /*rega*/, Region_t /*regb*/, Region_t /*result*/)
{
   // Computes the intersection of two regions.
   //
   // rega, regb - specify the two regions with which you want to perform
   //              the computation
   // result     - returns the result of the computation
}

//______________________________________________________________________________
void TGCocoa::SubtractRegion(Region_t /*rega*/, Region_t /*regb*/, Region_t /*result*/)
{
   // Subtracts regb from rega and stores the results in result.
}

//______________________________________________________________________________
void TGCocoa::XorRegion(Region_t /*rega*/, Region_t /*regb*/, Region_t /*result*/)
{
   // Calculates the difference between the union and intersection of
   // two regions.
   //
   // rega, regb - specify the two regions with which you want to perform
   //              the computation
   // result     - returns the result of the computation

}

//______________________________________________________________________________
Bool_t  TGCocoa::EmptyRegion(Region_t /*reg*/)
{
   // Returns kTRUE if the region reg is empty.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t  TGCocoa::PointInRegion(Int_t /*x*/, Int_t /*y*/, Region_t /*reg*/)
{
   // Returns kTRUE if the point [x, y] is contained in the region reg.

   return kFALSE;
}

//______________________________________________________________________________
Bool_t  TGCocoa::EqualRegion(Region_t /*rega*/, Region_t /*regb*/)
{
   // Returns kTRUE if the two regions have the same offset, size, and shape.

   return kFALSE;
}

//______________________________________________________________________________
void TGCocoa::GetRegionBox(Region_t /*reg*/, Rectangle_t * /*rect*/)
{
   // Returns smallest enclosing rectangle.
}

//______________________________________________________________________________
Drawable_t TGCocoa::CreateImage(UInt_t width, UInt_t height)
{
   // Allocates the memory needed for an drawable.
   //
   // width  - the width of the image, in pixels
   // height - the height of the image, in pixels
   return OpenPixmap(width, height);
}

//______________________________________________________________________________
void TGCocoa::GetImageSize(Drawable_t wid, UInt_t &width, UInt_t &height)
{
   // Returns the width and height of the image wid
   assert(int(wid) > fPimpl->GetRootWindowID() && "GetImageSize, wid parameter is a bad image id");
   
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(wid);
   width = drawable.fWidth;
   height = drawable.fHeight;
}

//______________________________________________________________________________
void TGCocoa::PutPixel(Drawable_t /*wid*/, Int_t /*x*/, Int_t /*y*/, ULong_t /*pixel*/)
{
   // Overwrites the pixel in the image with the specified pixel value.
   // The image must contain the x and y coordinates.
   //
   // wid   - specifies the image
   // x, y  - coordinates
   // pixel - the new pixel value
}

//______________________________________________________________________________
void TGCocoa::PutImage(Drawable_t /*wid*/, GContext_t /*gc*/,
                       Drawable_t /*img*/, Int_t /*dx*/, Int_t /*dy*/,
                       Int_t /*x*/, Int_t /*y*/, UInt_t /*w*/, UInt_t /*h*/)
{
   // Combines an image with a rectangle of the specified drawable. The
   // section of the image defined by the x, y, width, and height arguments
   // is drawn on the specified part of the drawable.
   //
   // wid  - the drawable
   // gc   - the GC
   // img  - the image you want combined with the rectangle
   // dx   - the offset in X from the left edge of the image
   // dy   - the offset in Y from the top edge of the image
   // x, y - coordinates, which are relative to the origin of the
   //        drawable and are the coordinates of the subimage
   // w, h - the width and height of the subimage, which define the
   //        rectangle dimensions
   //
   // GC components in use: function, plane-mask, subwindow-mode,
   // clip-x-origin, clip-y-origin, and clip-mask.
   // GC mode-dependent components: foreground and background.
   // (see also the GCValues_t structure)
}

//______________________________________________________________________________
void TGCocoa::DeleteImage(Drawable_t /*img*/)
{
   // Deallocates the memory associated with the image img
}

//______________________________________________________________________________
void TGCocoa::ShapeCombineMask(Window_t, Int_t, Int_t, Pixmap_t)
{
   // The Nonrectangular Window Shape Extension adds nonrectangular
   // windows to the System.
   // This allows for making shaped (partially transparent) windows
}

//______________________________________________________________________________
UInt_t TGCocoa::ScreenWidthMM() const
{
   //Comment from TVirtualX: Returns the width of the screen in millimeters.
/*
   NSArray *screens = [NSScreen screens];
   assert(screens != nil && "screens array is nil");
   
   NSScreen *mainScreen = [screens objectAtIndex : 0];
   assert(mainScreen != nil && "screen with index 0 is nil");

   NSDictionary *screenParameters = [mainScreen deviceDescription];
   assert(screenParameters != nil && "deviceDescription dictionary is nil");
   
   //This casts are just terrible and rely on the current documentation only.
   //But this is ... elegant Objective-C.
   NSNumber *screenNumber = (NSNumber *)[screenParameters objectForKey : @"NSScreenNumber"];
   assert(screenNumber != nil && "no screen number in device description");
   const CGSize screenSize = CGDisplayScreenSize([screenNumber integerValue]);
  */

   return CGDisplayScreenSize(CGMainDisplayID()).width;
}

//______________________________________________________________________________
void TGCocoa::DeleteProperty(Window_t, Atom_t&)
{
   // Deletes the specified property only if the property was defined on the
   // specified window and causes the X server to generate a PropertyNotify
   // event on the window unless the property does not exist.

}

//______________________________________________________________________________
Int_t TGCocoa::GetProperty(Window_t, Atom_t, Long_t, Long_t, Bool_t, Atom_t,
                           Atom_t*, Int_t*, ULong_t*, ULong_t*, unsigned char**)
{
   // Returns the actual type of the property; the actual format of the property;
   // the number of 8-bit, 16-bit, or 32-bit items transferred; the number of
   // bytes remaining to be read in the property; and a pointer to the data
   // actually returned.

   return 0;
}

//______________________________________________________________________________
void TGCocoa::ChangeActivePointerGrab(Window_t, UInt_t, Cursor_t)
{
   // Changes the specified dynamic parameters if the pointer is actively
   // grabbed by the client and if the specified time is no earlier than the
   // last-pointer-grab time and no later than the current X server time.

}

//______________________________________________________________________________
void TGCocoa::ConvertSelection(Window_t, Atom_t&, Atom_t&, Atom_t&, Time_t&)
{
   // Requests that the specified selection be converted to the specified
   // target type.

}

//______________________________________________________________________________
Bool_t TGCocoa::SetSelectionOwner(Window_t, Atom_t&)
{
   // Changes the owner and last-change time for the specified selection.

   return kFALSE;
}

//______________________________________________________________________________
void TGCocoa::ChangeProperties(Window_t, Atom_t, Atom_t, Int_t, UChar_t *, Int_t)
{
   // Alters the property for the specified window and causes the X server
   // to generate a PropertyNotify event on that window.
}

//______________________________________________________________________________
void TGCocoa::SetDNDAware(Window_t, Atom_t *)
{
   // Add XdndAware property and the list of drag and drop types to the
   // Window win.

}

//______________________________________________________________________________
void TGCocoa::SetTypeList(Window_t, Atom_t, Atom_t *)
{
   // Add the list of drag and drop types to the Window win.

}

//______________________________________________________________________________
Window_t TGCocoa::FindRWindow(Window_t, Window_t, Window_t, int, int, int)
{
   // Recursively search in the children of Window for a Window which is at
   // location x, y and is DND aware, with a maximum depth of maxd.

   return kNone;
}

//______________________________________________________________________________
Bool_t TGCocoa::IsDNDAware(Window_t, Atom_t *)
{
   // Checks if the Window is DND aware, and knows any of the DND formats
   // passed in argument.

   return kFALSE;
}

//______________________________________________________________________________
void TGCocoa::BeginModalSessionFor(Window_t wid)
{
   assert(!fPimpl->IsRootWindow(wid) && "BeginModalSessionFor, called for 'root' window");
   
   //We start special modal session _ONLY_ for dialogs.
   //Everything else is done in a different way.
   if (IsDialog(wid)) {
      //QuartzWindow *qw = fPimpl->GetWindow(wid).fQuartzWindow;
   }   
}

//______________________________________________________________________________
Int_t TGCocoa::SupportsExtension(const char *) const
{
   // Returns 1 if window system server supports extension given by the
   // argument, returns 0 in case extension is not supported and returns -1
   // in case of error (like server not initialized).

   return -1;
}

//______________________________________________________________________________
ROOT::MacOSX::X11::EventTranslator *TGCocoa::GetEventTranslator()const
{
   return &fPimpl->fX11EventTranslator;
}

//______________________________________________________________________________
ROOT::MacOSX::X11::CommandBuffer *TGCocoa::GetCommandBuffer()const
{
   return &fPimpl->fX11CommandBuffer;
}

//______________________________________________________________________________
void TGCocoa::CocoaDrawON()
{
   ++fCocoaDraw;
}

//______________________________________________________________________________
void TGCocoa::CocoaDrawOFF()
{
   assert(fCocoaDraw > 0 && "CocoaDrawOFF, was already off");
   --fCocoaDraw;
}

//______________________________________________________________________________
bool TGCocoa::IsCocoaDraw()const
{
   return bool(fCocoaDraw);
}

//______________________________________________________________________________
void *TGCocoa::GetCurrentContext()
{
   NSObject<X11Drawable> *drawable = fPimpl->GetDrawable(fSelectedDrawable);
   if (!drawable.fIsPixmap) {
      Error("GetCurrentContext", "TCanvas/TPad's internal error, selected drawable is not a pixmap!");
      return 0;
   }
   
   return drawable.fContext;
}

//______________________________________________________________________________
bool TGCocoa::IsDialog(Window_t wid)const
{
   if (Int_t(wid) <= fPimpl->GetRootWindowID())
      return false;
      
   TGWindow *window = gClient->GetWindowById(wid);
   if (!window)
      return false;
      
   const NSUInteger styleMask = [fPimpl->GetWindow(wid).fQuartzWindow styleMask];
   
   if (window->InheritsFrom("TGTransientFrame") && styleMask != NSBorderlessWindowMask)
       return true;
   
   return false;
}

//______________________________________________________________________________
bool TGCocoa::MakeProcessForeground()
{
   //We start root in a terminal window, so it's considered as a 
   //background process. Background process has a lot of problems
   //if it tries to create and manage windows.
   //So, first time we convert process to foreground, next time
   //we make it front.
   
   if (!fForegroundProcess) {
      ProcessSerialNumber psn = {0, kCurrentProcess};

      const OSStatus res1 = TransformProcessType(&psn, kProcessTransformToForegroundApplication);
      if (res1 != noErr) {
         Error("MakeProcessForeground", "TransformProcessType failed with code %d", res1);
         return false;
      }
   
      const OSErr res2 = SetFrontProcess(&psn);
      if (res2 != noErr) {
         Error("MakeProcessForeground", "SetFrontProcess failed with code %d", res2);
         return false;
      }

      fForegroundProcess = true;
   } else {
      ProcessSerialNumber psn = {};    

      OSErr res = GetCurrentProcess(&psn);
      if (res != noErr) {
         Error("MapProcessForeground", "GetCurrentProcess failed with code %d", res);
         return false;
      }
      
      res = SetFrontProcess(&psn);
      if (res != noErr) {
         Error("MapProcessForeground", "SetFrontProcess failed with code %d", res);
         return false;
      }
   }
   
   return true;
}
