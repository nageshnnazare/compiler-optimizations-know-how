# Shared Makefile rules. Include with:
#     include ../scripts/common.mk
#
# Each chapter's Makefile only needs to set SRCS := $(wildcard *.c).
# The default target builds an object file per source (no link), so functions
# remain externally visible and the optimizer can't dead-strip them.
#
# Targets:
#   make all     – compile each .c to a .o with -O3
#   make asm     – produce a .asm per .c (Intel syntax where supported)
#   make ll      – produce an LLVM .ll per .c (Clang only)
#   make clean

CC      ?= cc
CFLAGS  ?= -Wall -Wextra -std=c11 -O3
ASMOPTS ?= -S -fverbose-asm -fno-asynchronous-unwind-tables \
           -fomit-frame-pointer

# Intel syntax is much easier to read on x86; only request it when the target
# is actually x86, otherwise clang on arm64 complains the flag is unused.
TARGET_ARCH := $(shell $(CC) -dumpmachine 2>/dev/null)
ifneq (,$(findstring x86,$(TARGET_ARCH)))
INTEL_FLAG := -masm=intel
else
INTEL_FLAG :=
endif

SRCS ?= $(wildcard *.c)
OBJS := $(SRCS:.c=.o)
ASMS := $(SRCS:.c=.asm)
LLS  := $(SRCS:.c=.ll)

.PHONY: all asm ll clean

all: $(OBJS)

asm: $(ASMS)

ll: $(LLS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.asm: %.c
	$(CC) $(CFLAGS) $(ASMOPTS) $(INTEL_FLAG) $< -o $@

%.ll: %.c
	clang -O3 -S -emit-llvm $< -o $@

clean:
	rm -f $(OBJS) $(ASMS) $(LLS) *.s *.diff *.dot a.out
	rm -rf *.gcc-dumps
