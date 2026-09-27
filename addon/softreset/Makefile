#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= softreset.o

libsoftreset.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
