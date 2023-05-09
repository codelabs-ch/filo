# SPDX-License-Identifier: GPL-2.0-or-later

KBUILD_KCONFIG ?= Kconfig

export LC_ALL=C
export LANG=C

################################################################################
# Include original Makefile, with just enough edits to work for us
$(objk)/Makefile.real: $(dir $(lastword $(MAKEFILE_LIST)))Makefile
	mkdir -p $(objk)
	sed \
		-e "s,\$$(obj),\$$(objk),g" \
		-e "s,\$$(src),\$$(srck),g" \
		-e "s,^help:,help_kconfig help::," \
		-e "s,^%.config:,__disable__&," \
		-e "s,\$$(srctree)/arch/\$$(SRCARCH)/configs/\$$(KBUILD_DEFCONFIG),\$$(KBUILD_DEFCONFIG)," \
		-e "s,--defconfig=arch/\$$(SRCARCH)/configs/\$$(KBUILD_DEFCONFIG),--defconfig=\$$(KBUILD_DEFCONFIG)," \
		-e "/^unexport CONFIG_$$/d" \
		$< > $@.tmp
	mv $@.tmp $@

kecho := echo
HOSTPKG_CONFIG ?= pkg-config
export HOSTPKG_CONFIG

-include $(objk)/Makefile.real
unexport KCONFIG_DEFCONFIG_LIST

################################################################################
# Fill in Linux kconfig build rules to work (we need FORCE: and $(filechk))
# (copies from toplevel Makefile and scripts/Kbuild.include)

FORCE:

###
# Name of target with a '.' as filename prefix. foo/bar.o => foo/.bar.o
dot-target = $(dir $@).$(notdir $@)

###
# filechk is used to check if the content of a generated file is updated.
# Sample usage:
#
# filechk_sample = echo $(KERNELRELEASE)
# version.h: FORCE
#	$(call filechk,sample)
#
# The rule defined shall write to stdout the content of the new file.
# The existing file will be compared with the new one.
# - If no file exist it is created
# - If the content differ the new file is used
# - If they are equal no change, and no timestamp update
define filechk
	$(check-FORCE)
	$(Q)set -e;						\
	mkdir -p $(dir $@);					\
	trap "rm -f $(dot-target).tmp" EXIT;			\
	{ $(filechk_$(1)); } > $(dot-target).tmp;		\
	if [ ! -r $@ ] || ! cmp -s $@ $(dot-target).tmp; then	\
		$(kecho) '  UPD     $@';			\
		mv -f $(dot-target).tmp $@;			\
	fi
endef

# It is a typical mistake to forget the FORCE prerequisite. Check it here so
# no more breakage will slip in.
check-FORCE = $(if $(filter FORCE, $^),,$(warning FORCE prerequisite is missing))

################################################################################
# Our adapted build rules

$(objk)/%.o: $(srck)/%.c
	$(HOSTCC) -I $(srck) -I $(objk) -c $(HOSTCFLAGS_$(notdir $@)) -o $@ $<

$(objk)/%.o: $(srck)/%.cc
	$(HOSTCXX) -I $(srck) -I $(objk) -c $(HOSTCXXFLAGS_$(notdir $@)) -o $@ $<

$(objk)/%.o: $(objk)/%.c
	$(HOSTCC) -I $(srck) -I $(objk) -c -o $@ $<

$(objk)/%.moc: $(srck)/%.h | $(objk)/qconf-cfg
	$(call cmd_moc)

define hostprogs_template
# $1 entry in hostprogs
$(objk)/$(1): $$(foreach _o,$$($(1)-objs) $$($(1)-cxxobjs),$(objk)/$$(_o)) | $(wildcard $(objk)/$(1)-cfg)
	$$(HOSTCXX) -o $$@ $$^ $$(HOSTLDLIBS_$(1))
endef

$(foreach prog,$(hostprogs),$(eval $(call hostprogs_template,$(prog))))

# This might be a bit of a chicken & egg problem, using a kconfig flag when
# building kconfig, but if you're messing with the parser you probably know
# what you're doing: make CONFIG_UTIL_GENPARSER=y
ifeq ($(CONFIG_UTIL_GENPARSER),y)
$(objk)/%.tab.c $(objk)/%.tab.h: $(srck)/%.y
	bison -t -l --defines -b $(objk)/$* $<

$(objk)/%.lex.c: $(srck)/%.l
	flex -L -o$@ $<

else # !CONFIG_UTIL_GENPARSER

$(objk)/parser.tab.c: | $(objk)/parser.tab.h

$(objk)/%: $(srck)/%_shipped
	cp $< $@

endif

################################################################################
# Libpayload integration

LIB_CONFIG  := $(src)/lib.config

LIBPAYLOAD ?= $(wildcard $(LIBPAYLOAD_PREFIX)/lib/libpayload.a)
ifeq ($(LIBPAYLOAD),)
libpayload_config_%:
	echo "Libpayload config for FILO."
	$(MAKE) -C $(LIBCONFIG_PATH) KCONFIG_CONFIG=$(LIB_CONFIG) $*
	echo "Libpayload config done."
else
libpayload_config_%:
	echo "Using binary libpayload, nothing to configure"
endif

$(foreach c, config menuconfig nconfig gconfig xconfig defconfig $(simple-targets), \
	$(eval $(c): libpayload_config_$(c)))
