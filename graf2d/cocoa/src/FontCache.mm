// @(#)root/graf2d:$Id$
// Author: Timur Pocheptsov   19/03/2012

/*************************************************************************
 * Copyright (C) 1995-2012, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

//#define NDEBUG

#include <stdexcept>
#include <sstream>
#include <cassert>
#include <string>
#include <cmath>

#include "CocoaUtils.h"
#include "QuartzText.h"
#include "FontCache.h"
#include "TSystem.h"
#include "TError.h"
#include "TEnv.h"

namespace ROOT {
namespace MacOSX {
namespace Details {

namespace {

//ROOT uses indices for fonts.
//Later, I'll find (I promise! ;) better
//way to map font indices to actual fonts
//(families, etc.) - I simply do not have any time now.

const int fmdNOfFonts = 13;
const CFStringRef fixedFontNames[FontCache::nPadFonts] =
                                     {
                                      CFSTR("TimesNewRomanPS-ItalicMT"),
                                      CFSTR("TimesNewRomanPS-BoldMT"),
                                      CFSTR("TimesNewRomanPS-BoldItalicMT"),
                                      CFSTR("Helvetica"),
                                      CFSTR("Helvetica-Oblique"),
                                      CFSTR("Helvetica-Bold"),
                                      CFSTR("Helvetica-BoldOblique"),
                                      CFSTR("Courier"),
                                      CFSTR("Courier-Oblique"),
                                      CFSTR("Courier-Bold"),
                                      CFSTR("Courier-BoldOblique"),
                                      CFSTR("Symbol"),
                                      CFSTR("TimesNewRomanPSMT")
                                     };


//______________________________________________________________________________
CTFontCollectionRef CreateFontCollection(const X11::XLFDName &xlfd)
{
   CTFontCollectionRef ctCollection = 0;
   if (xlfd.fFamilyName == "*")
      ctCollection = CTFontCollectionCreateFromAvailableFonts(0);//Select all available fonts.
   else {
      //Create collection, using font descriptor?
      const Util::CFScopeGuard<CFStringRef> fontName(CFStringCreateWithCString(kCFAllocatorDefault, xlfd.fFamilyName.c_str(), kCFStringEncodingMacRoman));
      if (!fontName.Get()) {
         ::Error("CreateFontCollection", "CFStringCreateWithCString failed");
         return 0;
      }

      const Util::CFScopeGuard<CTFontDescriptorRef> fontDescriptor(CTFontDescriptorCreateWithNameAndSize(fontName.Get(), 0.));
      if (!fontDescriptor.Get()) {
         ::Error("CreateFontCollection", "CTFontDescriptorCreateWithNameAndSize failed");
         return 0;
      }

      Util::CFScopeGuard<CFMutableArrayRef> descriptors(CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
      if (!descriptors.Get()) {
         ::Error("CreateFontCollection", "CFArrayCreateMutable failed");
         return 0;
      }

      CFArrayAppendValue(descriptors.Get(), fontDescriptor.Get());
      ctCollection = CTFontCollectionCreateWithFontDescriptors(descriptors.Get(), 0);//Oh mama, so many code just to do this :(((
   }

   if (!ctCollection) {
      ::Error("CreateFontCollection", "No fonts are available for family %s", xlfd.fFamilyName.c_str());//WTF???
      return 0;
   }


   return ctCollection;
}

//______________________________________________________________________________
bool GetFamilyName(CTFontDescriptorRef fontDescriptor, std::vector<char> &name)
{
   assert(fontDescriptor != 0 && "GetFamilyName, fontDescriptor parameter is null");

   name.clear();

   Util::CFScopeGuard<CFStringRef> cfFamilyName((CFStringRef)CTFontDescriptorCopyAttribute(fontDescriptor, kCTFontFamilyNameAttribute));
   const CFIndex cfLen = CFStringGetLength(cfFamilyName.Get());
   name.resize(cfLen + 1);//+ 1 for '\0'.
   if (CFStringGetCString(cfFamilyName.Get(), &name[0], name.size(), kCFStringEncodingMacRoman))
      return true;

   return false;
}

//______________________________________________________________________________
void GetWeightAndSlant(CTFontDescriptorRef fontDescriptor, X11::XLFDName &newXLFD)
{
   //Let's ask for a weight and pixel size.
   const Util::CFScopeGuard<CFDictionaryRef> traits((CFDictionaryRef)CTFontDescriptorCopyAttribute(fontDescriptor, kCTFontTraitsAttribute));
   if (traits.Get()) {
      if(CFNumberRef weight = (CFNumberRef)CFDictionaryGetValue(traits.Get(), kCTFontWeightTrait)) {
         double val = 0.;
         if (CFNumberGetValue(weight, kCFNumberDoubleType, &val))
            newXLFD.fWeight = val > 0. ? X11::kFWBold : X11::kFWMedium;
      }

      if(CFNumberRef slant = (CFNumberRef)CFDictionaryGetValue(traits.Get(), kCTFontSlantTrait)) {
         double val = 0.;
         if (CFNumberGetValue(slant, kCFNumberDoubleType, &val))
            newXLFD.fSlant = val > 0. ? X11::kFSItalic : X11::kFSRegular;
      }
   }
}

//______________________________________________________________________________
void GetPixelSize(CTFontDescriptorRef fontDescriptor, X11::XLFDName &newXLFD)
{
   const Util::CFScopeGuard<CFNumberRef> size((CFNumberRef)CTFontDescriptorCopyAttribute(fontDescriptor, kCTFontSizeAttribute));
   if (size.Get()) {
      int pixelSize = 0;
      if(CFNumberIsFloatType(size.Get())) {
         double val = 0;
         CFNumberGetValue(size.Get(), kCFNumberDoubleType, &val);
         pixelSize = val;
      } else
         CFNumberGetValue(size.Get(), kCFNumberIntType, &pixelSize);

      if(pixelSize)
         newXLFD.fPixelSize = pixelSize;
   }
}

//_________________________________________________________________
void CreateXLFDString(const X11::XLFDName &xlfd, std::string &xlfdString)
{
    xlfdString = "-*-"; //skip the foundry.
    xlfdString += xlfd.fFamilyName;

    if (xlfd.fWeight == X11::kFWBold)
        xlfdString += "-bold";
    else
        xlfdString += "-*";

    if (xlfd.fSlant == X11::kFSItalic)
        xlfdString += "-i";
    else
        xlfdString += "-r";

    xlfdString += "-*-*"; //width, addstyle

    if (xlfd.fPixelSize) {
        std::ostringstream out;
        out<<xlfd.fPixelSize;
        xlfdString += "-";
        xlfdString += out.str();
    } else
        xlfdString += "-*";

    xlfdString += "-*-*-*-*-*-*-*-";//TODO: something more reasonable?
}

}

//_________________________________________________________________
FontCache::FontCache()
{
}

//______________________________________________________________________________
FontStruct_t FontCache::LoadFont(const X11::XLFDName &xlfd)
{
   using Util::CFScopeGuard;
   using Util::CFStrongReference;
   
   const CFScopeGuard<CFStringRef> fontName(CFStringCreateWithCString(kCFAllocatorDefault, xlfd.fFamilyName.c_str(), kCFStringEncodingMacRoman));
   const CFStrongReference<CTFontRef> baseFont(CTFontCreateWithName(fontName.Get(), xlfd.fPixelSize, 0), false);//false == do not retain
   
   if (!baseFont.Get()) {
      ::Error("FontCache::LoadFont", "CTFontCreateWithName failed for %s", xlfd.fFamilyName.c_str());
      return FontStruct_t();//Haha! Die ROOT, die!
   }
   
   CTFontSymbolicTraits symbolicTraits = CTFontSymbolicTraits();
   
   if (xlfd.fWeight == X11::kFWBold)
      symbolicTraits |= kCTFontBoldTrait;
   if (xlfd.fSlant == X11::kFSItalic)
      symbolicTraits |= kCTFontItalicTrait;
      
   if (symbolicTraits) {
      const CFStrongReference<CTFontRef> font(CTFontCreateCopyWithSymbolicTraits(baseFont.Get(), xlfd.fPixelSize, 0, symbolicTraits, symbolicTraits), false);//false == do not retain.
      if (font.Get()) {
         if (fLoadedFonts.find(font.Get()) == fLoadedFonts.end())
            fLoadedFonts[font.Get()] = font;
      
         return reinterpret_cast<FontStruct_t>(font.Get());
      }
   }
      
   if (fLoadedFonts.find(baseFont.Get()) == fLoadedFonts.end())
      fLoadedFonts[baseFont.Get()] = baseFont;

   return reinterpret_cast<FontStruct_t>(baseFont.Get());   
}

//______________________________________________________________________________
void FontCache::UnloadFont(FontStruct_t font)
{
   CTFontRef fontRef = (CTFontRef)font;
   font_iterator fontIter = fLoadedFonts.find(fontRef);

   assert(fontIter != fLoadedFonts.end() && "Attempt to unload font, not created by font manager");

   fLoadedFonts.erase(fontIter);
}

//______________________________________________________________________________
char **FontCache::ListFonts(const X11::XLFDName &xlfd, int maxNames, int &count)
{
   typedef std::vector<char>::size_type size_type;

   count =  0;

   //Ugly, ugly code. I should "think different"!!!   
   //To extract font names, I have to: create CFString, create font descriptor, create
   //CFArray, create CTFontCollection, that's a mess!!!
   //It's good I have my small and ugly RAII classes, otherwise the code will be
   //total trash and sodomy because of all possible cleanup actions.

   //First, create a font collection.
   const Util::CFScopeGuard<CTFontCollectionRef> collectionGuard(CreateFontCollection(xlfd));
   if (!collectionGuard.Get())
      return 0;

   Util::CFScopeGuard<CFArrayRef> fonts(CTFontCollectionCreateMatchingFontDescriptors(collectionGuard.Get()));
   if (!fonts.Get()) {
      ::Error("FontCache::ListFonts", "CTFontCollectionCreateMatchingFontDescriptors failed %s", xlfd.fFamilyName.c_str());
      return 0;
   }
   
   std::vector<char> xlfdData;
   std::vector<char> familyName;
   X11::XLFDName newXLFD = {};
   std::string xlfdString;
   
   const CFIndex nFonts = CFArrayGetCount(fonts.Get());
   for (CFIndex i = 0; i < nFonts && count < maxNames; ++i) {
      CTFontDescriptorRef font = (CTFontDescriptorRef)CFArrayGetValueAtIndex(fonts.Get(), i);

      if (!GetFamilyName(font, familyName))
         continue;
      //I do not check family name: if xlfd.fFamilyName is '*', all font names fit,
      //if it's a special name - collection is created using this name.
      newXLFD.fFamilyName = &familyName[0];

      GetWeightAndSlant(font, newXLFD);
      //Check weight and slant.
      if (newXLFD.fWeight != xlfd.fWeight)
         continue;
      if (newXLFD.fSlant != xlfd.fSlant)
         continue;

      if (xlfd.fPixelSize) {//Size was requested.
         GetPixelSize(font, newXLFD);
         //I do not think, that font has a pixel size.
         //But Core Text supports different font sizes.
         if (!newXLFD.fPixelSize)
            newXLFD.fPixelSize = xlfd.fPixelSize;
      }

      //Ok, now lets create XLFD name, and place into list.
      CreateXLFDString(newXLFD, xlfdString);
      //
      xlfdData.insert(xlfdData.end(), xlfdString.begin(), xlfdString.end());
      xlfdData.push_back(0);//terminal 0.
      ++count;
   }

   //Setup array with string addresses.
   if (xlfdData.size()) {
      fFontLists.push_back(fDummyList);
      fFontLists.back().fStringData.swap(xlfdData);
      
      std::vector<char> &data = fFontLists.back().fStringData;
      std::vector<char *> &list = fFontLists.back().fList;
      
      list.push_back(&data[0]);
      for (size_type i = 1, e = data.size(); i < e; ++i) {
         if (!data[i] && i + 1 < e)
            list.push_back(&data[i + 1]);
      }

      return &list[0];
   } else
      return 0;
}

//______________________________________________________________________________
void FontCache::FreeFontNames(char **fontList)
{
   if (!fontList)
      return;
   
   for (std::list<FontList>::iterator it = fFontLists.begin(), eIt = fFontLists.end(); it != eIt; ++it) {
      if (fontList == &it->fList[0]) {
         fFontLists.erase(it);
         return;
      }
   }
   
   assert(0 && "FreeFontNames, unknown fontList");
}

//______________________________________________________________________________
unsigned FontCache::GetTextWidth(FontStruct_t font, const char *text, int nChars)
{
   typedef std::vector<CGSize>::size_type size_type;
   //
   CTFontRef fontRef = (CTFontRef)font;
   assert(fLoadedFonts.find(fontRef) != fLoadedFonts.end() && "Font was not created by font manager");

   //nChars is either positive, or negative (take all string).
   if (nChars < 0)
      nChars = std::strlen(text);

   std::vector<UniChar> unichars(text, text + nChars);

   //Extract glyphs for a text.
   std::vector<CGGlyph> glyphs(unichars.size());
   CTFontGetGlyphsForCharacters(fontRef, &unichars[0], &glyphs[0], unichars.size());

   //Glyps' advances for a text.
   std::vector<CGSize> glyphAdvances(glyphs.size());
   CTFontGetAdvancesForGlyphs(fontRef, kCTFontHorizontalOrientation, &glyphs[0], &glyphAdvances[0], glyphs.size());
   
   CGFloat textWidth = 0.;
   for (size_type i = 0, e = glyphAdvances.size(); i < e; ++i)
      textWidth += std::ceil(glyphAdvances[i].width);
      
   return textWidth;
}


//_________________________________________________________________
void FontCache::GetFontProperties(FontStruct_t font, int &maxAscent, int &maxDescent)
{
   CTFontRef fontRef = (CTFontRef)font;

   assert(fLoadedFonts.find(fontRef) != fLoadedFonts.end() && "Font was not created by font manager");

   //Instead of this, use CT funtion to request ascent/descent.
   try {
      const Quartz::TextLine textLine("LALALA", fontRef);
      textLine.GetAscentDescent(maxAscent, maxDescent);
      maxAscent += 1;
      maxDescent += 1;
   } catch (const std::exception &) {
      throw;
   }
}


//_________________________________________________________________
CTFontRef FontCache::SelectFont(Font_t fontIndex, Float_t fontSize)
{
   fontIndex /= 10;

   if (fontIndex > nPadFonts || !fontIndex) {
      ::Warning("FontCache::SelectFont", "Font with index %d was requested", fontIndex);
      fontIndex = 1;
   }
   
   fontIndex -= 1;
   
   if (fontIndex == 11)//Special case, our own symbol.ttf file.
      return SelectSymbolFont(fontSize);
   
   const UInt_t fixedSize = UInt_t(fontSize);
   font_map_iterator it = fFonts[fontIndex].find(fixedSize);
   
   if (it == fFonts[fontIndex].end()) {
      //Insert the new font.
      try {
         const CTFontGuard_t font(CTFontCreateWithName(fixedFontNames[fontIndex], fixedSize, 0), false);
         if (!font.Get()) {//With Apple's lame documentation it's not clear, if function can return 0.
            ::Error("FontCache::SelectFont", "CTFontCreateWithName failed for font %d", fontIndex);
            return 0;
         }
    
         fFonts[fontIndex][fixedSize] = font;//Insetion can throw.
         return fSelectedFont = font.Get();
      } catch (const std::exception &) {//Bad alloc.
         return 0;
      }
   }

   return fSelectedFont = it->second.Get();
}

//_________________________________________________________________
CTFontRef FontCache::SelectSymbolFont(Float_t fontSize)
{
   const UInt_t fixedSize = UInt_t(fontSize);
   font_map_iterator it = fFonts[11].find(fixedSize);//In ROOT, 11 is a font from symbol.ttf.
   
   if (it == fFonts[11].end()) {
      //This GetValue + Which I took from Olivier's code.
      const char *fontDirectoryPath = gEnv->GetValue("Root.TTFontPath","$(ROOTSYS)/fonts");//This one I do not own.
      char *fontFileName = gSystem->Which(fontDirectoryPath, "symbol.ttf", kReadPermission);//This must be deleted.

      if (!fontFileName || fontFileName[0] == 0) {
         ::Error("FontCache::SelectSymbolFont", "sumbol.ttf file not found");
         delete [] fontFileName;
         return 0;
      }

      try {
         const Util::CFScopeGuard<CFStringRef> path(CFStringCreateWithCString(kCFAllocatorDefault, fontFileName, kCFURLPOSIXPathStyle));
         if (!path.Get()) {
            ::Error("FontCache::SelectSymbolFont", "CFStringCreateWithCString failed");
            delete [] fontFileName;
            return 0;
         }
         
         const Util::CFScopeGuard<CFArrayRef> arr(CTFontManagerCreateFontDescriptorsFromURL(CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path.Get(), kCFURLPOSIXPathStyle, false)));
         if (!arr.Get()) {
            ::Error("FontCache::SelectSymbolFont", "CTFontManagerCreateFontDescriptorsFromURL failed");
            delete [] fontFileName;
            return 0;
         }

         CTFontDescriptorRef fontDesc = (CTFontDescriptorRef)CFArrayGetValueAtIndex(arr.Get(), 0);
         const CTFontGuard_t font(CTFontCreateWithFontDescriptor(fontDesc, fixedSize, 0), false);
         if (!font.Get()) {
            ::Error("FontCache::SelectSymbolFont", "CTFontCreateWithFontDescriptor failed");
            delete [] fontFileName;
            return 0;
         }

         delete [] fontFileName;

         fFonts[11][fixedSize] = font;//This can throw.
         return fSelectedFont = font.Get();
      } catch (const std::exception &) {//Bad alloc.
         //RAII destructors should do their work.
         return 0;
      }
   }

   return fSelectedFont = it->second.Get();
}


//_________________________________________________________________   
void FontCache::GetTextBounds(UInt_t &w, UInt_t &h, const char *text)const
{
   assert(fSelectedFont != 0 && "GetTextBounds: no font was selected");
   
   try {
      const Quartz::TextLine ctLine(text, fSelectedFont);
      ctLine.GetBounds(w, h);
      h += 2;
   } catch (const std::exception &) {
      throw;
   }
}


//_________________________________________________________________
double FontCache::GetAscent()const
{
   assert(fSelectedFont != 0 && "GetAscent, no font was selected");
   return CTFontGetAscent(fSelectedFont) + 1;
}


//_________________________________________________________________
double FontCache::GetDescent()const
{
   assert(fSelectedFont != 0 && "GetDescent, no font was selected");
   return CTFontGetDescent(fSelectedFont) + 1;
}

//_________________________________________________________________
double FontCache::GetLeading()const
{
   assert(fSelectedFont != 0 && "GetLeading, no font was selected");
   return CTFontGetLeading(fSelectedFont);
}


}//Details
}//MacOSX
}//ROOT
