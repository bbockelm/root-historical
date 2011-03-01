# Module.mk for zip module
# Copyright (c) 2000 Rene Brun and Fons Rademakers
#
# Author: Fons Rademakers, 29/2/2000

MODNAME      := zip
MODDIR       := $(ROOT_SRCDIR)/core/$(MODNAME)
MODDIRS      := $(MODDIR)/src
MODDIRI      := $(MODDIR)/inc

ZIPDIR       := $(MODDIR)
ZIPDIRS      := $(ZIPDIR)/src
ZIPDIRI      := $(ZIPDIR)/inc

##### libZip (part of libCore) #####
ZIPOLDH      := $(MODDIRI)/Bits.h 	\
		$(MODDIRI)/Tailor.h 	\
		$(MODDIRI)/ZDeflate.h	\
		$(MODDIRI)/ZIP.h	\
		$(MODDIRI)/ZTrees.h
ZIPOLDS      := $(MODDIRS)/ZDeflate.c	\
		$(MODDIRS)/ZInflate.c
ZIPNEWH	     := $(MODDIRI)/crc32.h 	\
		$(MODDIRI)/deflate.h 	\
		$(MODDIRI)/inffast.h 	\
		$(MODDIRI)/inffixed.h 	\
		$(MODDIRI)/inflate.h	\
		$(MODDIRI)/inftrees.h 	\
		$(MODDIRI)/trees.h 	\
		$(MODDIRI)/zconf.h 	\
		$(MODDIRI)/zlib.h 	\
		$(MODDIRI)/zutil.h
ZIPNEWS	     := $(MODDIRS)/adler32.c 	\
		$(MODDIRS)/compress.c 	\
		$(MODDIRS)/crc32.c 	\
		$(MODDIRS)/deflate.c 	\
		$(MODDIRS)/gzio.c 	\
		$(MODDIRS)/infback.c	\
		$(MODDIRS)/inffast.c 	\
		$(MODDIRS)/inflate.c 	\
		$(MODDIRS)/inftrees.c 	\
		$(MODDIRS)/trees.c 	\
		$(MODDIRS)/uncompr.c 	\
		$(MODDIRS)/zutil.c
ifeq ($(BUILTINZLIB),yes)
ZIPH	     := $(ZIPOLDH) $(ZIPNEWH)
ZIPS	     := $(ZIPOLDS) $(ZIPNEWS)
else
ZIPH	     := $(ZIPOLDH)
ZIPS	     := $(ZIPOLDS)
endif
ZIPO         := $(call stripsrc,$(ZIPS:.c=.o))
ZIPDEP       := $(ZIPO:.o=.d)

# used in the main Makefile
ALLHDRS     += $(patsubst $(MODDIRI)/%.h,include/%.h,$(ZIPH))

# include all dependency files
INCLUDEFILES += $(ZIPDEP)

##### local rules #####
.PHONY:         all-$(MODNAME) clean-$(MODNAME) distclean-$(MODNAME)

include/%.h:    $(ZIPDIRI)/%.h
		cp $< $@

all-$(MODNAME): $(ZIPO)

clean-$(MODNAME):
		@rm -f $(ZIPO)

clean::         clean-$(MODNAME)

distclean-$(MODNAME): clean-$(MODNAME)
		@rm -f $(ZIPDEP)

distclean::     distclean-$(MODNAME)
