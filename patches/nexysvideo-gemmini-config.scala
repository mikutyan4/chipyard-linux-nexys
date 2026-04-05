// Append to fpga/src/main/scala/nexysvideo/Configs.scala
//
// Gemmini systolic array accelerator configurations for Nexys Video FPGA.
// Requires Gemmini imports at the top of Configs.scala:
//   import gemmini._
//   import chisel3._
//
// Three configurations:
//   - WithSmallGemmini: 8x8 systolic array optimized for Artix-7 BRAM constraints
//   - GemminiNexysVideoConfig: Gemmini + Rocket Core + FastUART (no SD card)
//   - GemminiNexysVideoSDConfig: Gemmini + Rocket Core + FastUART + SD card

// Small Gemmini configuration optimized for Artix-7 FPGA resources
// 8x8 systolic array with reduced scratchpad/accumulator for LLM inference
class WithSmallGemmini extends Config((site, here, up) => {
  case BuildRoCC => up(BuildRoCC) ++ Seq(
    (p: Parameters) => {
      implicit val q = p
      val gemmini = LazyModule(new Gemmini(GemminiConfigs.defaultConfig.copy(
        // Reduce systolic array size from 16x16 to 8x8
        meshRows = 8,
        meshColumns = 8,

        // Reduce memory capacity to fit Artix-7 BRAM
        sp_capacity = CapacityInKilobytes(32),
        acc_capacity = CapacityInKilobytes(16),

        // Disable unused features to save resources
        has_training_convs = false,
        has_max_pool = false,

        // Use weight-stationary dataflow for inference
        dataflow = Dataflow.WS,

        // Reduce queue depths to save resources
        ld_queue_length = 4,
        st_queue_length = 2,
        ex_queue_length = 4,

        // Keep other optimizations from lean config
        acc_read_full_width = false,
        ex_read_from_acc = false,
        ex_write_to_spad = false
      )))
      gemmini
    }
  )
})

// Gemmini + Rocket on Nexys Video with fast UART
class GemminiNexysVideoConfig extends Config(
  new WithSmallGemmini ++
  new WithNexysVideoTweaksFastUART ++
  new chipyard.config.WithBroadcastManager ++ // no L2 cache
  new chipyard.config.WithSystemBusWidth(128) ++ // Required for Gemmini
  new chipyard.RocketConfig)

// ============================================================
// Gemmini + SD Card Configuration
// ============================================================

// SPI peripheral configuration (for SD card)
class WithNexysVideoSPI extends Config((site, here, up) => {
  case PeripherySPIKey => List(SPIParams(rAddress = BigInt(0x64001000L)))
})

// Tweaks with SD card support (based on FastUART version)
class WithNexysVideoTweaksWithSD(freqMHz: Double = 50) extends Config(
  new WithNexysVideoUARTTSI ++
  new WithNexysVideoDDRTL ++
  new WithNexysVideoSDCard ++  // Connect onboard SD card
  new WithNoDesignKey ++
  new testchipip.tsi.WithUARTTSIClient(BigInt(921600)) ++ // Fast UART
  new chipyard.harness.WithSerialTLTiedOff ++
  new chipyard.harness.WithHarnessBinderClockFreqMHz(freqMHz) ++
  new chipyard.config.WithUniformBusFrequencies(freqMHz) ++
  new chipyard.harness.WithAllClocksFromHarnessClockInstantiator ++
  new chipyard.clocking.WithPassthroughClockGenerator ++
  new chipyard.config.WithNoDebug ++
  new chipyard.config.WithNoUART ++
  new chipyard.config.WithTLBackingMemory ++
  new freechips.rocketchip.subsystem.WithExtMemSize(BigInt(512) << 20) ++
  new freechips.rocketchip.subsystem.WithoutTLMonitors)

// Gemmini + SD card complete configuration
class GemminiNexysVideoSDConfig extends Config(
  new WithSmallGemmini ++                       // 8x8 Gemmini accelerator
  new WithNexysVideoSPI ++                      // SPI controller
  new WithNexysVideoTweaksWithSD ++             // Tweaks with SD card
  new chipyard.config.WithBroadcastManager ++   // No L2 cache
  new chipyard.config.WithSystemBusWidth(128) ++ // Required for Gemmini
  new chipyard.RocketConfig)
