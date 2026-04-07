# chipyard-linux-nexys

Run interactive Linux with Gemmini hardware acceleration on Nexys Video FPGA (Artix-7 XC7A200T) using Chipyard's Rocket Core.

Companion code for the Zhihu articles:
- [Why Chipyard: A Shorter Path to Full-Stack RISC-V](https://zhuanlan.zhihu.com/p/2013942351115077134)
- [Boot Linux on FPGA — Practice Guide](https://zhuanlan.zhihu.com/p/2022437802986468833)

Companion github book page:
- [Chipyard RISC-V Tutorial: From Environment Setup to LLM Inference on FPGA](https://mikutyan4.github.io/chipyard-linux-nexys/)

---

## Quick Start: Boot Linux (~10 minutes)

No Vivado synthesis or kernel compilation required.

**Step 1: Download prebuilt files**

Download `fw_payload.elf` and `NexysVideoHarness_FastUART.bit` from the [Releases page](../../releases) and place them in `prebuilt/`.

**Step 2: Build uart_tsi_interactive**

```bash
source ~/chipyard/env.sh
cd uart_tsi_interactive
make CHIPYARD=~/chipyard
```

**Step 3: Program the Bitstream**

Use Vivado Hardware Manager to program `prebuilt/NexysVideoHarness_FastUART.bit` (connect via PROG port).

**Step 4: Boot Linux**

Disconnect PROG port, connect UART port, and pass it through to WSL:

```powershell
# Windows PowerShell (Administrator)
usbipd attach --wsl --busid <BUSID>
```

```bash
# WSL
sudo chmod 666 /dev/ttyUSB0

# Press CPU_RESET on the Nexys Video, then immediately run:
./uart_tsi_interactive/uart_tsi_interactive \
    +tty=/dev/ttyUSB0 \
    +baudrate=921600 \
    +noecho \
    prebuilt/fw_payload.elf
```

Wait ~4 minutes for transfer. Log in with `root` / `fpga` when `buildroot login:` appears.

---

## Gemmini: Hardware-Accelerated LLM Inference

Run int8-quantized LLM inference on a Gemmini 8x8 systolic array attached to Rocket Core via RoCC.

See [`gemmini-llm/README.md`](gemmini-llm/README.md) for full instructions.

### Key results (Nexys Video, 50MHz)

| Version | Cycles (8 tokens) | Speedup |
|---------|-------------------|---------|
| CPU int8 | 1,633,777 | 1.0x |
| Gemmini int8 (optimized) | 163,474 | **10.0x** |

MatMul speedup: **75x**. Overall limited by CPU-bound operations (Amdahl's law).

### Gemmini setup

1. Append `patches/nexysvideo-gemmini-config.scala` to your `Configs.scala`
2. Apply `patches/opensbi-mstatus-xs.diff` (enable RoCC in OpenSBI)
3. Apply `patches/linux-rocc-support.diff` (enable `CONFIG_RISCV_ROCC`)
4. Synthesize Bitstream with `GemminiNexysVideoConfig`
5. Quantize model: `python3 gemmini-llm/quantize.py stories15M.bin model_int8.bin`
6. Build: `make -C gemmini-llm CHIPYARD=~/chipyard llm`
7. Package into initramfs or use SD card (`GemminiNexysVideoSDConfig`)

---

## Full Build: Compile from Source

For users who want to modify the kernel config or hardware design.

### Apply patches

```bash
# 1. OpenSBI: fix console getchar return value validation
cd ~/chipyard/software/firemarshal/boards/prototype/firmware/opensbi
git apply /path/to/patches/opensbi-sbi_ecall-getchar-fix.patch

# 2. OpenSBI: enable RoCC custom extensions (required for Gemmini)
# Apply patches/opensbi-mstatus-xs.diff manually (see file for instructions)

# 3. Linux kernel config: SBI console + RoCC support
# Refer to patches/linux-hvc-console.diff and patches/linux-rocc-support.diff

# 4. Configs.scala: FastUART and/or Gemmini configurations
# Append patches/nexysvideo-fastuart-config.scala to Configs.scala
# Append patches/nexysvideo-gemmini-config.scala for Gemmini support
```

### Build Linux image

```bash
# Build kernel
cd ~/chipyard/software/firemarshal/boards/prototype/linux
export ARCH=riscv
export CROSS_COMPILE=riscv64-unknown-linux-gnu-
make olddefconfig && make -j$(nproc) Image

# Package fw_payload.elf
cd ~/chipyard/software/firemarshal/boards/prototype/firmware/opensbi
make PLATFORM=generic \
     CROSS_COMPILE=riscv64-unknown-linux-gnu- \
     FW_PAYLOAD_PATH=../../linux/arch/riscv/boot/Image \
     -j$(nproc)
```

### Synthesize Bitstream

```bash
cd ~/chipyard && source env.sh

# Rocket-only (for Boot Linux articles)
make -C fpga SUB_PROJECT=nexysvideo CONFIG=RocketNexysVideoFastUARTConfig bitstream

# Rocket + Gemmini (for Gemmini articles)
make -C fpga SUB_PROJECT=nexysvideo CONFIG=GemminiNexysVideoConfig bitstream

# Rocket + Gemmini + SD card
make -C fpga SUB_PROJECT=nexysvideo CONFIG=GemminiNexysVideoSDConfig bitstream
```

---

## File Structure

```
├── uart_tsi_interactive/     # Interactive UART-TSI tool (custom, not upstream)
│   ├── testchip_uart_tsi_interactive.h
│   ├── testchip_uart_tsi_interactive.cc
│   └── Makefile
├── gemmini-llm/              # Gemmini-accelerated LLM inference
│   ├── llama2_int8_gemmini.c #   Main inference code
│   ├── llama2_int8_x86.c    #   x86 verification build
│   ├── quantize.py           #   Offline int8 quantization script
│   ├── Makefile              #   Cross-compilation for RISC-V
│   └── README.md
├── patches/
│   ├── opensbi-sbi_ecall-getchar-fix.patch   # OpenSBI console getchar bug fix
│   ├── opensbi-mstatus-xs.diff              # OpenSBI: enable RoCC extensions
│   ├── linux-hvc-console.diff                # Linux: SBI console config
│   ├── linux-rocc-support.diff              # Linux: RoCC user-space access
│   ├── nexysvideo-fastuart-config.scala      # Chipyard: 921600 baud UART
│   └── nexysvideo-gemmini-config.scala       # Chipyard: Gemmini + SD configs
└── prebuilt/                 # Prebuilt binaries (download from Releases)
    ├── fw_payload.elf
    └── NexysVideoHarness_FastUART.bit
```

## uart_tsi_interactive

Chipyard's stock `uart_tsi` does not support real-time interactive input — its main loop blocks on `sys_read()`, stalling UART processing and hiding target-side output. This tool uses a dedicated input thread with pipe-based stdin redirection, keeping the main UART loop running while forwarding user input to fesvr.

Supported options:

| Option | Description |
|--------|-------------|
| `+tty=/dev/ttyUSBx` | Serial device (required) |
| `+baudrate=N` | Baud rate, must match Bitstream config (default 115200) |
| `+noecho` | Disable local echo — required when running Linux (avoids double characters) |
| `+attach` | Skip program loading, attach to an already-running system |

## Version Info

| Component | Version |
|-----------|---------|
| Chipyard | v1.13.0 (commit 33182611) |
| OpenSBI | v1.2 |
| Linux | 6.6.0 |
| Gemmini | 8x8 systolic array, int8, Weight Stationary |
| Target board | Digilent Nexys Video (Artix-7 XC7A200T) |
