#
# Makefile
#

CIRCLEHOME = ../..

JSDLIB	 = $(CIRCLEHOME)/addon/jsd
JSDHOME	?= $(JSDLIB)/jsd
SOEMLIB	 = $(CIRCLEHOME)/addon/soem
SOEMHOME ?= $(SOEMLIB)/SOEM

ifeq ($(strip $(wildcard $(JSDLIB)/libjsd.a)),)

# The JSD library has not been built. This sample is skipped, so that it does
# not break a build of all samples (see sample/makeall).

.PHONY: all clean

all:
	@echo "  SKIP  sample/48-elmoweb"
	@echo "        Build $(JSDLIB)/libjsd.a first (see addon/jsd/README)!"

clean:
	@echo "  CLEAN " `pwd`
	@rm -f *.d *.o *.a *.elf *.lst *.img *.hex *.cir *.map *~

else

OBJS	= main.o kernel.o elmodrive.o webserver.o sysinfo.o

LIBS	= $(JSDLIB)/libjsd.a \
	  $(SOEMLIB)/libsoem.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

INCLUDE	+= -I $(JSDLIB)/include -I $(JSDLIB)/compat -I $(JSDHOME)/include \
	   -I $(SOEMLIB) -I $(SOEMLIB)/include \
	   -I $(SOEMHOME)/include -I $(SOEMHOME)/osal

DEFINE	+= -U_FORTIFY_SOURCE

-include $(DEPS)

endif
