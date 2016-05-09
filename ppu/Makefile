################
# Subdirectories
################

DIRS := ../spu

################
# Target
################
PROGRAM_ppu := tema3

################
# Local Defines
################

IMPORTS := ../spu/lib_dbuff_spu.a -lspe2 -lpthread -lmisc
CPPFLAGS_gcc := -Werror

# imports the embedded simple_spu library
# allows consolidation of spu program into ppe binary

################
# make.footer
################

# make.footer is in the top of the SDK
ifdef CELL_TOP
    include $(CELL_TOP)/buildutils/make.footer
else
    include ../../../../buildutils/make.footer
endif

run:
	./tema3 2 0 0 e ../../serial/in1 ../../serial/key1 out1.txt
