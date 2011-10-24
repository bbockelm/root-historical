# Module.mk for proofd module
# Copyright (c) 2000 Rene Brun and Fons Rademakers
#
# Author: Fons Rademakers, 29/2/2000

MODNAME      := proofd
MODDIR       := $(ROOT_SRCDIR)/proof/$(MODNAME)
MODDIRS      := $(MODDIR)/src
MODDIRI      := $(MODDIR)/inc

PROOFDDIR    := $(MODDIR)
PROOFDDIRS   := $(PROOFDDIR)/src
PROOFDDIRI   := $(PROOFDDIR)/inc

# When using an external XROOTD distribution XROOTDDIRI and XROOTDDIRL
# are undefined and have to point to the specified inc and lib dirs.
ifneq ($(XRDINCDIR),)
ifeq ($(XROOTDDIRI),)
XROOTDDIRI   := $(XRDINCDIR)
endif
endif
ifneq ($(XRDLIBDIR),)
ifeq ($(XROOTDDIRL),)
XROOTDDIRL   := $(XRDLIBDIR)
endif
endif

ifeq ($(PLATFORM),win32)

##### XrdProofd plugin ####
XPDH         := $(wildcard $(MODDIRI)/X*.h)
XPDS         := $(MODDIRS)/XProofProtUtils.cxx
XPDO         := $(call stripsrc,$(XPDS:.cxx=.o))

##### Object files used by libProofx #####
XPCONNH      := $(MODDIRI)/XrdProofConn.h $(MODDIRI)/XrdProofPhyConn.h \
                $(MODDIRI)/XProofProtUtils.h

XPCONNS      := $(MODDIRS)/XrdProofConn.cxx $(MODDIRS)/XrdProofPhyConn.cxx \
                $(MODDIRS)/XProofProtUtils.cxx

XPCONNO      := $(call stripsrc,$(XPCONNS:.cxx=.o))

XPDDEP       := $(XPCONNO:.o=.d)

XPDLIB       := $(LPATH)/libXrdProofd.$(SOEXT)

# Extra include paths and libs
XPDINCEXTRA  := $(XROOTDDIRI:%=-I%)
XPDINCEXTRA  += $(PROOFDDIRI:%=-I%)

XPDLIBEXTRA  := $(XROOTDDIRL)/libXrdClient.lib

# used in the main Makefile
PROOFDEXEH   := $(MODDIRI)/proofdp.h
ALLHDRS      += $(patsubst $(MODDIRI)/%.h,include/%.h,$(PROOFDEXEH))
ALLLIBS      += $(XPDLIB)

# include all dependency files
INCLUDEFILES += $(XPDDEP)

##### local rules #####
.PHONY:         all-$(MODNAME) clean-$(MODNAME) distclean-$(MODNAME)

include/%.h:    $(PROOFDDIRI)/%.h
		cp $< $@

$(XPDLIB):      $(XPCONNO) $(XPCONNH) $(XRDPLUGINS) $(ORDER_) $(MAINLIBS)
		@$(MAKELIB) $(PLATFORM) $(LD) "$(LDFLAGS)" \
		   "$(SOFLAGS)" libXrdProofd.$(SOEXT) $@ "$(XPDO)" \
		   "$(XPDLIBEXTRA)"

all-$(MODNAME): $(XPDLIB)

clean-$(MODNAME):
		@rm -f $(XPCONNO)

clean::         clean-$(MODNAME)

distclean-$(MODNAME): clean-$(MODNAME)
		@rm -f $(XPDDEP) $(XPDLIB)

distclean::     distclean-$(MODNAME)

##### extra rules ######
$(XPCONNO): CXXFLAGS += $(XPDINCEXTRA) $(EXTRA_XRDFLAGS)

else

##### proofd #####
PROOFDEXEH   := $(MODDIRI)/proofdp.h
PROOFDEXES   := $(MODDIRS)/proofd.cxx
PROOFDEXEO   := $(call stripsrc,$(PROOFDEXES:.cxx=.o))
PROOFDDEP    := $(PROOFDEXEO:.o=.d)
PROOFDEXE    := bin/proofd

