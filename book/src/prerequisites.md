# Prerequisites: Development Environment Setup -- WSL2 + VSCode

---

## Introduction

Whether you are getting into open-source chip design or AI algorithm development, the very first hurdle is almost always setting up the environment.

Environment setup looks like "just preparation before the real work begins," but it is far from a beginner-level task. Many people get stuck here for a month or more -- not because the steps are inherently difficult, but because online tutorials are scattered and inconsistent.

This article gives you a single, field-tested setup path: **WSL2 + VSCode**. It works for both open-source chip development and AI work.

---

## 1. Why You Must Work on Linux

The mainstream open-source chip toolchains -- Chipyard, Verilator, OpenROAD, Yosys -- almost exclusively support Linux. The AI world is similar: CUDA toolchains and many deep-learning frameworks have far better support on Linux than on Windows.

Dual-booting Linux is an option, but switching back and forth between operating systems is painful. Running WSL1 or Cygwin on Windows solves some problems, but falls short when you need to compile complex toolchains.

**WSL2** (Windows Subsystem for Linux 2) is the most practical solution available today. It lets you run a full Linux system directly inside Windows -- no dual-boot, no virtual machine. Both systems run side by side with shared file access. Under the hood it runs a real Linux kernel, not an emulation layer, so the vast majority of Linux tools work out of the box with near-native performance.

You might ask, "Why not just use Docker?" -- Once WSL2 is set up, Docker Desktop can use WSL2 as its backend. If you ever need a containerized environment later, the transition is seamless.

---

## 2. Why VSCode Remote-WSL

Working directly in the WSL2 terminal is perfectly possible, but for large projects like Chipyard the pure-terminal experience is rough -- deep directory trees, searching and jumping around with nothing but the command line, and difficulty pinpointing issues when something goes wrong.

VSCode's **Remote-WSL** extension solves this. Once installed, VSCode connects directly into the WSL2 environment. The file tree, code completion, and integrated terminal all live in one window, giving you an experience identical to local development. The files you edit are the Linux files inside WSL2, and the terminal runs WSL2's bash -- there is no context-switching friction.

On top of that, you can install the **GitHub Copilot** extension in VSCode; having AI assistance while writing scripts, editing config files, or debugging environment issues is a huge productivity boost.

---

## 3. Setup Steps

### 3.1 Install WSL2 and Ubuntu

Open PowerShell as Administrator and run:

```powershell
wsl --install
```

This command automatically enables WSL2 and installs Ubuntu (the latest LTS version by default). Restart your computer after it finishes. On the first Ubuntu launch, set your username and password.

Verify that WSL2 is running correctly:

```powershell
wsl --list --verbose
```

Confirm that the VERSION column for Ubuntu shows 2.

Type `wsl` in PowerShell to enter the Ubuntu environment, then update your packages:

```bash
sudo apt update && sudo apt upgrade -y
```

### 3.2 Install VSCode and the Remote-WSL Extension

On the Windows side, download and install [VSCode](https://code.visualstudio.com/). Once installed, open the Extensions Marketplace and search for and install **Remote - WSL** (the official Microsoft extension).

In the WSL2 terminal, navigate to your working directory and run:

```bash
code .
```

VSCode will automatically open in Remote-WSL mode. If the bottom-left corner shows "WSL: Ubuntu," the connection is successful.

Recommended extensions (install on the WSL side, all optional):
- **C/C++** (IntelliSense and debugging support)
- **Scala (Metals)** (essential for Chisel/Scala development)
- **verilog-hdl** (Verilog syntax highlighting)

---

## 4. Closing Thoughts

At this point your development environment is ready. Whether you are about to clone Chipyard, install a toolchain, or work on an AI project, this setup has you covered.

One last note: environment setup is actually a core research skill, not a trivial chore. Research frequently involves reproducing open-source work, and the main obstacle to reproduction is often not understanding the paper -- it is getting the code to run. Dependency version conflicts, toolchain incompatibilities, failed downloads... these are all environment problems. Building solid environment-setup skills is a prerequisite for efficiently leveraging open-source work.

Next up: **Chapter 1 -- Chipyard Environment Setup and Toolchain Configuration**.
