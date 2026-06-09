include ../scripts/common.mk

# OpenMP simd pragmas need the OpenMP runtime header to be parsed; both
# Clang and GCC accept -fopenmp-simd which enables JUST the SIMD bits
# without dragging in the libgomp/libomp runtime.
CFLAGS += -fopenmp-simd
