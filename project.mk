# Project definition for alcatraz.

## Tools.
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld

## Verbose mode.
V = 0
Q1 = $(V:1=)
QCC    = $(Q1:0=@echo '    CC   ' $@;)
QCXX   = $(Q1:0=@echo '    CXX  ' $@;)
QLD    = $(Q1:0=@echo '    LD   ' $@;)
QLINK  = $(Q1:0=@echo '    LINK ' $@;)
QCLEAN = $(Q1:0=@echo '    CLEAN';)
