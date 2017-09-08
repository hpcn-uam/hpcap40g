INSTALL_PATH ?=

INSTALL_LIB_PATH = $(INSTALL_PATH)/usr/lib
INSTALL_BIN_PATH = $(INSTALL_PATH)/usr/bin
INSTALL_ETC_PATH = $(INSTALL_PATH)/etc/hpcap
INSTALL_INC_PATH = $(INSTALL_PATH)/usr/include
INSTALL_AUX_PATH = $(INSTALL_PATH)/usr/share/hpcap
INSTALL_MAN_PATH = $(INSTALL_PATH)/usr/share/man
INSTALL_MOD_PATH = $(INSTALL_PATH)/lib/modules/$(shell uname -r)
INSTALL_MODPROBE_CONF = $(INSTALL_PATH)/etc/modprobe.d/hpcap.conf

BIN_TO_INSTALL_NAMES = huge_map hpcapdd hpcapdd_p mgmon raw2pcap checkraw
BIN_TO_INSTALL = $(addprefix $(BINDIR)/$(DEFAULT_CONF)/, $(BIN_TO_INSTALL_NAMES))

SCR_TO_INSTALL_NAMES = hugepage_mount hpcap-monitor hpcap-modprobe hpcap-modprobe-remove \
	launch-hpcap-monitors stop-hpcap-monitors parse-hpcap-log hpcap-status
SCR_TO_INSTALL = $(addprefix $(SCRIPTDIR)/, $(SCR_TO_INSTALL_NAMES))

INSTALLED_MANPAGES = $(patsubst $(DOCDIR)/man/%, $(INSTALL_MAN_PATH)/%, $(wildcard $(DOCDIR)/man/**/*))
INSTALLED_MANPAGES_FILE = $(INSTALL_AUX_PATH)/.manpages

install-libs: libs
	@install -d $(INSTALL_LIB_PATH) $(INSTALL_INC_PATH)
	@install -D -m 755 $(wildcard $(LIBDIR)/$(DEFAULT_CONF)/*) $(INSTALL_LIB_PATH)/
	@install -D -m 644 $(wildcard $(INCDIR)/*) $(INSTALL_INC_PATH)

install: install-libs $(BIN_TO_INSTALL) drivers
	@install -d $(INSTALL_ETC_PATH) $(INSTALL_BIN_PATH)
	@if [ -e $(INSTALL_ETC_PATH)/params.cfg ]; then \
		install -m 644 params.cfg $(INSTALL_ETC_PATH)/params.cfg.new; \
	else \
		install -D -m 644 params.cfg $(INSTALL_ETC_PATH)/params.cfg; \
	fi
	@install -D -m 755 $(SCR_TO_INSTALL) $(BIN_TO_INSTALL) $(INSTALL_BIN_PATH)
	@install -D -m 644 $(SCRIPTDIR)/hpcap-lib.bash $(INSTALL_BIN_PATH)
	@install -m 644 $(wildcard $(BINDIR)/$(DEFAULT_CONF)/*.ko) $(INSTALL_MOD_PATH)
	@$(SCRIPTDIR)/generate_modprobe_conf "$(INSTALL_PATH)" $(DRIV_TARGETS) > modprobe.tmp
	@install -D -m 644 modprobe.tmp $(INSTALL_MODPROBE_CONF)
	@rm modprobe.tmp
	@depmod -a
	@-update-initramfs -u

install-doc:
	@install -d $(INSTALL_MAN_PATH) $(INSTALL_AUX_PATH)
	@cp -r $(wildcard $(DOCDIR)/man/*) $(INSTALL_MAN_PATH)
	@install -D -m 644 $(PDF_PATHS) $(INSTALL_AUX_PATH)
	@echo $(INSTALLED_MANPAGES) >> $(INSTALLED_MANPAGES_FILE)

SCR_TO_REMOVE = $(addprefix $(INSTALL_BIN_PATH)/, $(SCR_TO_INSTALL_NAMES))
BIN_TO_REMOVE = $(addprefix $(INSTALL_BIN_PATH)/, $(BIN_TO_INSTALL_NAMES))
LIB_TO_REMOVE = $(addprefix $(INSTALL_LIB_PATH)/, $(notdir $(wildcard $(LIBDIR)/$(DEFAULT_CONF)/*)))
DRV_TO_REMOVE = $(addprefix $(INSTALL_MOD_PATH)/, $(notdir $(wildcard $(BINDIR)/$(DEFAULT_CONF)/*.ko)))

uninstall:
	@-rm $(shell cat $(INSTALLED_MANPAGES_FILE))
	@-rm -rf $(INSTALL_ETC_PATH) $(INSTALL_MODPROBE_CONF) $(INSTALL_AUX_PATH)
	@-rm $(SCR_TO_REMOVE) $(BIN_TO_REMOVE) $(LIB_TO_REMOVE) $(DRV_TO_REMOVE)
	@-rm $(INSTALL_BIN_PATH)/hpcap-lib.bash
	@depmod -a
