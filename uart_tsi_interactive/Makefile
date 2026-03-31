# Makefile for uart_tsi_interactive
#
# Prerequisites: Chipyard toolchain installed, env.sh sourced
#
# Usage:
#   source ~/chipyard/env.sh
#   make CHIPYARD=~/chipyard
#
# Defaults to ~/chipyard; override with CHIPYARD=

CHIPYARD ?= $(HOME)/chipyard

SRCS = $(addprefix $(CHIPYARD)/generators/testchipip/src/main/resources/testchipip/csrc/, \
         testchip_tsi.cc testchip_htif.cc)

FESVR_SRC = $(CHIPYARD)/toolchains/riscv-tools/riscv-isa-sim

ifndef RISCV
$(error RISCV is not set. Please run 'source $(CHIPYARD)/env.sh' first)
endif

uart_tsi_interactive: testchip_uart_tsi_interactive.cc testchip_uart_tsi_interactive.h
	g++ -O3 -g \
		-L $(RISCV)/lib \
		-I $(CHIPYARD)/generators/testchipip/src/main/resources/testchipip/csrc \
		-I $(FESVR_SRC) \
		-I $(RISCV)/include \
		-std=c++17 \
		-o $@ \
		testchip_uart_tsi_interactive.cc $(SRCS) \
		-lfesvr -lpthread

clean:
	rm -f uart_tsi_interactive

.PHONY: clean
