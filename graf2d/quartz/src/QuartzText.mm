//Author: Timur Pocheptsov.
#include <stdexcept>
#include <cassert>
#include <vector>
#include <cmath>

#include "QuartzText.h"
#include "CocoaUtils.h"
#include "TVirtualX.h"
#include "TColor.h"
#include "TError.h"
#include "TROOT.h"
#include "TMath.h"

namespace ROOT {
namespace Quartz {

namespace {

//______________________________________________________________________________
void GetTextColorForIndex(Color_t colorIndex, Float_t &r, Float_t &g, Float_t &b)
{
   if (const TColor *color = gROOT->GetColor(colorIndex))
      color->GetRGB(r, g, b);
}

}

//_________________________________________________________________
TextLine::TextLine(const char *textLine, CTFontRef font)
             : fCTLine(0)
{
   //Create attributed string with one attribue: the font.
   CFStringRef keys[] = {kCTFontAttributeName};
   CFTypeRef values[] = {font};

   Init(textLine, 1, keys, values);
}

//_________________________________________________________________
TextLine::TextLine(const std::vector<UniChar> &unichars, CTFontRef font)
{
   //Create attributed string with one attribue: the font.
   CFStringRef keys[] = {kCTFontAttributeName};
   CFTypeRef values[] = {font};

   Init(unichars, 1, keys, values);
}

//_________________________________________________________________
TextLine::TextLine(const char *textLine, CTFontRef font, Color_t color)
            : fCTLine(0)
{
   //Create attributed string with font and color.
   using MacOSX::Util::CFScopeGuard;

   const CFScopeGuard<CGColorSpaceRef> rgbColorSpace(CGColorSpaceCreateDeviceRGB());
   if (!rgbColorSpace.Get())
      throw std::runtime_error("TextLine: color space");

   Float_t rgba[] = {0.f, 0.f, 0.f, 1.f};
   GetTextColorForIndex(color, rgba[0], rgba[1], rgba[2]);
   const CGFloat cgRgba[] = {rgba[0], rgba[1], rgba[2], 1.};

   const CFScopeGuard<CGColorRef> textColor(CGColorCreate(rgbColorSpace.Get(), cgRgba));
   //Not clear from docs, if textColor.Get() can be 0.
   
   CFStringRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
   CFTypeRef values[] = {font, textColor.Get()};

   Init(textLine, 2, keys, values);
}

//_________________________________________________________________
TextLine::TextLine(const char *textLine, CTFontRef font, const CGFloat *rgb)
            : fCTLine(0)
{
   //Create attributed string with font and color.
   using ROOT::MacOSX::Util::CFScopeGuard;
   CFScopeGuard<CGColorSpaceRef> rgbColorSpace(CGColorSpaceCreateDeviceRGB());
   
   if (!rgbColorSpace.Get())
      throw std::runtime_error("TexLine: color space is null");

   CFScopeGuard<CGColorRef> textColor(CGColorCreate(rgbColorSpace.Get(), rgb));
   //Not clear from docs, if textColor can be 0.

   CFStringRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
   CFTypeRef values[] = {font, textColor.Get()};

   Init(textLine, 2, keys, values);
}

//_________________________________________________________________
TextLine::TextLine(const std::vector<UniChar> &unichars, CTFontRef font, Color_t color)
            : fCTLine(0)
{
   //Create attributed string with font and color.
   //TODO: Make code more general, this constructor is copy&paste.
   using MacOSX::Util::CFScopeGuard;

   const CFScopeGuard<CGColorSpaceRef> rgbColorSpace(CGColorSpaceCreateDeviceRGB());
   if (!rgbColorSpace.Get())
      throw std::runtime_error("TextLine: color space");

   Float_t rgba[] = {0.f, 0.f, 0.f, 1.f};
   GetTextColorForIndex(color, rgba[0], rgba[1], rgba[2]);
   const CGFloat cgRgba[] = {rgba[0], rgba[1], rgba[2], 1.};

   const CFScopeGuard<CGColorRef> textColor(CGColorCreate(rgbColorSpace.Get(), cgRgba));
   //Not clear from docs, if textColor.Get() can be 0.
   
   CFStringRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
   CFTypeRef values[] = {font, textColor.Get()};

   Init(unichars, 2, keys, values);
}


//_________________________________________________________________
TextLine::~TextLine()
{
   CFRelease(fCTLine);
}


//_________________________________________________________________
void TextLine::GetBounds(UInt_t &w, UInt_t &h)const
{
   CGFloat ascent = 0., descent = 0., leading = 0.;
   w = UInt_t(CTLineGetTypographicBounds(fCTLine, &ascent, &descent, &leading));
   h = UInt_t(ascent);// + descent + leading);
}


//_________________________________________________________________
void TextLine::GetAscentDescent(Int_t &asc, Int_t &desc)const
{
   CGFloat ascent = 0., descent = 0., leading = 0.;
   CTLineGetTypographicBounds(fCTLine, &ascent, &descent, &leading);
   asc = Int_t(ascent);
   desc = Int_t(descent);
}


//_________________________________________________________________
void TextLine::Init(const char *textLine, UInt_t nAttribs, CFStringRef *keys, CFTypeRef *values)
{
   using MacOSX::Util::CFScopeGuard;
   
   //Strong reference must be replaced with scope guards.
   const CFScopeGuard<CFDictionaryRef> stringAttribs(CFDictionaryCreate(kCFAllocatorDefault, (const void **)keys, (const void **)values,
                                                     nAttribs, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
   if (!stringAttribs.Get())
      throw std::runtime_error("TextLine: null attribs");

   const CFScopeGuard<CFStringRef> wrappedCString(CFStringCreateWithCString(kCFAllocatorDefault, textLine, kCFStringEncodingMacRoman));
   if (!wrappedCString.Get())
      throw std::runtime_error("TextLine: cstr wrapper");

   CFScopeGuard<CFAttributedStringRef> attributedString(CFAttributedStringCreate(kCFAllocatorDefault, wrappedCString.Get(), stringAttribs.Get()));
   fCTLine = CTLineCreateWithAttributedString(attributedString.Get());

   if (!fCTLine)
      throw std::runtime_error("TextLine: attrib string");
}

//_________________________________________________________________
void TextLine::Init(const std::vector<UniChar> &unichars, UInt_t nAttribs, CFStringRef *keys, CFTypeRef *values)
{
   using MacOSX::Util::CFScopeGuard;
   
   const CFScopeGuard<CFStringRef> wrappedUniString(CFStringCreateWithCharacters(kCFAllocatorDefault, &unichars[0], unichars.size()));
   const CFScopeGuard<CFDictionaryRef> stringAttribs(CFDictionaryCreate(kCFAllocatorDefault, (const void **)keys, (const void **)values,
                                                           nAttribs, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
   
   if (!stringAttribs.Get())
      throw std::runtime_error("TextLine: null attribs");

   if (!wrappedUniString.Get())
      throw std::runtime_error("TextLine: cstr wrapper");

   const CFScopeGuard<CFAttributedStringRef> attributedString(CFAttributedStringCreate(kCFAllocatorDefault, wrappedUniString.Get(), stringAttribs.Get()));
   fCTLine = CTLineCreateWithAttributedString(attributedString.Get());

   if (!fCTLine)
      throw std::runtime_error("TextLine: attrib string");
}

//_________________________________________________________________
void TextLine::DrawLine(CGContextRef ctx)const
{
   assert(ctx != nullptr && "DrawLine, ctx parameter is null");
   CTLineDraw(fCTLine, ctx);
}


//______________________________________________________________________________
void TextLine::DrawLine(CGContextRef ctx, Double_t x, Double_t y)const
{
   assert(ctx != nullptr && "DrawLine, ctx parameter is null");

   CGContextSetAllowsAntialiasing(ctx, 1);
   UInt_t w = 0, h = 0;
   
   GetBounds(w, h);
   
   Double_t xc = 0., yc = 0.;   
   const UInt_t hAlign = UInt_t(gVirtualX->GetTextAlign() / 10);   
   switch (hAlign) {
   case 1:
      xc = 0.5 * w;
      break;
   case 2:
      break;
   case 3:
      xc = -0.5 * w;
      break;
   }

   const UInt_t vAlign = UInt_t(gVirtualX->GetTextAlign() % 10);
   switch (vAlign) {
   case 1:
      yc = 0.5 * h;
      break;
   case 2:
      break;
   case 3:
      yc = -0.5 * h;
      break;
   }
   
   CGContextSetTextPosition(ctx, 0., 0.);
   CGContextTranslateCTM(ctx, x, y);  
   CGContextRotateCTM(ctx, gVirtualX->GetTextAngle() * TMath::DegToRad());
   CGContextTranslateCTM(ctx, xc, yc);
   CGContextTranslateCTM(ctx, -0.5 * w, -0.5 * h);

   DrawLine(ctx);
}

//______________________________________________________________________________
void DrawTextLineNoKerning(CGContextRef ctx, CTFontRef font, const std::vector<UniChar> &text, Int_t x, Int_t y)
{
   typedef std::vector<CGSize>::size_type size_type;
   
   if (!text.size())//This can happen with ROOT's GUI.
      return;

   assert(ctx != nullptr && "DrawTextLineNoKerning, ctx parameter is null");
   assert(font != nullptr && "DrawTextLineNoKerning, font parameter is null");
   assert(text.size() && "DrawTextLineNoKerning, text parameter is an empty vector");

   std::vector<CGGlyph> glyphs(text.size());
   if (!CTFontGetGlyphsForCharacters(font, &text[0], &glyphs[0], text.size())) {
      ::Error("DrawTextLineNoKerning", "Font could not encode all Unicode characters in a text");
      return;
   }
   
   std::vector<CGSize> glyphAdvances(glyphs.size());
   CTFontGetAdvancesForGlyphs(font, kCTFontHorizontalOrientation, &glyphs[0], &glyphAdvances[0], glyphs.size());

   CGFloat currentX = x;  
   std::vector<CGPoint> glyphPositions(glyphs.size());
   glyphPositions[0].x = currentX;
   glyphPositions[0].y = y;

   for (size_type i = 1; i < glyphs.size(); ++i) {
      currentX += std::ceil(glyphAdvances[i - 1].width);
      glyphPositions[i].x = currentX;
      glyphPositions[i].y = y;
   }
   
   CTFontDrawGlyphs(font, &glyphs[0], &glyphPositions[0], glyphs.size(), ctx);
}

}
}
