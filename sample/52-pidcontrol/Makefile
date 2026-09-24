#
# Makefile
#

CIRCLEHOME = ../..

PIDLIBHOME = $(CIRCLEHOME)/addon/pid

ifeq ($(strip $(wildcard $(PIDLIBHOME)/libpid.a)),)

# The PID library has not been built. This sample is skipped, so that it does
# not break a build of all samples (see sample/makeall).

.PHONY: all clean

all:
	@echo "  SKIP  sample/52-pidcontrol"
	@echo "        Build $(PIDLIBHOME)/libpid.a first (see addon/pid/README)!"

clean:
	@echo "  CLEAN " `pwd`
	@rm -f *.d *.o *.a *.elf *.lst *.img *.hex *.cir *.map *~

else

OBJS	= main.o kernel.o heaterplant.o

LIBS	= $(PIDLIBHOME)/libpid.a \
	  $(CIRCLEHOME)/lib/libcircle.a

include ../Rules.mk

-include $(DEPS)

endif
