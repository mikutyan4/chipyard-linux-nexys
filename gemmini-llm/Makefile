# Makefile for Gemmini-accelerated LLM inference
#
# This Makefile builds RISC-V Linux binaries using Chipyard's toolchain.
# Place this directory under: generators/gemmini/software/gemmini-rocc-tests/llm/
# Or set CHIPYARD to your Chipyard root directory.
#
# Targets:
#   llm  - Gemmini-accelerated int8 inference with profiling
#   llc  - CPU-only int8 inference with profiling (for comparison)
#   llama2_int8_gemmini - Gemmini version without profiling

CHIPYARD ?= $(HOME)/chipyard
GEMMINI_ROCC_TESTS ?= $(CHIPYARD)/generators/gemmini/software/gemmini-rocc-tests

# RISC-V cross compiler (from Chipyard conda environment)
CC = $(CHIPYARD)/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc

# Gemmini headers
GEMMINI_INC = -I$(GEMMINI_ROCC_TESTS)

CFLAGS := -O2 -std=gnu99 -static -mcmodel=medany -ffast-math -DBAREMETAL=1 $(GEMMINI_INC)
LDFLAGS := -lm

.PHONY: all clean help

all: llm llc

# Gemmini-accelerated version with profiling
llm: llama2_int8_gemmini.c
	$(CC) $(CFLAGS) -DENABLE_PROFILING=1 $< -o $@ $(LDFLAGS)

# CPU-only version with profiling (for performance comparison)
llc: llama2_int8_gemmini.c
	$(CC) $(CFLAGS) -DENABLE_PROFILING=1 -DUSE_CPU_ONLY=1 $< -o $@ $(LDFLAGS)

# Gemmini version without profiling
llama2_int8_gemmini: llama2_int8_gemmini.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# x86 verification (native compiler)
llama2_int8_x86: llama2_int8_x86.c
	gcc -O3 -o $@ $< -lm

clean:
	rm -f llm llc llama2_int8_gemmini llama2_int8_x86

help:
	@echo "Targets:"
	@echo "  llm                 - Gemmini int8 with profiling (recommended)"
	@echo "  llc                 - CPU-only int8 with profiling (for comparison)"
	@echo "  llama2_int8_gemmini - Gemmini int8 without profiling"
	@echo "  llama2_int8_x86     - x86 native build (for verifying quantization)"
	@echo ""
	@echo "Quantize model (run on x86):"
	@echo "  python3 quantize.py stories15M.bin stories15M_int8.bin"
	@echo ""
	@echo "Verify on x86:"
	@echo "  make llama2_int8_x86"
	@echo "  ./llama2_int8_x86 stories15M_int8.bin -n 64 -i \"Once upon a time\""
	@echo ""
	@echo "Run on FPGA:"
	@echo "  make llm llc"
	@echo "  # Copy llm, llc, model, tokenizer to initramfs or SD card"
	@echo "  ./llm model.bin -z tokenizer.bin -n 32 -i \"Once upon a time\""
