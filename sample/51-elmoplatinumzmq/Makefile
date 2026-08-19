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
	@echo "  SKIP  sample/51-elmoplatinumzmq"
	@echo "        Build $(SOEMLIBHOME)/libsoem.a first (see addon/soem/README)!"

clean:
	@echo "  CLEAN " `pwd`
	@rm -f *.d *.o *.a *.elf *.lst *.img *.hex *.cir *.map *~

else

OBJS	= main.o kernel.o elmo_core1.o zmqclient.o zmtp.o

LIBS	= $(SOEMLIBHOME)/libsoem.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

INCLUDE	+= -I $(SOEMLIBHOME) -I $(SOEMLIBHOME)/include \
	   -I $(SOEMHOME)/include -I $(SOEMHOME)/osal

-include $(DEPS)

endif
