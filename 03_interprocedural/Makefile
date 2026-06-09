include ../scripts/common.mk

.PHONY: lto_demo

lto_demo:
	@echo "== build without LTO =="
	$(CC) -O2 -c 07_lto.c     -o 07_lto.no-lto.o
	$(CC) -O2 -c 07_lto_mod.c -o 07_lto_mod.no-lto.o
	$(CC) -O2    07_lto.no-lto.o 07_lto_mod.no-lto.o -o 07_lto.no-lto.out
	@echo "  → callee_no_lto disassembly:"
	@objdump -d 07_lto.no-lto.out 2>/dev/null | awk '/<_*callee_no_lto>:/{f=1} f{print; if(/ret/){exit}}'
	@echo
	@echo "== build with LTO =="
	$(CC) -O2 -flto -c 07_lto.c     -o 07_lto.lto.o
	$(CC) -O2 -flto -c 07_lto_mod.c -o 07_lto_mod.lto.o
	$(CC) -O2 -flto    07_lto.lto.o 07_lto_mod.lto.o -o 07_lto.lto.out
	@echo "  → callee_no_lto disassembly:"
	@objdump -d 07_lto.lto.out 2>/dev/null | awk '/<_*callee_no_lto>:/{f=1} f{print; if(/ret/){exit}}'
