##############################################################################
# Dread Bass - microKORG 2 oscillator unit
#
# The DSP lives in ../core/dreadbass.h (shared with the desktop tests).
# No SDK tables are needed: pitch, sines and pulses are computed directly.
#

PROJECT := DreadBass
PROJECT_TYPE := osc

CSRC = header.c

CXXSRC = unit.cc

ACSRC =
ACXXSRC =
ASMSRC =
ASMXSRC =

UINCDIR = $(PROJECT_ROOT)/../core

ULIBDIR =

ULIBS  = -lm
ULIBS += -lc

OPTIM = -O3

UDEFS =
