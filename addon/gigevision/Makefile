#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= gvcpclient.o gvspreceiver.o gigecamera.o \
	  genicam.o genicamformula.o xmlparser.o unzip.o

libgigevision.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