##### XrdProofd plugin ####
XPDH         := $(wildcard $(MODDIRI)/X*.h)
XPDS         := $(wildcard $(MODDIRS)/X*.cxx)
XPDO         := $(call stripsrc,$(XPDS:.cxx=.o))

XPDDEP       := $(XPDO:.o=.d)

XPDLIB       := $(LPATH)/libXrdProofd.$(SOEXT)

##### proofexecv #####
PROOFEXECVS   := $(MODDIRS)/proofexecv.cxx
PROOFEXECVO   := $(call stripsrc,$(PROOFEXECVS:.cxx=.o))
PROOFEXECVDEP := $(PROOFEXECVO:.o=.d)
PROOFEXECVEXE := bin/proofexecv
ifeq ($(PLATFORM),win32)
PROOFEXECVEXE :=
endif
ifeq ($(PROOFLIB),)
PROOFEXECVEXE :=
endif

##### Object files used by libProofx #####
XPCONNO      := $(call stripsrc,$(MODDIRS)/XrdProofConn.o \
                $(MODDIRS)/XrdProofPhyConn.o \
                $(MODDIRS)/XProofProtUtils.o)

# Extra include paths and libs
XPROOFDEXELIBS :=
XPROOFDEXESYSLIBS :=
XPROOFDEXE     :=
ifeq ($(HASXRD),yes)
XPDINCEXTRA    := $(XROOTDDIRI:%=-I%)
XPDINCEXTRA    += $(PROOFDDIRI:%=-I%)
ifeq ($(HASXRDUTILS),no)
XPDLIBEXTRA    := -L$(XROOTDDIRL) -lXrdClient -lXrdNet -lXrdOuc \
                  -lXrdSys -lXrdSut
XPROOFDEXELIBS := $(XROOTDDIRL)/libXrd.a $(XROOTDDIRL)/libXrdClient.a \
                  $(XROOTDDIRL)/libXrdNet.a $(XROOTDDIRL)/libXrdOuc.a \
                  $(XROOTDDIRL)/libXrdSys.a $(XROOTDDIRL)/libXrdSut.a
# Starting from Jul 2010 XrdNet has been split in two libs:
#    XrdNet and XrdNetUtil
# both are needed
XRDNETUTIL     :=
ifneq ($(XRDVERSION),)
XRDNETUTIL     := $(shell if test $(XRDVERSION) -gt 20100729; then \
                             echo "yes"; \
                          fi)
endif
ifeq ($(XRDNETUTIL),yes)
XPDLIBEXTRA    += -L$(XROOTDDIRL) -lXrdNetUtil
XPROOFDEXELIBS += $(XROOTDDIRL)/libXrdNetUtil.a
endif

else
XPDLIBEXTRA    := -L$(XROOTDDIRL) -lXrdClient -lXrdUtils
ifeq ($(PLATFORM),macosx)
XPROOFDEXELIBS := $(XROOTDDIRL)/libXrdMain.dylib $(XROOTDDIRL)/libXrdClient.dylib  $(XROOTDDIRL)/libXrdUtils.dylib
else
XPROOFDEXELIBS := $(XROOTDDIRL)/libXrdMain.$(SOEXT) $(XROOTDDIRL)/libXrdClient.$(SOEXT)  $(XROOTDDIRL)/libXrdUtils.$(SOEXT)
endif

endif
XPDLIBEXTRA    +=  $(DNSSDLIB)
XPROOFDEXELIBS +=  $(DNSSDLIB)

ifeq ($(PLATFORM),solaris)
XPROOFDEXESYSLIBS := -lsendfile
endif
XPROOFDEXE     := bin/xproofd
endif

# used in the main Makefile
ALLHDRS      += $(patsubst $(MODDIRI)/%.h,include/%.h,$(PROOFDEXEH))
ALLEXECS     += $(PROOFDEXE)
ifeq ($(HASXRD),yes)
ALLLIBS      += $(XPDLIB)
ALLEXECS     += $(XPROOFDEXE) $(PROOFEXECVEXE)
endif

