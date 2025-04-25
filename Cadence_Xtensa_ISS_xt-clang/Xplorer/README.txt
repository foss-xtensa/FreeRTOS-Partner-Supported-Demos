This directory contains an example worksapce that builds the FreeRTOS library
and several examples.  It is FOR DEMO PURPOSES ONLY, and the source code
contained within it is not kept up to date.

The build process is slightly modified from the command-line Makefiles.
Makefile.include has been added to the FreeRTOS folder; this is a 
pre-build Makefile. It will build asm-offset.c and generate asm-offsets.h
in the build folder before building the FreeRTOS library. This file can
be modified as needed.

