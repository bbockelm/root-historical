# ModuleVars.mk for utils module
# Copyright (c) 2008 Rene Brun and Fons Rademakers
#
# Author: Axel Naumann, 2008-06-10

ifneq ($(HOST),)

UTILSDIRS    := $(BUILDTOOLSDIR)/core/utils/src

ROOTCINTS    := $(UTILSDIRS)/rootcint.cxx \
                $(filter-out %_tmp.cxx,$(wildcard $(UTILSDIRS)/R*.cxx))
ROOTCINTTMPO := $(ROOTCINTS:.cxx=_tmp.o)
ROOTCINTTMPEXE := $(UTILSDIRS)/rootcint_tmp$(EXEEXT)
ROOTCINTTMP  ?= $(ROOTCINTTMPEXE) -$(ROOTDICTTYPE)

ifeq ($(BUILDCLING),yes)
ROOTCLINGS    := $(UTILSDIRS)/rootcling.cxx \
                 $(filter-out %root%.cxx,$(filter-out %_tmp.cxx,$(wildcard $(UTILSDIRS)/*.cxx)))
ROOTCLINGTMPO := $(ROOTCLINGS:.cxx=_tmp.o)
ROOTCLINGTMPEXE := $(UTILSDIRS)/rootcling_tmp$(EXEEXT)
ROOTCINTTMP  ?= $(ROOTCLINGTMPEXE) -$(ROOTDICTTYPE)
endif

##### Dependencies for all dictionaries
ifeq ($(BUILDCLING),yes)
ROOTCINTTMPDEP = $(ROOTCLINGTMPO) $(ORDER_) $(ROOTCLINGTMPEXE)
else
ROOTCINTTMPDEP = $(ROOTCINTTMPO) $(ORDER_) $(ROOTCINTTMPEXE)
endif

##### rlibmap #####
RLIBMAP      := $(BUILDTOOLSDIR)/bin/rlibmap$(EXEEXT)

else

MODNAME      := utils
MODDIR       := $(ROOT_SRCDIR)/core/$(MODNAME)
UTILSDIR     := $(MODDIR)
UTILSDIRS    := $(UTILSDIR)/src
UTILSDIRI    := $(UTILSDIR)/inc

##### rootcint #####
ROOTCINTS    := $(UTILSDIRS)/rootcint.cxx \
                $(filter-out %_tmp.cxx,$(wildcard $(UTILSDIRS)/R*.cxx))
ROOTCINTTMPO := $(call stripsrc,$(ROOTCINTS:.cxx=_tmp.o))

ROOTCINTTMPEXE := $(call stripsrc,$(UTILSDIRS)/rootcint_tmp$(EXEEXT))
ROOTCINTEXE  := bin/rootcint$(EXEEXT)
ROOTCINTTMP  ?= $(ROOTCINTTMPEXE) -$(ROOTDICTTYPE)

##### rootcint #####
ifeq ($(BUILDCLING),yes)
ROOTCLINGS    := $(UTILSDIRS)/rootcling.cxx \
                 $(filter-out %_tmp.cxx,$(filter-out %rlibmap.cxx,$(filter-out %rootcling.cxx,$(filter-out %rootcint.cxx,$(filter-out %_tmp.cxx,$(wildcard $(UTILSDIRS)/*.cxx))))))
ROOTCLINGTMPO := $(call stripsrc,$(ROOTCLINGS:.cxx=_tmp.o))

ROOTCLINGTMPEXE := $(call stripsrc,$(UTILSDIRS)/rootcling_tmp$(EXEEXT))
ROOTCLINGEXE  := bin/rootcling$(EXEEXT)
ROOTCLINGTMP  ?= $(ROOTCLINGTMPEXE) -$(ROOTDICTTYPE)
endif

##### Dependencies for all dictionaries
ifeq ($(BUILDCLING),yes)
ROOTCINTTMPDEP = $(ROOTCLINGTMPO) $(ORDER_) $(ROOTCLINGTMPEXE)
else
ROOTCINTTMPDEP = $(ROOTCINTTMPO) $(ORDER_) $(ROOTCINTTMPEXE)
endif

##### rlibmap #####
RLIBMAP      := bin/rlibmap$(EXEEXT)

ifeq ($(BUILDCLING),yes)
ROOTCLINGCXXFLAGS = $(filter-out -fno-exceptions,$(filter-out -fno-rtti,$(CLINGCXXFLAGS)))
ifneq ($(CXX:g++=),$(CXX))
ROOTCLINGCXXFLAGS += -Wno-shadow -Wno-unused-parameter
endif
else
ROOTCLINGCXXFLAGS := 
endif

endif
