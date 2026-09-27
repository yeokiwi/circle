#
# Makefile
#

CIRCLEHOME = ../..

SOFTRESETHOME = $(CIRCLEHOME)/addon/softreset

ifeq ($(strip $(wildcard $(SOFTRESETHOME)/libsoftreset.a)),)

# The software reset library has not been built. This sample is skipped, so that
# it does not break a build of all samples (see sample/makeall).

.PHONY: all clean

all:
	@echo "  SKIP  sample/53-softreset"
	@echo "        Build $(SOFTRESETHOME)/libsoftreset.a first (see addon/softreset/README)!"

clean:
	@echo "  CLEAN " `pwd`
	@rm -f *.d *.o *.a *.elf *.lst *.img *.hex *.cir *.map *~

else

OBJS	= main.o kernel.o

LIBS	= $(SOFTRESETHOME)/libsoftreset.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

-include $(DEPS)

endif