# include all dependency files
ifeq ($(HASXRD),yes)
INCLUDEFILES += $(PROOFDDEP) $(XPDDEP) $(PROOFEXECVDEP)
else
INCLUDEFILES += $(PROOFDDEP)
endif

##### local rules #####
.PHONY:         all-$(MODNAME) clean-$(MODNAME) distclean-$(MODNAME)

include/%.h:    $(PROOFDDIRI)/%.h
		cp $< $@

$(PROOFDEXE):   $(PROOFDEXEO) $(RSAO) $(SNPRINTFO) $(GLBPATCHO) $(RPDUTILO) \
                $(STRLCPYO)
		$(LD) $(LDFLAGS) -o $@ $(PROOFDEXEO) $(RPDUTILO) $(GLBPATCHO) \
		   $(RSAO) $(SNPRINTFO) $(CRYPTLIBS) $(AUTHLIBS) $(STRLCPYO) \
		   $(SYSLIBS)

$(XPROOFDEXE):  $(XPDO) $(XPROOFDEXELIBS) $(XRDPROOFXD) $(RPDCONNO)
		$(LD) $(LDFLAGS) -o $@ $(XPDO) $(RPDCONNO) $(XPROOFDEXELIBS) \
		   $(SYSLIBS) $(XPROOFDEXESYSLIBS)

$(XPDLIB):      $(XPDO) $(XPDH) $(XPROOFDEXELIBS) $(ORDER_) $(MAINLIBS) \
                $(XRDPROOFXD) $(RPDCONNO)
		@$(MAKELIB) $(PLATFORM) $(LD) "$(LDFLAGS)" \
		   "$(SOFLAGS)" libXrdProofd.$(SOEXT) $@ "$(XPDO) $(RPDCONNO)" \
		   "$(XPDLIBEXTRA)"

$(PROOFEXECVEXE): $(PROOFEXECVO) $(RPDCONNO) $(RPDPRIVO)
		  $(LD) $(LDFLAGS) -o $@ $(PROOFEXECVO) $(RPDCONNO) \
		    $(RPDPRIVO) $(SYSLIBS)

all-$(MODNAME): $(PROOFDEXE) $(XPROOFDEXE) $(PROOFEXECVEXE) $(XPDLIB)

clean-$(MODNAME):
		@rm -f $(PROOFDEXEO) $(PROOFEXECVO) $(XPDO)

clean::         clean-$(MODNAME)

distclean-$(MODNAME): clean-$(MODNAME)
		@rm -f $(PROOFDDEP) $(PROOFDEXE) $(XPROOFDEXE) $(XPDDEP) \
		  $(PROOFEXECVEXE) $(PROOFEXECVDEP) $(XPDLIB)

distclean::     distclean-$(MODNAME)

##### extra rules ######
$(PROOFDEXEO): CXXFLAGS += $(AUTHFLAGS)

$(XPDO): $(XROOTDMAKE) $(XRDHDRS)
$(XPDO): CXXFLAGS += $(XPDINCEXTRA) $(EXTRA_XRDFLAGS) $(BONJOURCPPFLAGS)

ifneq ($(ICC_GE_9),)
# remove when xrootd has moved from strstream.h -> sstream.
$(XPDO): CXXFLAGS += -Wno-deprecated

else

ifneq ($(GCC_MAJOR),)
ifneq ($(GCC_MAJOR),2)
# remove when xrootd has moved from strstream.h -> sstream.
$(XPDO): CXXFLAGS += -Wno-deprecated
endif
endif

endif

# ifeq ($(PLATFORM),macosx)
# $(XPDLIB): SOFLAGS := -undefined dynamic_lookup $(SOFLAGS)
# endif
ifeq ($(PLATFORM),linux)
comma := ,
$(XPDLIB): LDFLAGS := $(subst -Wl$(comma)--no-undefined,,$(LDFLAGS))
endif

$(PROOFEXECVO): $(RPDCONNO) $(RPDPRIVO)

endif
