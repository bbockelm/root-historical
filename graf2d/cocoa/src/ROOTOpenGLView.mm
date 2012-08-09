// @(#)root/graf2d:$Id$
// Author: Timur Pocheptsov   26/04/2012

/*************************************************************************
 * Copyright (C) 1995-2012, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#import <cassert>

#import "ROOTOpenGLView.h"
#import "X11Events.h"
#import "TGCocoa.h"

namespace ROOT {
namespace MacOSX {
namespace OpenGL {

//______________________________________________________________________________
bool GLViewIsValidDrawable(ROOTOpenGLView *glView)
{
   assert(glView != nil && "GLViewIsValid, glView parameter is nil");
   
   if ([glView isHiddenOrHasHiddenAncestor]) {
      //This will result in "invalid drawable" message
      //from -setView:.
      return false;
   }

   const NSRect visibleRect = [glView visibleRect];
   if (visibleRect.size.width < 1. || visibleRect.size.height < 1.) {
      //Another reason for "invalid drawable" message.
      return false;
   }

   return true;
}

}
}
}

@implementation ROOTOpenGLView {
   BOOL            fIsOverlapped;
   
   NSOpenGLPixelFormat *fPixelFormat;
   NSOpenGLContext *fOpenGLContext;
   
   BOOL fUpdateContext;
}

@synthesize fUpdateContext;

//______________________________________________________________________________
- (id) initWithFrame : (NSRect) frameRect pixelFormat : (NSOpenGLPixelFormat *) format
{
   if (self = [super initWithFrame : frameRect windowAttributes : 0]) {
      [self setHidden : YES];//Not sure.
      fIsOverlapped = NO;
      fPixelFormat = [format retain];
   }

   return self;
}

//______________________________________________________________________________
- (void) dealloc
{
   if (fOpenGLContext && [fOpenGLContext view] == self)
      [fOpenGLContext clearDrawable];

   [fPixelFormat release];
   //View does not own context.
   [super dealloc];
}

//______________________________________________________________________________
- (void) setOpenGLContext : (NSOpenGLContext *) context
{
   //View does not own context, no changes in any ref counter == weak reference.
   fOpenGLContext = context;
}

//______________________________________________________________________________
- (NSOpenGLPixelFormat *) pixelFormat
{
   return fPixelFormat;
}

//______________________________________________________________________________
- (void) setPixelFormat : (NSOpenGLPixelFormat *) pixelFormat
{
   if (fPixelFormat != pixelFormat) {
      [fPixelFormat release];
      fPixelFormat = [pixelFormat retain];
   }
}

//X11Drawable protocol.

//______________________________________________________________________________
- (BOOL) fIsOpenGLWidget
{
   return YES;
}

//______________________________________________________________________________
- (QuartzPixmap *) fBackBuffer
{
   //GL-view does not have/need any "back buffer".
   return nil;
}

//______________________________________________________________________________
- (void) setFBackBuffer : (QuartzPixmap *) notUsed
{
   //GL-view does not have/need any "back buffer".
   (void) notUsed;
}

//______________________________________________________________________________
- (void) mapWindow
{   
   [self setHidden : NO];
}

//______________________________________________________________________________
- (void) mapSubwindows
{
   //GL-view can not have any subwindows.
   assert([[self subviews] count] == 0 && "mapSubwindows, GL-view has children");
}

//______________________________________________________________________________
- (void) configureNotifyTree
{
   //The only node in the tree is 'self'.
   if (self.fMapState == kIsViewable) {
      if (self.fEventMask & kStructureNotifyMask) {
         TGCocoa * const vx = dynamic_cast<TGCocoa *>(gVirtualX);
         assert(vx && "configureNotifyTree, gVirtualX is either null or has type different from TGCocoa");
         vx->GetEventTranslator()->GenerateConfigureNotifyEvent(self, self.frame);
      }
   }
}

//______________________________________________________________________________
- (BOOL) fIsOverlapped
{
   return fIsOverlapped;
}

//______________________________________________________________________________
- (void) setOverlapped : (BOOL) overlap
{
   //If GL-view is overlapped by another view,
   //it must be hidden (overwise it will be always on top,
   //producing some strange-looking buggy GUI).

   fIsOverlapped = overlap;
   [self setHidden : fIsOverlapped];

   if (!overlap) {
      TGCocoa * const vx = dynamic_cast<TGCocoa *>(gVirtualX);
      assert(vx != 0 && "setFrameSize:, gVirtualX is either null or has a type, different from TGCocoa");
      [fOpenGLContext update];
      //View becomes visible, geometry can be changed at this point,
      //notify ROOT's GL code about this changes.
      vx->GetEventTranslator()->GenerateConfigureNotifyEvent(self, self.frame);
      vx->GetEventTranslator()->GenerateExposeEvent(self, self.frame);
   }
}

//______________________________________________________________________________
- (BOOL) isFlipped 
{
   return YES;
}

//______________________________________________________________________________
- (void) setFrame : (NSRect) newFrame
{
   //In case of TBrowser, setFrame started infinite recursion:
   //HandleConfigure for embedded main frame emits signal, slot
   //calls layout, layout calls setFrame -> HandleConfigure and etc. etc.
   if (CGRectEqualToRect(newFrame, self.frame))
      return;

   [super setFrame : newFrame];
}

//______________________________________________________________________________
- (void) setFrameSize : (NSSize) newSize
{
   //Check, if setFrameSize calls setFrame.
   
   [super setFrameSize : newSize];
   
   if (![self isHiddenOrHasHiddenAncestor] && !fIsOverlapped)
      [fOpenGLContext update];
   else 
      fUpdateContext = YES;
   
   if ((self.fEventMask & kStructureNotifyMask) && (self.fMapState == kIsViewable || fIsOverlapped == YES)) {
      TGCocoa * const vx = dynamic_cast<TGCocoa *>(gVirtualX);
      assert(vx != 0 && "setFrameSize:, gVirtualX is either null or has a type, different from TGCocoa");
      vx->GetEventTranslator()->GenerateConfigureNotifyEvent(self, self.frame);
      vx->GetEventTranslator()->GenerateExposeEvent(self, self.frame);
   }
}

//______________________________________________________________________________
- (void) drawRect : (NSRect) dirtyRect
{
   (void) dirtyRect;
/*
   if ((fEventMask & kStructureNotifyMask) && (self.fMapState == kIsViewable || fIsOverlapped == YES)) {
      TGCocoa * const vx = dynamic_cast<TGCocoa *>(gVirtualX);
      assert(vx != 0 && "drawRect:, gVirtualX is either null or has a type, different from TGCocoa");
      vx->GetEventTranslator()->GenerateConfigureNotifyEvent(self, self.frame);
      vx->GetEventTranslator()->GenerateExposeEvent(self, self.frame);
   }*/
}

@end
