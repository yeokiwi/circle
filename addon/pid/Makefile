#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= pidcontroller.o pidautotuner.o

libpid.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
