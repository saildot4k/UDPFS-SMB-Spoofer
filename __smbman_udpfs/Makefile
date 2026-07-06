IOP_BIN = ../smbman.irx
IOP_INCS += -I../__ps2ip_ministack/src/include -Iinclude
IOP_CFLAGS += -mno-check-zero-division -DFEATURE_UDPFS_IOMAN -DUDPRDMA_MAX_SOCKETS=1 -DIOMANX_OLD_NAME_COMPATIBILITY=0 -DIOMANX_OLD_NAME_ADDDELDRV=0
NETWORK_OBJS = smap.o xfer.o ministack_eth.o ministack_arp.o ministack_ip.o ministack_udp.o udptty.o
IOP_OBJS = exports.o main.o $(NETWORK_OBJS) imports.o udprdma.o udpfs_core.o udpfs_ioman.o

include ../global.make

$(addprefix $(IOP_OBJS_DIR),$(NETWORK_OBJS)): $(IOP_OBJS_DIR)%.o: ../__ps2ip_ministack/src/%.c
	$(DIR_GUARD)
	$(IOP_C_COMPILE) -c $< -o $@
