##############################################################################
# DreadMtr - Dread Bass with a CPU load meter in the MODE display (diagnostic)
# Same DSP and glue as microkorg2/ (unit.cc is shared), built with -DDREAD_LOADMETER.
#

PROJECT := DreadMtr
PROJECT_TYPE := osc

CSRC = header.c

CXXSRC = unit.cc

ACSRC =
ACXXSRC =
ASMSRC =
ASMXSRC =

UINCDIR = $(PROJECT_ROOT)/../../core

ULIBDIR =

ULIBS  = -lm
ULIBS += -lc

OPTIM = -O3

UDEFS = -DDREAD_LOADMETER
