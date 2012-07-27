# Module.mk for utils module
# Copyright (c) 2000 Rene Brun and Fons Rademakers
#
# Author: Fons Rademakers, 29/2/2000

# see also ModuleVars.mk

MODNAME := utils

ifneq ($(HOST),)

.PHONY: all-$(MODNAME) clean-$(MODNAME) distclean-$(MODNAME)

all-$(MODNAME):

clean-$(MODNAME):

distclean-$(MODNAME):

else # ifneq ($(HOST),)

.PHONY: all-$(MODNAME) clean-$(MODNAME) distclean-$(MODNAME)

ifeq ($(BUILDCLING),yes)
.SECONDARY: $(ROOTCLINGTMPS)
endif

$(ROOTCINTEXE): $(CINTLIB) $(ROOTCINTO) $(METAUTILSO) $(SNPRINTFO) \
	   $(STRLCPYO) $(IOSENUM)
	$(LD) $(LDFLAGS) -o $@ $(ROOTCINTO) $(METAUTILSO) \
	   $(SNPRINTFO) $(STRLCPYO) $(RPATH) $(CINTLIBS) $(CILIBS)

$(ROOTCINTTMPEXE): $(CINTTMPO) $(ROOTCINTTMPO) $(METAUTILSO) $(SNPRINTFO) \
	   $(STRLCPYO) $(IOSENUM)
	$(LD) $(LDFLAGS) -o $@ \
	   $(ROOTCINTTMPO) $(METAUTILSO) $(SNPRINTFO) $(STRLCPYO) \
	   $(CINTTMPO) $(CINTTMPLIBS) $(CILIBS)

$(ROOTCLINGEXE): $(CINTLIB) $(ROOTCLINGO) $(ROOTCLINGUTILO) \
	   $(METAUTILSO) $(METAUTILSTO) $(SNPRINTFO) $(STRLCPYO) $(IOSENUM) \
	   $(CLINGLIB)
	$(LD) $(LDFLAGS) -o $@ $(ROOTCLINGO) $(ROOTCLINGUTILO) \
	   $(METAUTILSO) $(METAUTILSTO) $(SNPRINTFO) $(STRLCPYO) $(RPATH) \
	   -Llib -lCling $(CINTLIBS) $(CILIBS)

$(ROOTCLINGTMPEXE): $(CINTTMPO) $(ROOTCLINGTMPO) $(ROOTCLINGUTILO) \
	   $(METAUTILSO) $(METAUTILSTO) $(SNPRINTFO) $(STRLCPYO) $(IOSENUM) \
	   $(CLINGLIB)
	$(LD) $(LDFLAGS) -o $@ $(ROOTCLINGTMPO) $(ROOTCLINGUTILO) \
	   $(METAUTILSO) $(METAUTILSTO) $(SNPRINTFO) $(STRLCPYO) $(CINTTMPO) \
	   $(CINTTMPLIBS) -Llib -lCling $(CILIBS)

ifneq ($(PLATFORM),win32)
$(RLIBMAP): $(RLIBMAPO)
	$(LD) $(LDFLAGS) -o $@ $<
else
$(RLIBMAP): $(RLIBMAPO)
	$(LD) $(LDFLAGS) -o $@ $< imagehlp.lib
endif

all-$(MODNAME): $(ROOTCINTTMPEXE) $(ROOTCINTEXE) $(ROOTCLINGTMPEXE) \
	   $(ROOTCLINGEXE) $(RLIBMAP)

clean-$(MODNAME):
	@rm -f $(ROOTCINTTMPO) $(ROOTCINTO) $(ROOTCLINGTMPO) $(ROOTCLINGO) \
	   $(RLIBMAPO)

clean:: clean-$(MODNAME)

distclean-$(MODNAME): clean-$(MODNAME)
	@rm -f $(ROOTCINTDEP) $(ROOTCINTTMPEXE) $(ROOTCINTEXE) \
	   $(ROOTCLINGDEP) $(ROOTCLINGTMPEXE) $(ROOTCLINGEXE) \
	   $(RLIBMAPDEP) $(RLIBMAP) \
	   $(call stripsrc,$(UTILSDIRS)/*.exp $(UTILSDIRS)/*.lib \
	      $(UTILSDIRS)/*_tmp.cxx)

distclean:: distclean-$(MODNAME)

##### extra rules ######
$(call stripsrc,$(UTILSDIRS)/%_tmp.cxx): $(UTILSDIRS)/%.cxx
	$(MAKEDIR)
	cp $< $@

$(call stripsrc,$(UTILSDIRS)/rootcint_tmp.o): $(call stripsrc,\
	   $(UTILSDIRS)/rootcint_tmp.cxx)

$(call stripsrc,$(UTILSDIRS)/rootcling_tmp.o): $(call stripsrc,\
	   $(UTILSDIRS)/rootcling_tmp.cxx)

$(call stripsrc,$(UTILSDIRS)/RStl_tmp.o): $(call stripsrc,\
	   $(UTILSDIRS)/RStl_tmp.cxx)

$(ROOTCINTTMPO): CXXFLAGS += -UR__HAVE_CONFIG -DROOTBUILD -I$(UTILSDIRS)
$(ROOTCLINGTMPO): CXXFLAGS += -UR__HAVE_CONFIG -DROOTBUILD -I$(UTILSDIRS) \
	   $(ROOTCLINGCXXFLAGS)
$(ROOTCLINGO): CXXFLAGS += -UR__HAVE_CONFIG -I$(UTILSDIRS) $(ROOTCLINGCXXFLAGS)
$(ROOTCLINGUTILO): CXXFLAGS += -UR__HAVE_CONFIG -I$(UTILSDIRS) \
	   $(ROOTCLINGCXXFLAGS)

endif # ifneq ($(HOST),)
