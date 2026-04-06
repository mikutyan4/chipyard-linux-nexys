# Conclusion: Doing Architecture Research on the Shoulders of Giants

We've reached the end. The entire series is now complete.

From setting up the environment, running simulations, and understanding the tapeout process, to booting Linux on an FPGA, and then integrating the Gemmini systolic array to run LLM inference -- you now have a **complete system spanning hardware, operating system, and accelerator applications**. If you followed along step by step, this journey was certainly not easy: all kinds of errors, unfamiliar concepts, debugging across multiple abstraction layers. But you made it through. Congratulations.

---

## I. Our Methodology

Looking back, what this entire series did can be summed up in one sentence: **assemble existing components, then focus on the things we actually care about.**

We used Rocket Core for the CPU, Linux for the operating system, Gemmini for the accelerator, and GCC for the compiler -- we didn't write any of them ourselves. What we did was configure them, wire them together, get them running on real hardware, and then run applications, do profiling, and optimize on top of that foundation.

The payoff of this approach is straightforward: in roughly one to two weeks, we completed the entire pipeline from environment setup to running LLM-accelerated inference on an FPGA -- including the ability to export tapeout-grade RTL design files.

I used to lean toward building everything from scratch myself. Then, while working on projects, I realized that things I spent ages implementing were often just a `git clone` away in the open-source community. After shifting my mindset, my productivity improved dramatically. This series is also meant to share that lesson -- you don't have to reinvent the wheel. **Optimizing and exploring on top of an already mature system can yield equally deep understanding**, and it drives you to read more cutting-edge papers and explore broader resources, actually widening your perspective.

The same philosophy applies to AI tools. Honestly, without AI assistance, it would have been very difficult for me to navigate the entirety of Chipyard on my own. The `getchar` return value validation bug in OpenSBI, the `hvc0` console configuration in the Linux kernel, the `full_C` parameter behavior in Gemmini -- these issues span multiple repositories and tens of thousands of lines of code. Debugging them manually is extremely inefficient, but describing the symptoms to AI lets you narrow down the scope quickly.

A tool is just a tool. Using AI is no fundamentally different from using an IDE or a search engine. **What matters is the problem you solved, not the tool you used to solve it.** If you haven't yet deeply used AI programming tools like Copilot, Claude Code, or Cursor, I'd recommend giving them a try -- especially for cross-layer system debugging scenarios like this one.

---

## II. What This Series Covered -- and What It Didn't

**What we covered**:
- Chipyard environment setup and toolchain
- Rocket Core simulation and HelloWorld
- Chipyard's role in the chip design flow
- Complete theory and practice of booting Linux on FPGA
- Gemmini systolic array principles and hardware configuration
- Accelerating LLM inference with Gemmini + profiling analysis

**What we didn't cover, but you can follow the same patterns for**:
- **BOOM** (out-of-order superscalar processor): used exactly the same way as Rocket -- just swap the Config
- **Other accelerators**: same RoCC deployment path as Gemmini -- write a Config, synthesize, set permissions, invoke
- **Other models and applications**: just write (or find) the source files, cross-compile, and package them into initramfs or an SD card
- **Tapeout backend**: Chipyard can export RTL, but the backend flow from RTL to GDS (synthesis, place-and-route, timing closure) is a separate topic entirely

At its core, what this series built is a **framework**. When you want to dive deeper into any particular part -- whether it's optimizing CPU microarchitecture, designing a new accelerator, or improving quantization algorithms -- you'll need domain-specific knowledge for that area, but the rest of the system can be reused directly without building from scratch.

---

## III. Why I Wrote This Series

Chipyard is an excellent open-source architecture research platform, continuously maintained by the Berkeley team for many years. But I noticed that the Chinese-language community never had a sufficiently complete tutorial -- one that could take you all the way from environment setup to running accelerator applications on an FPGA. Scattered posts exist, but stitching together the entire pipeline involves a tremendous number of pitfalls.

The original motivation for this series was to fill that gap: to create content that couldn't be found anywhere else online, but that would genuinely help people.

Halfway through writing, I actually hesitated, thinking maybe no one would read it and my time was already tight. It was the feedback and encouragement from readers in the comments section that kept me going until the content was complete. Thank you all.

---

## IV. Final Words

If you followed this series all the way through, what you now have is a **complete architecture experimentation platform** -- a configurable processor core, a customizable accelerator, Linux running on a real FPGA, and a verified software stack. Many researchers in computer architecture spend a long time building up a similar environment.

What you do next depends on your own research direction. Here are a few active and potentially fruitful areas on this platform: design space exploration of accelerator dataflows (WS/OS/hybrid), hardware-software co-designed quantization strategies (e.g., hardware-aware mixed-precision), scratchpad and DMA scheduling optimization, and end-to-end verification of custom RoCC accelerators. All of these can be pursued directly on the system we've built, without setting up the environment from scratch.

If you've hit new pitfalls during reproduction, adapted to a new FPGA board, or gotten a new model or application running, feel free to share in the comments or open an Issue or PR on the GitHub repo. The reason Chinese-language Chipyard resources are scarce is that many people encounter problems but few share their solutions -- a single tip from you could save someone else an entire day of debugging.

Best of luck.

> Companion code: [https://github.com/mikutyan4/chipyard-linux-nexys](https://github.com/mikutyan4/chipyard-linux-nexys)

---
