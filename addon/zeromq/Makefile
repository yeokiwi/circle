#
# Makefile
#

CIRCLEHOME = ../..

OBJS	= zmqmessage.o zmtpconnection.o zmqsocket.o zmqpublisher.o zmqsubscriber.o

libzeromq.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

-include $(DEPS)
