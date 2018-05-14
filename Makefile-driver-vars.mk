ifeq (,$(BUILD_KERNEL))
BUILD_KERNEL=$(shell uname -r)
endif

PRE_EXCLUDED_CFILES = hpcap_params_1.c hpcap_params_2.c kcompat_ethtool.c i40e_fcoe.c ixgbe_hv_vf.c ixgbe_cna.c
EXTRA_HFILES =

DRIV_SUFFIX = $(shell echo $(DRIVER_NAME) | sed -E 's/hpcap(\w*)/\1/' | tr '[:lower:]' '[:upper:]')

ifeq ($(DRIV_SUFFIX),MLX)
DRIV_MACRO_ID = "HPCAP_MLNX"
$(info HPCAP_MLNX)
else
ifeq ($(DRIV_SUFFIX),I)
DRIV_MACRO_ID = "HPCAP_I40E"
else
ifeq ($(DRIV_SUFFIX),IVF)
DRIV_MACRO_ID = "HPCAP_I40EVF"
else
DRIV_MACRO_ID = "HPCAP_IXGBE$(DRIV_SUFFIX)"
endif
endif
endif


###########################################################################
# Environment tests

# Kernel Search Path
# All the places we look for kernel source
KSP :=  /lib/modules/$(BUILD_KERNEL)/build \
        /lib/modules/$(BUILD_KERNEL)/source \
        /usr/src/linux-$(BUILD_KERNEL) \
        /usr/src/linux-$($(BUILD_KERNEL) | sed 's/-.*//') \
        /usr/src/kernel-headers-$(BUILD_KERNEL) \
        /usr/src/kernel-source-$(BUILD_KERNEL) \
        /usr/src/kernels/$(BUILD_KERNEL) \
        /usr/src/linux-$($(BUILD_KERNEL) | sed 's/\([0-9]*\.[0-9]*\)\..*/\1/') \
        /usr/src/kernels/$($(BUILD_KERNEL) | sed 's/\([0-9]*\.[0-9]*\)\..*/\1/') \
        /usr/src/linux

# prune the list down to only values that exist
# and have an include/linux sub-directory
test_dir = $(shell [ -e $(dir)/include/linux ] && echo $(dir))
KSP := $(foreach dir, $(KSP), $(test_dir))

# we will use this first valid entry in the search path
ifeq (,$(KSRC))
  KSRC := $(firstword $(KSP))
endif

ifeq (,$(KSRC))
  $(warning *** Kernel header files not in any of the expected locations.)
  $(warning *** Install the appropriate kernel development package, e.g.)
  $(error kernel-devel, for building kernel modules and try again)
else
ifeq (/lib/modules/$(BUILD_KERNEL)/source, $(KSRC))
  KOBJ :=  /lib/modules/$(BUILD_KERNEL)/build
else
  KOBJ :=  $(KSRC)
endif
endif

# Version file Search Path
VSP :=  $(KOBJ)/include/generated/utsrelease.h \
        $(KOBJ)/include/linux/utsrelease.h \
  $(KOBJ)/include/linux/version.h \
  $(KOBJ)/include/generated/uapi/linux/version.h \
        /boot/vmlinuz.version.h

# Config file Search Path
CSP :=  $(KOBJ)/include/generated/autoconf.h \
        $(KOBJ)/include/linux/autoconf.h \
        /boot/vmlinuz.autoconf.h

# prune the lists down to only files that exist
test_file = $(shell [ -f $(file) ] && echo $(file))
VSP := $(foreach file, $(VSP), $(test_file))
CSP := $(foreach file, $(CSP), $(test_file))

##$(info $(VSP))

# and use the first valid entry in the Search Paths
ifeq (,$(VERSION_FILE))
  VERSION_FILE := $(firstword $(VSP))
endif
ifeq (,$(CONFIG_FILE))
  CONFIG_FILE := $(firstword $(CSP))
endif

ifeq (,$(wildcard $(VERSION_FILE)))
  $(error Linux kernel source not configured - missing version header file)
endif

ifeq (,$(wildcard $(CONFIG_FILE)))
  $(error Linux kernel source not configured - missing autoconf.h)
endif

# pick a compiler
ifneq (,$(findstring egcs-2.91.66, $(shell cat /proc/version)))
  CC := kgcc gcc cc
else
  CC := gcc cc
endif
test_cc = $(shell $(cc) --version > /dev/null 2>&1 && echo $(cc))
CC := $(foreach cc, $(CC), $(test_cc))
CC := $(firstword $(CC))
ifeq (,$(CC))
  $(error Compiler not found)
endif

# we need to know what platform the driver is being built on
# some additional features are only built on Intel platforms
ARCH := $(shell uname -m | sed 's/i.86/i386/')
ifeq ($(ARCH),alpha)
  EXTRA_CFLAGS += -ffixed-8 -mno-fp-regs
endif
ifeq ($(ARCH),x86_64)
  EXTRA_CFLAGS += -mcmodel=kernel -mno-red-zone
endif
ifeq ($(ARCH),ppc)
  EXTRA_CFLAGS += -msoft-float
endif
ifeq ($(ARCH),ppc64)
  EXTRA_CFLAGS += -m64 -msoft-float
  LDFLAGS += -melf64ppc
endif

