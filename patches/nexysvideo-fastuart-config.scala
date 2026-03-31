// Append to fpga/src/main/scala/nexysvideo/Configs.scala
//
// Increases UART-TSI baud rate from default 115200 to 921600 (8x).
// Reduces fw_payload.elf (~27MB) transfer time from ~32 min to ~4 min.
// Requires Vivado re-synthesis to generate a new Bitstream.

// Fast UART version with 921600 baud rate (8x faster than default 115200)
class WithNexysVideoTweaksFastUART(freqMHz: Double = 50) extends Config(
  new WithNexysVideoUARTTSI ++
  new WithNexysVideoDDRTL ++
  new WithNoDesignKey ++
  new testchipip.tsi.WithUARTTSIClient(BigInt(921600)) ++ // 921600 baud
  new chipyard.harness.WithSerialTLTiedOff ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(freqMHz) ++
  new chipyard.config.WithUniformBusFrequencies(freqMHz) ++
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithNoDebug ++
  new chipyard.config.WithNoUART ++ // UART is used for UART-TSI
  new chipyard.config.WithTLBackingMemory ++
  new freechips.rocketchip.subsystem.WithExtMemSize(BigInt(512) << 20) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors)

// RocketNexysVideoConfig with fast UART (921600 baud)
class RocketNexysVideoFastUARTConfig extends Config(
  new WithNexysVideoTweaksFastUART ++
  new chipyard.config.WithBroadcastManager ++
  new chipyard.RocketConfig)
