IOP_BIN = ../smbman.irx
IOP_INCS += -Iinclude -I../__ps2ip_ministack/src/include
IOP_CFLAGS += -mno-check-zero-division -DFEATURE_UDPFS_IOMAN -DUDPRDMA_MAX_SOCKETS=1
IOP_OBJS = exports.o main.o imports.o udprdma.o udpfs_core.o udpfs_ioman.o

include ../global.make
