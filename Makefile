# Top-level Makefile: build every chapter that has its own Makefile.

CHAPTERS := \
    00_fundamentals/examples \
    01_local_optimizations \
    02_loop_optimizations \
    03_interprocedural \
    04_data_flow_analysis \
    05_control_flow \
    06_memory_optimizations \
    07_ssa_form \
    08_vectorization \
    09_register_allocation \
    10_advanced

.PHONY: all asm ll clean $(CHAPTERS)

all:
	@for d in $(CHAPTERS); do \
		echo "==> $$d"; \
		$(MAKE) -C $$d all || exit $$?; \
	done

asm:
	@for d in $(CHAPTERS); do \
		echo "==> $$d (asm)"; \
		$(MAKE) -C $$d asm || exit $$?; \
	done

ll:
	@for d in $(CHAPTERS); do \
		echo "==> $$d (ll)"; \
		$(MAKE) -C $$d ll || exit $$?; \
	done

clean:
	@for d in $(CHAPTERS); do \
		$(MAKE) -C $$d clean; \
	done
