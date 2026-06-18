# ----------------------------------------------------------------------
#  Quick Makefile (CMake is the primary build; this mirrors it 1:1).
#
#    make            # interpreter-only build  -> build/spt_demo
#    make JIT=1      # JIT build (links vendored MIR)
#    make clean
#
#  gnu11 is used so the computed-goto fast dispatch is available; conf.h
#  selects a switch fallback automatically on toolchains without it.
# ----------------------------------------------------------------------
CC      ?= cc
CSTD    ?= -std=gnu11
WARN    ?= -Wall -Wextra
OPT     ?= -O2
INC     := -Iinclude -Isrc
SPT_SRC := src/mem.c src/object.c src/state.c src/vm.c src/api.c \
           src/lexer.c src/parser.c src/codegen.c
MIR_DIR ?= third_party/mir

ifeq ($(JIT),1)
  DEFS    := -DSPT_JIT $(INC) -I$(MIR_DIR)
  JIT_SRC := src/jit/jit_mir.c
  MIR_OBJ := build/mir.o build/mir-gen.o
  LIBS    := -lm -ldl -lpthread
else
  DEFS    := $(INC)
  JIT_SRC :=
  MIR_OBJ :=
  LIBS    := -lm
endif

all: build/spt_demo build/spt_run

build:
	@mkdir -p build

# MIR is third-party C: GNU mode + explicit <stdarg.h>, warnings silenced.
build/mir.o: $(MIR_DIR)/mir.c | build
	$(CC) -O2 -std=gnu11 -include stdarg.h -I$(MIR_DIR) -w -c $< -o $@
build/mir-gen.o: $(MIR_DIR)/mir-gen.c | build
	$(CC) -O2 -std=gnu11 -include stdarg.h -I$(MIR_DIR) -w -c $< -o $@

build/spt_demo: $(SPT_SRC) $(JIT_SRC) examples/demo.c $(MIR_OBJ) | build
	$(CC) $(CSTD) $(WARN) $(OPT) $(DEFS) \
	  $(SPT_SRC) $(JIT_SRC) examples/demo.c $(MIR_OBJ) $(LIBS) -o $@

build/spt_run: $(SPT_SRC) $(JIT_SRC) examples/run_source.c $(MIR_OBJ) | build
	$(CC) $(CSTD) $(WARN) $(OPT) $(DEFS) \
	  $(SPT_SRC) $(JIT_SRC) examples/run_source.c $(MIR_OBJ) $(LIBS) -o $@

clean:
	rm -rf build

.PHONY: all clean
