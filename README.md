# chipyard-linux-nexys

Run interactive Linux on Nexys Video FPGA (Artix-7 XC7A200T) with Chipyard's Rocket Core.

Companion code for the Zhihu articles:
- [Boot Linux on FPGA — Practice Guide](https://zhuanlan.zhihu.com/p/2022437802986468833)
- [Why Chipyard: A Shorter Path to Full-Stack RISC-V](https://zhuanlan.zhihu.com/p/2013942351115077134)

---

## Quick Start (~10 minutes)

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

## Full Build (compile from source)

For users who want to modify the kernel config or hardware design.

### Apply patches

```bash
# 1. OpenSBI patch (fix console getchar return value validation bug)
cd ~/chipyard/software/firemarshal/boards/prototype/firmware/opensbi
git apply /path/to/patches/opensbi-sbi_ecall-getchar-fix.patch

# 2. Linux kernel config
# Refer to patches/linux-hvc-console.diff and modify linux/.config accordingly

# 3. Configs.scala (optional, for synthesizing FastUART Bitstream yourself)
# Append patches/nexysvideo-fastuart-config.scala to:
# ~/chipyard/fpga/src/main/scala/nexysvideo/Configs.scala
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

---

## File Structure

```
├── uart_tsi_interactive/     # Interactive UART-TSI tool (original, not upstream)
│   ├── testchip_uart_tsi_interactive.h
│   ├── testchip_uart_tsi_interactive.cc
│   └── Makefile
├── patches/
│   ├── opensbi-sbi_ecall-getchar-fix.patch   # OpenSBI bug fix
│   ├── linux-hvc-console.diff                # Linux kernel config changes
│   └── nexysvideo-fastuart-config.scala      # Chipyard FastUART config
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
| Target board | Digilent Nexys Video (Artix-7 XC7A200T) |
