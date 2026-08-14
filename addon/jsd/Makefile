#
# Makefile
#

CIRCLEHOME = ../..

JSDHOME  ?= ./jsd
SOEMLIB  ?= $(CIRCLEHOME)/addon/soem
SOEMHOME ?= $(SOEMLIB)/SOEM

ifeq ($(strip $(wildcard $(JSDHOME)/src/jsd_egd.c)),)
ifneq ($(MAKECMDGOALS),clean)
$(warning The JSD sources have not been found in $(JSDHOME).)
$(warning Please read the file addon/jsd/README for instructions!)
$(error JSD sources not found)
endif
endif

# JSD uses C11 features (anonymous unions in jsd_slave_config_t)
C_STANDARD = -std=gnu11

# The device drivers of JSD for the Elmo drives, which are used unmodified.
# Add the other device drivers here, if you need them, and extend
# jsd_init_single_device() in circle/jsd_circle.cpp accordingly.
JSDSRCS	= $(JSDHOME)/src/jsd_egd.c \
	  $(JSDHOME)/src/jsd_epd_nominal.c \
	  $(JSDHOME)/src/jsd_epd_common.c \
	  $(JSDHOME)/src/jsd_elmo_common.c \
	  $(JSDHOME)/src/jsd_common_device_types.c \
	  $(JSDHOME)/src/jsd_error_cirq.c

# The Circle port: bus layer, SDO layer and the OS support functions
PORTOBJS = circle/jsd_circle.o circle/jsd_sdo_circle.o circle/jsd_support.o

OBJS	= $(PORTOBJS) $(JSDSRCS:.c=.o)

EXTRACLEAN = $(PORTOBJS) $(PORTOBJS:.o=.d) $(JSDSRCS:.c=.o) $(JSDSRCS:.c=.d)

# The JSD sources include their headers as "jsd/xxx.h", but the headers are in
# src/. The CMake build of JSD copies them to include/jsd/, this link does the
# same without copying. It has to be done, while this file is read, because the
# dependency files are generated before any rule is executed.
ifneq ($(strip $(wildcard $(JSDHOME)/src/jsd_egd.c)),)
$(shell mkdir -p $(JSDHOME)/include && ln -sfn ../src $(JSDHOME)/include/jsd)
endif

libjsd.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

# ./include and ./compat must come first, they override the JSD headers
# jsd_print.h and jsd_time.h and provide "ethercat.h" and <pthread.h>
INCLUDE	+= -I include -I compat -I $(JSDHOME)/include \
	   -I $(SOEMLIB) -I $(SOEMLIB)/include \
	   -I $(SOEMHOME)/include -I $(SOEMHOME)/osal

# snprintf() is provided by the Circle port of SOEM, but some toolchains
# redirect it to a function of the C library, which is not linked here.
DEFINE	+= -U_FORTIFY_SOURCE

# Build with "make JSD_DEBUG=1" to enable the MSG_DEBUG messages of JSD.
JSD_DEBUG ?= 0

ifeq ($(strip $(JSD_DEBUG)),1)
DEFINE	+= -DDEBUG
endif

-include $(DEPS)
