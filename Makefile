CC = gcc
CFLAGS = -Wall -pedantic -std=c99 -D_GNU_SOURCE
DEBUG_CFLAGS = -ggdb -DDEBUG -ftrapv
# Add -DDBG to the KERN_DEBUG_CFLAGS to output extra debug information from IXGBE
KERN_DEBUG_CFLAGS = -DDEBUG -DDBG
KERN_DEBUG_ENVVARS = CONFIG_DEBUG_INFO=1
RELEASE_CFLAGS = -O3 -march=native
KERN_RELEASE_CFLAGS = -O3
KERN_RELEASE_ENVVARS =
LDFLAGS = -lhpcap -lnuma -lpcap -lpthread -lm -lmgmon
DEBUG_LDFLAGS = -Llib/debug
RELEASE_LDFLAGS = -Llib/release
LATEXFLAGS = -pdf -silent -synctex=1 -shell-escape

# Uncomment to build the driver for a specific kernel version
# BUILD_KERNEL=3.2.0-23-generic

# Directory variables
BINDIR = bin
OBJDIR = obj
DOCDIR = doc
LIBDIR = lib
KOBJDIR = $(OBJDIR)/kernel
LIBSRCDIR = srclib
SAMPLEDIR = samples
INCDIR = include
SCRIPTDIR = scripts
INCLUDEDIRS = . $(INCDIR)
DRIVDIR = driver
COMMON_DIR = $(DRIVDIR)/common
IXGBE_DIR = $(DRIVDIR)/hpcap_ixgbe-3.7.17_buffer/driver
IXGBEVF_DIR = $(DRIVDIR)/hpcap_ixgbevf-2.14.2/driver

PDF_DOCS = HPCAP_DevGuide.pdf HPCAP_UserGuide.pdf
PDF_PATHS = $(addprefix $(DOCDIR)/, $(PDF_DOCS))

DRIVERVARS = Makefile-driver-vars.mk
DRIVERVARS_KBUILD = $(abspath $(DRIVERVARS))

# Some automatic variables
INCLUDES := $(addprefix -I, $(addsuffix /, $(INCLUDEDIRS)))
INCLUDES_ABS := $(addprefix -I, $(abspath $(addsuffix /, $(INCLUDEDIRS))))

