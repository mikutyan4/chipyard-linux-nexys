# Gemmini LLM Inference

Int8-quantized LLM inference accelerated by Gemmini systolic array on RISC-V + Linux.

Based on [llama2.c](https://github.com/karpathy/llama2.c) by Andrej Karpathy, modified to use Gemmini RoCC instructions for matrix multiplication.

## Files

| File | Description |
|------|-------------|
| `llama2_int8_gemmini.c` | Main inference code (Gemmini-accelerated, with profiling support) |
| `llama2_int8_x86.c` | x86 CPU version for verifying quantization accuracy |
| `quantize.py` | Offline quantization script (float32 → int8) |
| `Makefile` | Build for RISC-V (cross-compilation via Chipyard toolchain) |
| `setup.sh` | Download TinyStories 15M model and tokenizer |

## Quick Start

### 1. Download model

```bash
chmod +x setup.sh && ./setup.sh
```

This downloads `stories15M.bin` (~58MB) and `tokenizer.bin` (~424KB).

### 2. Quantize model (on x86)

```bash
python3 quantize.py stories15M.bin stories15M_int8.bin
```

### 3. Verify on x86

```bash
gcc -O3 -o llama2_int8_x86 llama2_int8_x86.c -lm
./llama2_int8_x86 stories15M_int8.bin -n 64 -i "Once upon a time"
```

### 4. Build for RISC-V

```bash
source ~/chipyard/env.sh
make CHIPYARD=~/chipyard llm llc
```

- `llm` — Gemmini-accelerated version with profiling
- `llc` — CPU-only version with profiling (for performance comparison)

### 5. Deploy to FPGA

Package into initramfs or copy to SD card. Use short filenames for SD card compatibility:

| Original | Short name |
|----------|------------|
| `llm` | `llm` |
| `llc` | `llc` |
| `stories15M_int8.bin` | `m.bin` |
| `tokenizer.bin` | `t.bin` |

### 6. Run on FPGA

```bash
# Greedy mode (fastest)
./llm m.bin -z t.bin -n 32 -t 0 -i "Once upon a time"

# Top-p sampling
./llm m.bin -z t.bin -n 32 -t 1.0 -p 0.9 -i "Once upon a time"

# CPU-only comparison
./llc m.bin -z t.bin -n 32 -t 0 -i "Once upon a time"
```

## Hardware Requirements

- FPGA with Gemmini-enabled Bitstream (see `patches/nexysvideo-gemmini-config.scala`)
- OpenSBI with `mstatus.XS` enabled (see `patches/opensbi-mstatus-xs.diff`)
- Linux kernel with `CONFIG_RISCV_ROCC=y` (see `patches/linux-rocc-support.diff`)

## Performance

Tested on Nexys Video (Artix-7 XC7A200T), Rocket Core + Gemmini 8x8, 50MHz:

| Version | Cycles (8 tokens) | Speedup |
|---------|-------------------|---------|
| CPU int8 | 1,633,777 | 1.0x |
| Gemmini int8 (initial) | 1,072,250 | 1.5x |
| Gemmini int8 (optimized) | 163,474 | **10.0x** |

MatMul-only speedup: **75x**. Overall speedup limited by CPU-bound operations (Attention, SiLU, Sampling).

## Quantization

Per-tensor symmetric int8 quantization with static activation calibration:

- **Weights**: quantized offline, per-layer scale factors
- **Activations**: calibrated with 5 sample sequences, 1.2x margin
- **Logits correlation**: 0.997 vs float32 (verified on x86)
