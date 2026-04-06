# Prerequisites: Set Up Your Dev Environment Right and Avoid 90% of Setup Headaches -- WSL2 + TUN Proxy + VSCode

---

## 0. Introduction

Whether you are getting into open-source chip design or AI algorithm development, the very first hurdle is almost always setting up the environment.

Environment setup looks like "just preparation before the real work begins," but it is far from a beginner-level task. Many people get stuck here for a month or more -- not because the steps are inherently difficult, but because online tutorials are scattered and inconsistent. You patch together advice from different sources, hit one pitfall after another, and eventually lose track of which step actually went wrong.

This article gives you a single, field-tested setup path: **WSL2 + TUN mode proxy + VSCode**. It works for both open-source chip development and AI work. Once configured, it eliminates the vast majority of environment and network issues you would otherwise face.

---

## 1. Why You Must Work on Linux

The mainstream open-source chip toolchains -- Chipyard, Verilator, OpenROAD, Yosys -- almost exclusively support Linux. The AI world is similar: CUDA toolchains and many deep-learning frameworks have far better support on Linux than on Windows.

Dual-booting Linux is an option, but switching back and forth between operating systems is painful, especially when you also need to do day-to-day office work. Running WSL1 or Cygwin on Windows solves some problems, but falls short when you need to compile complex toolchains.

WSL2 (Windows Subsystem for Linux 2) is the most practical solution available today. In short, it lets you run a full Linux system directly inside Windows -- no dual-boot, no virtual machine. Both systems run side by side with shared file access. Under the hood it runs a real Linux kernel, not an emulation layer, so the vast majority of Linux tools work out of the box with near-native performance.

You might ask, "Why not just use Docker?" -- Once WSL2 is set up, Docker Desktop can use WSL2 as its backend, and the two are fully compatible. If you ever need a containerized environment later, the transition is seamless.

---

## 2. Why You Need a TUN Mode Proxy

Dependencies for open-source toolchains are almost entirely hosted on GitHub and other servers outside China: cloning repositories, downloading toolchains, installing packages via pip/conda -- all of these require stable access to overseas networks.

Newcomers typically go down one of two dead-end paths when dealing with this:

**Configuring mirror sources.** This is the most common advice, but it is actually a trap. Different tools have different mirrors -- pip has its own, conda has its own, apt has its own, Maven has its own -- and each one must be configured separately. Mirrors are often out of sync, so you frequently run into missing versions or package conflicts. The more mirrors you add, the messier things get, and when something breaks, it is very hard to debug.

**Setting up port-based proxies.** This means manually configuring `http_proxy`, `https_proxy`, and similar environment variables for every tool. With many tools in play it is easy to miss one, and some tools do not even respect these variables, so downloads still fail.

From personal experience: enabling TUN mode on the host machine is by far the simplest approach. TUN mode intercepts all outbound traffic at the network layer, so every terminal command inside WSL2 -- git, curl, wget, conda, pip -- automatically goes through the proxy with zero additional configuration. It is a set-it-and-forget-it solution. You do not need to understand the underlying networking; just turn it on and it works.

---

## 3. Why VSCode Remote-WSL

Working directly in the WSL2 terminal is perfectly possible, but for large projects like Chipyard the pure-terminal experience is rough -- deep directory trees, searching and jumping around with nothing but the command line, and difficulty pinpointing issues when something goes wrong.

VSCode's Remote-WSL extension solves this problem. Once installed, VSCode connects directly into the WSL2 environment. The file tree, code completion, and integrated terminal all live in one window, giving you an experience identical to local development. The files you edit are the Linux files inside WSL2, and the terminal runs WSL2's bash -- there is no context-switching friction. On top of that, you can install the GitHub Copilot extension in VSCode; having AI assistance while writing scripts, editing config files, or debugging environment issues is a huge productivity boost and highly recommended.

---

## 4. Setup Steps

### 4.1 Enable the TUN Mode Proxy

**Get the proxy working first -- every download step that follows depends on it.**

On your Windows host, use a proxy client that supports TUN mode (e.g., Clash Verge Rev, Mihomo Party, etc.). Find the TUN mode toggle and enable it. The specifics of obtaining and configuring a proxy service are beyond the scope of this article; if you are unsure, ask a colleague or friend who has experience.

### 4.2 Install WSL2 and Ubuntu

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

Type `wsl` in PowerShell to enter the Ubuntu environment, then verify that network traffic is going through the proxy:

```bash
curl google.com
```

If you see HTML content in the response, the proxy is working. Next, update your packages:

```bash
sudo apt update && sudo apt upgrade -y
```

### 4.3 Install VSCode and the Remote-WSL Extension

On the Windows side, download and install [VSCode](https://code.visualstudio.com/). Once installed, open the Extensions Marketplace and search for and install **Remote - WSL** (the official Microsoft extension).

In the WSL2 terminal, navigate to your working directory and run:

```bash
code .
```

VSCode will automatically open in Remote-WSL mode. If the bottom-left corner shows "WSL: Ubuntu," the connection is successful.

While you are at it, consider installing the following extensions (install them on the WSL side; all optional):
- **C/C++** (IntelliSense and debugging support)
- **Scala (Metals)** (essential for Chisel/Scala development)
- **verilog-hdl** (Verilog syntax highlighting)

---

## 5. Closing Thoughts

At this point your development environment is essentially ready. Whether you are about to clone Chipyard, install a toolchain, or work on an AI project, this setup has you covered -- no more wasting time on environment issues.

One last personal note: environment setup is actually a core research skill, not a trivial chore you can brush off. Research frequently involves reproducing open-source work, and the main obstacle to reproduction is often not understanding the paper -- it is getting the code to run. Dependency version conflicts, toolchain incompatibilities, failed downloads... these are all environment problems. Building solid environment-setup skills is a prerequisite for efficiently reproducing and leveraging open-source work. Do not underestimate it.

Next up: **Chipyard Environment Setup -- Cloning, Initialization, and Toolchain Configuration**.
