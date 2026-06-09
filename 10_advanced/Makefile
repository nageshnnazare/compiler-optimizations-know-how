include ../scripts/common.mk

# 01_pgo_demo.c has a main() so it's special: link an executable rather
# than just compiling to .o.
01_pgo_demo: 01_pgo_demo.c
	$(CC) $(CFLAGS) $< -o $@

# Skip the Polly-only example in the default `make all` since it might or
# might not be available.
SRCS := 01_pgo_demo.c
