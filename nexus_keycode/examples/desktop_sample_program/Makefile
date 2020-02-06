# Default build has minimal dependencies (just requires GCC)
#default skips scan-build, run `make all BUILD=sa` to enable static analysis
#
BUILD := default
# Use gnu11 instead of c99 for timer_t. We link in the RT library as well.
C_STANDARD = gnu11

# define compilation flags (includes, optimization, debug, warnings) here
INCLUDES = -Iinc -Isrc -I../.. -I../../include -I../../src -I../../include/common
WARNINGS.default := -Wall -Wextra -Wpedantic
WARNINGS.sa := $(WARNINGS.default) -Werror -Wno-unused-parameter -Wundef \
    -Wuninitialized -Winit-self -Wswitch-default -Wunreachable-code \
    -Wlogical-op -Winline -Wsuggest-attribute=const -Wconversion

CFLAGS = $(INCLUDES) -DDEBUG -O0 -g ${WARNINGS.${BUILD}} -std=$(C_STANDARD)

SCANBUILD_FLAGS = --status-bugs\
                  -enable-checker core\
                  -enable-checker deadcode\
                  -enable-checker alpha.core.BoolAssignment\
                  -enable-checker alpha.core.CallAndMessageUnInitRefArg\
                  -enable-checker alpha.core.CastSize\
                  -enable-checker alpha.core.CastToStruct\
                  -enable-checker alpha.core.FixedAddr\
                  -enable-checker alpha.core.IdenticalExpr\
                  -enable-checker alpha.core.PointerArithm\
                  -enable-checker alpha.core.PointerSub\
                  -enable-checker alpha.core.SizeofPtr\
                  -enable-checker alpha.core.TestAfterDivZero\
                  -enable-checker alpha.deadcode.UnreachableCode\
                  -enable-checker alpha.security.ArrayBoundV2\
                  -enable-checker alpha.security.ReturnPtrRange\
                  -enable-checker alpha.security.taint.TaintPropagation\
                  -analyze-headers\
                  -maxloop 40\
                  -o scanbuild-reports/

CC.sa := scan-build-6.0 $(SCANBUILD_FLAGS) gcc-9 $(CFLAGS)
CC.default := gcc $(CFLAGS)
CC = ${CC.${BUILD}}

# Include math and RT libraries for `floor` and `timer_t` functions
LDFLAGS = -lm -lrt

# this rule defines how to get .o files from .c files
%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

SOURCE_FILES := $(shell find src -name '*.c')

# These are the files from the Nexus Keycode library
LIB_SOURCE_FILES := $(shell find ../../src -name '*.c')
LIB_COMMON_SOURCE_FILES := $(shell find ../../include/common -name '*.c')

# take every c file, and replace the ".c" with ".o" (list of all object files)
SRC_OBJECT_FILES = $(SOURCE_FILES:.c=.o)
LIB_OBJECT_FILES = $(LIB_SOURCE_FILES:.c=.o)
LIB_COMMON_OBJECT_FILES = $(LIB_COMMON_SOURCE_FILES:.c=.o)

OBJECT_FILES = $(SRC_OBJECT_FILES) $(LIB_OBJECT_FILES) $(LIB_COMMON_OBJECT_FILES)

PROGRAM_NAME = sample_nexus_keycode_program

# link all the object files in the project
all: $(OBJECT_FILES)
	$(CC) -o $(PROGRAM_NAME) $(OBJECT_FILES) $(LDFLAGS)


# 'phony' target; always execute clean regardless of file state
.PHONY: clean

install-dependencies:
	sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
	sudo apt-get update -y
	sudo apt-get install gcc-9 -y
	sudo apt-get install clang-6.0 -y
	sudo apt-get install clang-tools-6.0 -y
	sudo apt-get install clang-tidy-6.0 -y
	sudo apt-get install clang-format-6.0 -y
	sudo apt-get install check -y

# Includes for stdbool.h and stddef.h
# Used in `tidy` below, adjust path as required.
CLANG_INCLUDE = -I/usr/lib/llvm-6.0/lib/clang/6.0.0/include

tidy:
	clang-tidy $(SOURCE_FILES) -checks=* -- $(INCLUDES) $(CLANG_INCLUDE)

format:
	$(foreach file, $(SOURCE_FILES), clang-format-6.0 -style=file -i $(file);)

clean:
	rm -rf $(OBJECT_FILES) $(LIB_OBJECT_FILES)
	rm -f $(PROGRAM_NAME)
