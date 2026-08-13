#
# Makefile
#

CIRCLEHOME = ../..

SOEMLIBHOME = $(CIRCLEHOME)/addon/soem
SOEMHOME ?= $(SOEMLIBHOME)/SOEM

ifeq ($(strip $(wildcard $(SOEMLIBHOME)/libsoem.a)),)

# The SOEM library has not been built. This sample is skipped, so that it does
# not break a build of all samples (see sample/makeall).

.PHONY: all clean

all:
	@echo "  SKIP  sample/47-ethercatweb"
	@echo "        Build $(SOEMLIBHOME)/libsoem.a first (see addon/soem/README)!"

clean:
	@echo "  CLEAN " `pwd`
	@rm -f *.d *.o *.a *.elf *.lst *.img *.hex *.cir *.map *~

else

OBJS	= main.o kernel.o ethercat.o webserver.o sysinfo.o

LIBS	= $(SOEMLIBHOME)/libsoem.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

INCLUDE	+= -I $(SOEMLIBHOME) -I $(SOEMLIBHOME)/include \
	   -I $(SOEMHOME)/include -I $(SOEMHOME)/osal

-include $(DEPS)

endif
