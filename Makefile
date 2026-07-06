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

PACKAGE = UDPFS_SMB_Spoofer.zip

all:
	$(foreach module,$(MODULES),$(MAKE) -C $(module) all &&) true

clean:
	$(foreach module,$(MODULES),$(MAKE) -C $(module) clean &&) true
	rm -rf READY_TO_USE_SMB $(PACKAGE)

package: all
	rm -f $(PACKAGE)
	zip -9 $(PACKAGE) $(IRX_FILES)

.PHONY: all clean package