SAMPLE_DIRS := $(notdir $(wildcard $(SAMPLEDIR)/*))
SAMPLE_SRCS := $(wildcard $(SAMPLEDIR)/**/*.c)

# Avoid adding to our list of sample applications directories that do not comply
# with our expected directory structure. That is, we only include samples where the
# file $(SAMPLEDIR)/samplename/samplename.c exists.
SAMPLES := $(foreach sample, $(SAMPLE_DIRS), $(shell [ -z "$(wildcard $(SAMPLEDIR)/$(sample)/$(sample).c)" ] || echo $(sample)))

COMMON_SRCS := $(wildcard $(COMMON_DIR)/*.c)
COMMON_HDRS := $(wildcard $(COMMON_DIR)/*.h)
IXGBE_SRCS := $(wildcard $(IXGBE_DIR)/*.c)
IXGBE_HDRS := $(wildcard $(IXGBE_DIR)/*.h)
IXGBEVF_SRCS := $(wildcard $(IXGBEVF_DIR)/*.c)
IXGBEVF_HDRS := $(wildcard $(IXGBEVF_DIR)/*.h)
DRIV_NAMES := $(filter-out common, $(notdir $(wildcard $(DRIVDIR)/*)))

LIB_SRCS := $(wildcard $(LIBSRCDIR)/**/*.c)
LIB_OBJS := $(addprefix $(OBJDIR)/, $(patsubst %.c,%.o, $(LIB_SRCS)))
LIB_NAMES := $(notdir $(wildcard $(LIBSRCDIR)/*))
LIBS :=	$(addsuffix .a, $(LIB_NAMES))

ALL_SRCS = $(LIB_SRCS) $(SAMPLE_SRCS) include/*
ALL_SRCS += $(COMMON_SRCS) $(COMMON_HDRS) $(IXGBE_SRCS) $(IXGBE_HDRS) $(IXGBEVF_SRCS) $(IXGBEVF_HDRS)

# Formatting
FMT_BOLD := $(shell tput bold)
FMT_NORM := $(shell tput sgr0)

.PHONY: config dirs depend hpcap hpcapvf libs samples drivers version-info changes .FORCE driverclean

ALL_TARGETS = libs samples drivers

# Basic rules
all: $(ALL_TARGETS)
libs: $(LIB_NAMES)
samples: $(SAMPLES)

.FORCE:

# For debugging
print-%: ; @echo $*=$($*)

####################################
## Help							  ##
####################################
help:
	@echo "HPCAP Makefile help. Available targets: "
	@echo
	@echo "- all: build all targets (libraries, samples and drivers)"
	@echo "- $(ALL_TARGETS): build libraries, samples or drivers respectively."
	@echo "- $(CONFS): build all the targets in the given configuration."
	@echo "- $(addprefix drivers-, $(CONFS)): build the drivers for the given configuration."
	@echo "- clean: Clean the intermediate files and binaries."
	@echo "- docs: Build the documentation."
	@echo "- doxydoc: Build the doxygen documentation (included in the docs target)."
	@echo "- cscope: Build the cscope database."
	@echo "- install: Install the driver in this system. Accepts INSTALL_PATH=prefix to set the installation prefix."
	@echo "- install-libs: Install only the HPCAP libraries and headers. Useful for development."
	@echo "- uninstall: Uninstall the driver from the system."
	@echo "- pack: Pack the source code in a .tar.gz file in the current directory."
	@echo "- dist: Pack the source code and binaries in a ready-to-install package."
	@echo ""
	@echo "Apart from those generic rules, you can use specific rules, such as bin/[conf]/[binary]"
	@echo "or lib/[conf]/[library] to build just one file in one given configuration. Drivers are"
	@echo "stored in the bin directory. Also, 'make [object]' will build the given object in the default"
	@echo "configuration ($(DEFAULT_CONF)). E.g., 'make libhpcap' will build lib/$(DEFAULT_CONF)/libhpcap.a,"
	@echo "and 'make bin/debug/hpcapdd' will build hpcapdd with the debug configuration."
	@echo ""
	@echo "Files that can be built:"
	@echo "Drivers: $(DRIV_TARGETS)"
	@echo "Sample applications: $(SAMPLES)"
	@echo "Libraries: $(LIB_NAMES)"

####################################
## Build configuration management ##
####################################
CONFS = debug release
DEFAULT_CONF = release

SAMPLE_CONFS := $(addsuffix -conf.mk, $(addprefix $(OBJDIR)/.sample-, $(SAMPLES)))
SAMPLE_DEPS = $(addsuffix -deps.mk, $(addprefix $(OBJDIR)/.sample-, $(SAMPLES)))

LIB_CONFS := $(addsuffix -conf.mk, $(addprefix $(OBJDIR)/.lib-, $(LIB_NAMES)))
LIB_DEPS := $(addsuffix -deps.mk, $(addprefix $(OBJDIR)/.lib-, $(LIB_NAMES)))

DRIV_CONFS := $(addsuffix -conf.mk, $(addprefix $(OBJDIR)/.driver-, $(DRIV_NAMES)))
DRIV_OBJDIRS := $(foreach conf, $(CONFS), $(addprefix $(OBJDIR)/kernel/$(conf)/, $(DRIV_NAMES)))

LIB_OBJDIRS := $(foreach conf, $(CONFS), $(addprefix $(OBJDIR)/$(conf)/$(LIBSRCDIR)/, $(LIB_NAMES)))
LIB_OUTDIRS := $(addprefix $(LIBDIR)/, $(CONFS))
SAMPLE_OBJDIRS := $(foreach conf, $(CONFS), $(addprefix $(OBJDIR)/$(conf)/samples/, $(SAMPLES)))

ALL_CONFS = $(SAMPLE_CONFS) $(LIB_CONFS) $(DRIV_CONFS)
ALL_DEPS = $(SAMPLE_DEPS) $(LIB_DEPS)

config: $(ALL_CONFS)
depend: $(ALL_DEPS)

# If the kernel version is fixed, we have to add it to the kernel environment variables
ifneq (,$(BUILD_KERNEL))
KERN_DEBUG_ENVVARS += BUILD_KERNEL=$(BUILD_KERNEL)
KERN_RELEASE_ENVVARS += BUILD_KERNEL=$(BUILD_KERNEL)
endif

# This function generates a new file that will be automatically included in this Makefile,
# 	containing dynamically generated rules and flags for each configuration of a given target.
# Arguments:
# 	1: File where the new rules will be stored
# 	2: Root for the sources (e.g., SAMPLEDIR or LIBSRCDIR)
# 	3: Target directory (e.g., bin or lib)
# 	4: Target name
# 	5: Target
generate_config = \
	-rm -f $(1); \
	echo "SRCS_$(4) := \$$(wildcard $(2)/$(4)/*.c)" >> $(1); \
	for c in $(CONFS); do \
		cnf=$$(echo $$c | tr '[:lower:]' '[:upper:]'); \
		echo "OBJS_$${c}_$(4) := \$$(addprefix $(OBJDIR)/$$c/, \$$(patsubst %.c, %.o, \$$(SRCS_$(4))))" >> $(1); \
		echo "$(3)/$$c/$(5): CFLAGS += \$$(""$$cnf""_CFLAGS)" >> $(1); \
		echo "$(3)/$$c/$(5): LDFLAGS += \$$(""$$cnf""_LDFLAGS)" >> $(1); \
		echo "$(3)/$$c/$(5): \$$(OBJS_$${c}_$(4))" >> $(1); \
	done; \
	echo "$(4): $(3)/$(DEFAULT_CONF)/$(5)" >> $(1); \
	echo "" >> $(1); \
	for c in $(CONFS); do \
		echo "$$c: $(3)/$$c/$(5)" >> $(1); \
	done

# This function generates a new file that will be included automatically in this Makefile,
# 	containing dynamically generated dependencies for each configuration of a given target.
# Arguments:
#  	1: File where the new rules will be stored
# 	2: Root for the sources (e.g., SAMPLEDIR or LIBSRCDIR)
# 	3: Target directory (e.g., bin or lib)
# 	4: Source files
# 	5: Target name (e.g., libhpcap or hpcapdd)
# 	6: Final file name
generate_deps = \
	-rm -f $(1); \
	$(CC) $(CFLAGS) $(INCLUDES) -MM $(4) >> $(1) || exit 1; \
	awk '{if (sub(/\\$$/,"")) printf "%s", $$0; else print $$0}' $(1) > "$(1).0"; \
	mv "$(1).0" $(1) && \
	for c in $(CONFS); do \
		awk '{printf("$(OBJDIR)/%s/$(2)/$(5)/%s\n", conf, $$0)}' conf=$$c $(1) >> "$(1).0"; \
		echo "$(3)/$$c/$(6): $(patsubst %.c, $(OBJDIR)/$$c/%.o, $(4))" >> "$(1).0"; \
	done; \
	mv "$(1).0" $(1)

drivname = $(shell echo $(1) | sed -E 's/hpcap_(ixgbe)?([a-zA-Z]*).*/hpcap\2/')

DRIV_TARGETS := $(foreach driver, $(DRIV_NAMES), $(call drivname, $(driver)))
DRIV_BINARIES := $(foreach conf, $(CONFS), $(wildcard $(BINDIR)/$(conf)/*.ko))

ifeq (Linux,$(shell uname))
	-include $(ALL_CONFS)
	-include $(ALL_DEPS)
	-include $(DRIVERVARS)
endif

# Generate the configuration for the given output sample. Include
# dependencies on the libraries.
$(OBJDIR)/.sample-%-conf.mk: Makefile | $(OBJDIR)
	@$(call generate_config,$@,$(SAMPLEDIR),$(BINDIR),$*,$*)
	@for c in $(CONFS); do \
		for s in $(SAMPLES); do \
			echo "$(BINDIR)/$$c/$$s: $(addprefix $(LIBDIR)/$$c/, $(LIBS))" >> $@; \
		done; \
	done; \

$(OBJDIR)/.lib-%-conf.mk: Makefile | $(OBJDIR)
	@$(call generate_config,$@,$(LIBSRCDIR),$(LIBDIR),$*,$*.a)

# This weird rule is actually pretty simple. I'd comment it between the lines but that would
# mess up with the shell long line.
#
# What we want with all of this is to be able to generate driver modules with different
# configurations, and to be able to control the output of the objects in order to avoid filling
# the code trees with temporary files, which is really ugly (although this rule is probably uglier).
# The reasoning behind this is explained in the developer documentation guide.
#
# To achieve that, we automatically generate some rules for each driver. You can take a look
# at the generated files in the obj directory and see that it's actually fairly simple.
#
# First, we generate the user-friendly name ($fname) with the regex, based on directory names.
# The names will be either hpcap or hpcapvf.
#
# The module (*.ko) is placed in the bin/confname directory. It is a simple cp command.
# Its prerequisite is the actual driver file, generated in the duplicated source tree
# in the obj/kernel/confname/drivername directory. Make will automatically copy out of date
# files.
#
# To build the files, we call the kernel build system. We have to copy the whole source tree
# for each configuration because it's impossible to change the output directory of the kernel
# build system. This is the trick that allows us to maintain different build configurations.
#
# The mkmakefile script generates automatically an script for the configuration.
#
# PS: This would be so much easier if Make had the ability to do multiple matches in one pattern.
$(OBJDIR)/.driver-%-conf.mk: Makefile $(COMMON_FILES) $(DRIVDIR)/%/driver/*.c $(COMMON_DIR)/hpcap_version.h | $(OBJDIR)
	@-rm -f $@
	@for cnf in $(CONFS); do \
		fname="$(call drivname,$*)"; \
		cnfcaps=$$(echo $$cnf | tr '[:lower:]' '[:upper:]'); \
		echo -n "$(BINDIR)/$$cnf/$$fname.ko"; \
		echo ": $(KOBJDIR)/$$cnf/$*/$$fname.ko | dirs"; \
		echo "	@cp \$$< \$$@"; \
		echo ""; \
		echo "$${fname}_$${cnf}_orig_files := \$$(wildcard $(DRIVDIR)/$*/driver/*.c) \$$(wildcard $(DRIVDIR)/$*/driver/*.h)"; \
		echo "$${fname}_$${cnf}_orig_files += \$$(wildcard $(COMMON_DIR)/*.c) \$$(wildcard $(COMMON_DIR)/*.h)"; \
		echo "$${fname}_$${cnf}_files := \$$(patsubst %, $(KOBJDIR)/$$cnf/$*/%, \$$(notdir \$$($${fname}_$${cnf}_orig_files)))"; \
		echo "$(KOBJDIR)/$$cnf/$*/$$fname.ko: \$$($${fname}_$${cnf}_files) $(KOBJDIR)/$$cnf/$*/Makefile"; \
		echo "	\$$(KERN_$${cnfcaps}_ENVVARS) \$$(MAKE) -C \$$(KSRC) \$$(KOBJRULE) SUBDIRS=\$$(shell pwd)/$(KOBJDIR)/$$cnf/$* modules"; \
		echo "$(KOBJDIR)/$$cnf/$*/%.c: $(DRIVDIR)/$*/driver/%.c"; \
		echo "	@cp \$$< \$$@"; \
		echo "$(KOBJDIR)/$$cnf/$*/%.c: $(COMMON_DIR)/%.c"; \
		echo "	@cp \$$< \$$@"; \
		echo "$(KOBJDIR)/$$cnf/$*/%.h: $(DRIVDIR)/$*/driver/%.h"; \
		echo "	@cp \$$< \$$@"; \
		echo "$(KOBJDIR)/$$cnf/$*/%.h: $(COMMON_DIR)/%.h"; \
		echo "	@cp \$$< \$$@"; \
		echo "$(KOBJDIR)/$$cnf/$*/Makefile: Makefile scripts/mkmakefile"; \
		echo "	@scripts/mkmakefile $$fname $$cnf \"\$$(KERN_$${cnfcaps}_CFLAGS) $(INCLUDES_ABS)\" $(DRIVERVARS_KBUILD) > \$$@"; \
		echo "$$cnf: $(BINDIR)/$$cnf/$$fname.ko"; \
		echo "drivers-$$cnf: $(BINDIR)/$$cnf/$$fname.ko"; \
		echo; \
	done >> $@
	@echo "$(call drivname,$*): $(BINDIR)/$(DEFAULT_CONF)/$(call drivname,$*).ko" >> $@


# Detect file dependencies for each object file.
$(OBJDIR)/.sample-%-deps.mk: Makefile $(SAMPLEDIR)/%/*.c | $(OBJDIR)
	@$(call generate_deps,$@,$(SAMPLEDIR),$(BINDIR),$(filter-out Makefile,$^),$*,$*)

$(OBJDIR)/.lib-%-deps.mk: $(LIBSRCDIR)/%/*.c Makefile | $(OBJDIR)
	@$(call generate_deps,$@,$(LIBSRCDIR),$(LIBDIR),$(filter-out Makefile,$^),$*,$*.a)

####################################
## Build information			  ##
####################################
SOURCES_AFFECTING_VERSION := $(filter-out $(COMMON_DIR)/hpcap_version.h, $(ALL_SRCS))

VERSION_FILE = .svnversion
DEFAULT_BRANCH = trunk
BRANCH = $(shell svn info 2> /dev/null | grep '^URL:' | egrep -o '(tags|branches)/[^/]+|trunk' || echo "$(DEFAULT_BRANCH)")
REVISION = $(shell svnversion -n 2>/dev/null || cat $(VERSION_FILE) 2>/dev/null || echo "no-cvs-info")

VERSION_BRANCH_INFO = from $(BRANCH)

# hpcap_version.h should be regenerated also when the SVN revision changes.
# However, SVN is not like git where we have a nice file that changes when
# the revision or HEAD pointer changes, so I can't do that. I can't neither
# make this file depend on VERSION_FILE because it hangs the build process.
$(COMMON_DIR)/hpcap_version.h: $(SOURCES_AFFECTING_VERSION)
	@echo "#ifndef HPCAP_VERSION_H" > $@
	@echo "#define HPCAP_VERSION_H" >> $@
	@echo "#define HPCAP_REVISION \"$(REVISION) $(VERSION_BRANCH_INFO)\"" >> $@
	@echo "#define HPCAP_BUILD_DATE \"$(shell date +"%d %b %Y %R %Z")\"" >> $@
	@echo "#define HPCAP_BUILD_INFO \"rev \" HPCAP_REVISION \" built \" HPCAP_BUILD_DATE" >> $@
	@echo "#endif" >> $@

$(VERSION_FILE): .FORCE
ifneq (,$(shell which svnversion 2>/dev/null))
	@echo $(shell svnversion 2>/dev/null || echo "no-svn-info") > $@
else
	@[ -e $@ ] || echo "no-svn-info" > $@
endif

version-info: $(COMMON_DIR)/hpcap_version.h $(VERSION_FILE)

####################################
## Directory management 		  ##
####################################
dirs: $(OBJDIR) $(BINDIR) $(LIBDIR)

$(OBJDIR): Makefile
	@for c in $(CONFS); do \
		mkdir -p $(OBJDIR)/$$c/$(SRCDIR); \
		mkdir -p $(OBJDIR)/$$c/$(LIBSRCDIR); \
		mkdir -p $(OBJDIR)/$$c/$(TESTDIR); \
	done
	@mkdir -p $(SAMPLE_OBJDIRS)
	@mkdir -p $(DRIV_OBJDIRS)
	@mkdir -p $(KOBJDIR)

$(BINDIR): Makefile
	@for c in $(CONFS); do \
		mkdir -p $(BINDIR)/$$c; \
	done

$(LIBDIR): Makefile
	@mkdir -p $(LIB_OUTDIRS)
	@mkdir -p $(LIB_OBJDIRS)


####################################
## Compilation rules			  ##
####################################
$(OBJDIR)/%.o: | $(OBJDIR) depend config
	@echo "$< -> $@"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BINDIR)/%: | $(BINDIR) depend config
	@echo "$(FMT_BOLD)Building binary $* $(FMT_NORM)"
	@$(CC) $(CFLAGS) $(filter-out libs, $^) $(LDFLAGS) -o $@

$(LIBDIR)/%.a: | $(LIBDIR) depend config
	@echo "$(FMT_BOLD)Building static library $* $(FMT_NORM)"
	@$(AR) -r $@ $^

####################################
## Cleaning rules				  ##
####################################
clean: codeclean

codeclean:
	@-rm -rf $(BINDIR) $(LIBDIR) $(OBJDIR)

distclean:
	@-rm -rf $(OBJDIR)

driverclean:
	@-rm -rf $(KOBJDIR) $(DRIV_BINARIES)


####################################
## Documentation				  ##
####################################
docs: $(PDF_PATHS) doxydoc

doxydoc: $(ALL_SRCS)
	@doxygen

%.pdf: %.tex
	@echo "$(FMT_BOLD)Latexmk: generating $@ $(FMT_NORM)"
	@cd $(dir $<); latexmk $(LATEXFLAGS) $(notdir $<)

cscope: cscope.out

cscope.out: $(LIB_SRCS) $(SAMPLE_SRCS)
	@cscope -bR -f$@


####################################
## Drivers (for convenience)	  ##
####################################

drivers: $(DRIV_TARGETS) | dirs


####################################
## Installing					  ##
####################################

include Makefile-install.mk

####################################
## Packaging					  ##
####################################

TAR_EXCLUDES_DIST = obj *.tar.gz .git tasks \
		cscope.out *.sublime-project *.sublime-workspace \
		*.dat callgrind.* gmon.out \
		*.blg *.aux *.bbl *.bib *.fdb_latexmk *.fls *.log *.toc \
		*.idx *.ilg *.ind *.out *.synctex.gz *.pyg.. \
		doc/man doc/html
TAR_EXCLUDES_SRC = $(TAR_EXCLUDES_DIST) bin lib
TAR_EXCLUDES_DIST_ARG = $(addprefix --exclude=, $(TAR_EXCLUDES_DIST))
TAR_EXCLUDES_SRC_ARG = $(addprefix --exclude=, $(TAR_EXCLUDES_SRC))

TAR_BRANCH = _$(shell echo $(BRANCH) | tr '/' '-' | sed 's/release/pre-release/')

TAR_TARGET = HPCAP$(TAR_BRANCH)_$(shell date +"%F")_rev$(subst :,-,$(REVISION))

changes: version-info

pack: docs changes
	@cd ..; COPYFILE_DISABLE=1 tar $(TAR_EXCLUDES_SRC_ARG) -czf $(TAR_TARGET).tar.gz $(lastword $(notdir $(CURDIR)))
	@mv ../$(TAR_TARGET).tar.gz .
	@echo "Packed $(TAR_TARGET).tar.gz."

dist: release docs changes
	@cd ..; COPYFILE_DISABLE=1 tar $(TAR_EXCLUDES_DIST_ARG) -czf $(TAR_TARGET)_dist.tar.gz $(lastword $(notdir $(CURDIR)))
	@mv ../$(TAR_TARGET)_dist.tar.gz .
	@echo "Packed $(TAR_TARGET).tar.gz."