# extra flags for module builds
EXTRA_CFLAGS += -DDRIVER_$(shell echo $(DRIVER_NAME) | tr '[a-z]' '[A-Z]')
EXTRA_CFLAGS += -DDRIVER_NAME=$(DRIVER_NAME)
EXTRA_CFLAGS += -DDRIVER_NAME_CAPS=$(shell echo $(DRIVER_NAME) | tr '[a-z]' '[A-Z]')
# standard flags for module builds
EXTRA_CFLAGS += -DLINUX -D__KERNEL__ -DMODULE -O2 -pipe -Wall
EXTRA_CFLAGS += -I$(KSRC)/include -I.
EXTRA_CFLAGS += $(shell [ -f $(KSRC)/include/linux/modversions.h ] && \
            echo "-DMODVERSIONS -DEXPORT_SYMTAB \
                  -include $(KSRC)/include/linux/modversions.h")
EXTRA_CFLAGS += -fno-pie

ifneq (,$(DRIV_MACRO_ID))
EXTRA_CFLAGS += -D$(DRIV_MACRO_ID)
endif

EXTRA_CFLAGS += $(CFLAGS_EXTRA)

RHC := $(KSRC)/include/linux/rhconfig.h
ifneq (,$(wildcard $(RHC)))
  # 7.3 typo in rhconfig.h
  ifneq (,$(shell $(CC) $(CFLAGS) -E -dM $(RHC) | grep __module__bigmem))
  EXTRA_CFLAGS += -D__module_bigmem
  endif
endif

# get the kernel version - we use this to find the correct install path
KVER := $(shell $(CC) $(EXTRA_CFLAGS) -E -dM $(VERSION_FILE) | grep UTS_RELEASE | \
        awk '{ print $$3 }' | sed 's/\"//g')

# assume source symlink is the same as build, otherwise adjust KOBJ
ifneq (,$(wildcard /lib/modules/$(KVER)/build))
ifneq ($(KSRC),$(shell readlink /lib/modules/$(KVER)/build))
  KOBJ=/lib/modules/$(KVER)/build
endif
endif

KVER_CODE := $(shell $(CC) $(EXTRA_CFLAGS) -E -dM $(VSP) 2>/dev/null |\
  grep -m 1 LINUX_VERSION_CODE | awk '{ print $$3 }' | sed 's/\"//g')

# abort the build on kernels older than 2.4.0
ifneq (1,$(shell [ $(KVER_CODE) -ge 132096 ] && echo 1 || echo 0))
  $(error *** Aborting the build. \
          *** This driver is not supported on kernel versions older than 2.4.0)
endif

# Add DCB netlink source if our kernel is 2.6.23 or newer
ifeq (1,$(shell [ $(KVER_CODE) -ge 132631 ] && echo 1 || echo 0))
  EXTRA_CFILES += ixgbe_dcb_nl.c
endif

# Add FCoE source if FCoE is supported by the kernel
FCOE := $(shell grep -wE 'CONFIG_FCOE|CONFIG_FCOE_MODULE' $(CONFIG_FILE) | \
  awk '{print $$3}')
ifeq ($(FCOE), 1)
  EXTRA_CFILES += ixgbe_fcoe.c
  EXTRA_HFILES += ixgbe_fcoe.h
endif

ifneq ($(CONFIG_COMPAT_EN_SYSFS),)
  EXTRA_CFILES += en_sysfs.c
endif

# set the install path before and after 3.2.0
ifeq (1,$(shell [ $(KVER_CODE) -lt 197120 ] && echo 1 || echo 0))
INSTDIR := /lib/modules/$(KVER)/kernel/drivers/net/$(DRIVER_NAME)
else
INSTDIR := /lib/modules/$(KVER)/kernel/drivers/net/ethernet/intel/$(DRIVER_NAME)
endif

ifneq ($(filter %NO_PTP_SUPPORT,$(CFLAGS_EXTRA)),-DNO_PTP_SUPPORT)
  ifeq (1,$(shell ([ $(KVER_CODE) -ge 196608 ] || [ $(RHEL_CODE) -ge 1540 ]) && echo 1 || echo 0))
    include $(KOBJ)/include/config/auto.conf

    ifdef CONFIG_PTP_1588_CLOCK
      EXTRA_CFILES += ixgbe_ptp.c
    endif # CONFIG_PTP_1588_CLOCK
  endif # kernel version >= 3.0
endif # !NO_PTP_SUPPORT

# look for SMP in config.h
SMP := $(shell $(CC) $(EXTRA_CFLAGS) -E -dM $(CONFIG_FILE) | \
         grep -w CONFIG_SMP | awk '{ print $$3 }')
ifneq ($(SMP),1)
  SMP := 0
endif

ifneq ($(SMP),$(shell uname -a | grep SMP > /dev/null 2>&1 && echo 1 || echo 0))
  $(warning ***)
  ifeq ($(SMP),1)
    $(warning *** Warning: kernel source configuration (SMP))
    $(warning *** does not match running kernel (UP))
  else
    $(warning *** Warning: kernel source configuration (UP))
    $(warning *** does not match running kernel (SMP))
  endif
  $(warning *** Continuing with build,)
  $(warning *** resulting driver may not be what you want)
  $(warning ***)
endif

ifeq ($(SMP),1)
  EXTRA_CFLAGS += -D__SMP__
endif

ifeq ($(KOBJ),$(KSRC))
  KOBJRULE =
else
  KOBJRULE := O=$(KOBJ)
endif
