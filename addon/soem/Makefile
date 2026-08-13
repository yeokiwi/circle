#
# Makefile
#

CIRCLEHOME = ../..

SOEMHOME ?= ./SOEM

ifeq ($(strip $(wildcard $(SOEMHOME)/src/ec_main.c)),)
ifneq ($(MAKECMDGOALS),clean)
$(warning The SOEM sources have not been found in $(SOEMHOME).)
$(warning Please read the file addon/soem/README for instructions!)
$(error SOEM sources not found)
endif
endif

# the SOEM library sources (see CMakeLists.txt of SOEM)
SOEMSRCS = $(SOEMHOME)/src/ec_base.c \
	   $(SOEMHOME)/src/ec_coe.c \
	   $(SOEMHOME)/src/ec_config.c \
	   $(SOEMHOME)/src/ec_dc.c \
	   $(SOEMHOME)/src/ec_eoe.c \
	   $(SOEMHOME)/src/ec_foe.c \
	   $(SOEMHOME)/src/ec_main.c \
	   $(SOEMHOME)/src/ec_print.c \
	   $(SOEMHOME)/src/ec_soe.c

# the Circle port of the OS and hardware abstraction layers
PORTOBJS = osal.o oshw.o nicdrv.o

OBJS	= $(PORTOBJS) $(SOEMSRCS:.c=.o)

EXTRACLEAN = $(SOEMSRCS:.c=.o) $(SOEMSRCS:.c=.d)

libsoem.a: $(OBJS)
	@echo "  AR    $@"
	@rm -f $@
	@$(AR) cr $@ $(OBJS)

include $(CIRCLEHOME)/Rules.mk

INCLUDE	+= -I . -I include -I $(SOEMHOME)/include -I $(SOEMHOME)/osal

# SOEM uses snprintf(3), which is provided by the Circle port. Some toolchains
# enable _FORTIFY_SOURCE by default, which would redirect this call to a
# function of the C library, which is not linked into Circle applications.
DEFINE	+= -U_FORTIFY_SOURCE

# Build with "make EC_DEBUG=1" to write the internal debug messages of SOEM to
# the Circle system log (with the log level LogDebug).
EC_DEBUG ?= 0

ifeq ($(strip $(EC_DEBUG)),1)
DEFINE	+= -DEC_DEBUG
endif

-include $(DEPS)
