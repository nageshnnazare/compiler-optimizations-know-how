include ../../scripts/common.mk

.PHONY: ast

ast: pipeline_demo.c
	clang -Xclang -ast-dump -fsyntax-only $< | head -40
