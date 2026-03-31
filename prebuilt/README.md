# Prebuilt Files

Prebuilt binaries are distributed via GitHub Releases (too large to store in git).

Download the following files from the [Releases page](../../releases) and place them in this directory:

| File | Size | Description |
|------|------|-------------|
| `fw_payload.elf` | ~27MB | OpenSBI v1.2 + Linux 6.6.0 + Buildroot rootfs |
| `NexysVideoHarness_FastUART.bit` | ~20MB | Bitstream for RocketNexysVideoFastUARTConfig (921600 baud) |

## Build Info

| Component | Version |
|-----------|---------|
| Chipyard | v1.13.0 (commit 33182611) |
| OpenSBI | v1.2 (with sbi_ecall getchar fix) |
| Linux | 6.6.0 |
| Buildroot | 2024.02 |
| FPGA | Nexys Video (Artix-7 XC7A200T) |
| UART baud rate | 921600 bps |

## Login Credentials

- Username: `root`
- Password: `fpga`
