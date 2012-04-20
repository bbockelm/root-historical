//Author: Timur Pocheptsov 16/02/2012

#ifndef ROOT_QuartzPixmap
#define ROOT_QuartzPixmap

#import <Cocoa/Cocoa.h>

#import "X11Drawable.h"
#import "GuiTypes.h"

///////////////////////////////////////////////////////
//                                                   //
// "Pixmap". Graphical context to draw into image.   //
//                                                   //
///////////////////////////////////////////////////////

@interface QuartzPixmap : NSObject<X11Drawable>

- (id) initWithW : (unsigned) width H : (unsigned) height;
- (BOOL) resizeW : (unsigned) width H : (unsigned) height;

- (CGImageRef) createImageFromPixmap;
- (CGImageRef) createImageFromPixmap : (Rectangle_t) cropArea;

//X11Drawable protocol.

@property (nonatomic, assign) unsigned fID;

- (BOOL) fIsPixmap;
- (BOOL) fIsOpenGLWidget;

@property (nonatomic, readonly) CGContextRef fContext;

- (unsigned) fWidth;
- (unsigned) fHeight;

//Point_t, Rectangle_t are in GuiTypes.h
- (void) copy : (NSObject<X11Drawable> *) src area : (Rectangle_t) area withMask : (QuartzImage *) mask 
         clipOrigin : (Point_t) origin toPoint : (Point_t) dstPoint;

//
- (unsigned char *) fData;

@end

/////////////////////////////////////////////////////////
//                                                     //
// CGImageRef, created from external data source (raw  //
// data)                                               //
//                                                     //
/////////////////////////////////////////////////////////

@interface QuartzImage : NSObject<X11Drawable>

- (id) initWithW : (unsigned) width H : (unsigned) height data : (unsigned char *) data;
- (id) initMaskWithW : (unsigned) width H : (unsigned) height bitmapMask : (unsigned char *) mask;
- (void) dealloc;
@property (nonatomic, readonly) BOOL fIsStippleMask;
- (CGImageRef) fImage;

//X11Drawable protocol.
@property (nonatomic, assign) unsigned fID;

- (BOOL) fIsPixmap;
- (BOOL) fIsOpenGLWidget;

- (unsigned) fWidth;
- (unsigned) fHeight;

- (unsigned char *) readColorBits : (Rectangle_t) area;

@end


namespace ROOT {
namespace MacOSX {
namespace X11 {//X11 emulation. But must go into quartz module later.

CGImageRef CreateSubImage(QuartzImage *image, const Rectangle_t &area);
//
bool AdjustCropArea(const Rectangle_t &srcRect, Rectangle_t &cropArea);
bool AdjustCropArea(QuartzImage *srcImage, Rectangle_t &cropArea);
bool AdjustCropArea(QuartzPixmap *srcImage, Rectangle_t &cropArea);

//Aux. function for TGCocoa.
void FillPixmapBuffer(const unsigned char *bitmap, unsigned width, unsigned height, ULong_t foregroundPixel, 
                      ULong_t backgroundPixel, unsigned depth, unsigned char *imageData);

}
}
}

#endif
