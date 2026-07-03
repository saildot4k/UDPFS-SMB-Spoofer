MODULES = \
	__poweroff \
	__ps2dev9 \
	__smsutils_stub \
	__ps2ip_ministack \
	__ps2smap_stub \
	__smbman_udpfs

IRX_FILES = \
	poweroff.irx \
	ps2dev9.irx \
	smsutils.irx \
	ps2ip.irx \
	ps2smap.irx \
	smbman.irx

PACKAGE = UDPFS_SMB_Spoofer.7z
READY_DIR = READY_TO_USE_SMB

all:
	$(foreach module,$(MODULES),$(MAKE) -C $(module) all &&) true

clean:
	$(foreach module,$(MODULES),$(MAKE) -C $(module) clean &&) true
	rm -rf $(READY_DIR) $(PACKAGE)

package: all
	rm -rf $(READY_DIR)
	mkdir -p $(READY_DIR)
	cp $(IRX_FILES) $(READY_DIR)/
	7z a -t7z $(PACKAGE) $(IRX_FILES) $(READY_DIR)/*

.PHONY: all clean package
