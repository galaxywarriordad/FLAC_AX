# 🎛️ FLAC_AX & AX AV Studio

**Deterministic `[AX]` Bit-Perfect Audio Transport, Live Synthesizer Workstation & ESS Sabre Appliance for `Piccolo [AX]`**

`FLAC_AX` unites two zero-compromise C++20 real-time applications built around a deterministic **`2:3:1` physical CPU core topology** on `Linux 6.12-rt` (`Piccolo [AX]`) and standard `x86_64` Linux workstations:

1. **`flac_ax`** — A headless, bit-perfect high-resolution audio transport playing lossless **FLAC** (`PCM` up to `384 kHz / 32-bit`) and native **DSD64 / DSD128** (`.dsf` & `.dff`) via **DoP** (`176.4 kHz` / `352.8 kHz` over `S32_LE`) straight from locked RAM to an **ESS Sabre USB DAC** (`HiBy FC3` / `ES9281AC Pro`).
2. **`ax_av_studio`** — A `2.00 ms` (`96 frames @ 48 kHz`) real-time 16-voice polyphonic subtractive synthesizer, 8-voice percussion synthesizer, 4-comb stereo Schroeder reverb (`StereoReverbDsp`), damped ping-pong tape echo (`TapeDelayDsp`), and VBlank-locked 60 FPS OpenGL oscilloscope & 64-band FFT spectrum HUD driven by the **Akai MPK Mini MK3**.

---

## 🎯 What It Means For You

### 🎮 For Gamers
* **Zero-Contention Hardware Arbitration:** `FLAC_AX` respects `/run/lock/ax-dac.lock` (`flock`) and clean `SIGUSR1` / `SIGTERM` handoff—meaning your music player or synth rig immediately yields the ESS Sabre USB DAC when you launch an emulator or full-screen game, and reclaims it cleanly when you exit without ALSA `EBUSY` lockups.
* **Sub-2 ms Audio Discipline:** Built with the exact same `SCHED_FIFO` real-time scheduling, `nrpacks=1` USB isochronous packet tuning, and `2:3:1` core pinning required by competitive low-latency emulation on `Piccolo [AX]`, ensuring frame-paced gameplay and crisp transient audio never fight for CPU time.

### 🎧 For Audiophiles
* **100% RAM-Resident Playback (Zero Storage Bus Noise):** `flac_ax` preloads entire FLAC albums and native `.dsf`/`.dff` DSD masters into locked memory on Core 5 (`RamFilePreloader`) before playback starts, letting your storage bus go completely idle during listening.
* **True Bit-Perfect `S32_LE` & DoP Pipeline:** Bypasses PipeWire, PulseAudio, `dmix`, software volume scaling, and kernel resampling completely—feeding raw `32-bit` PCM (`44.1 kHz – 384 kHz`) and `0x05`/`0xFA`-packed `DSD64`/`DSD128` streams directly to the ESS Sabre `ES9281AC Pro` DAC chip.

### 🎹 For Musicians
* **Hardware-Feel `2.00 ms` Response:** Strike a key or pad on the **Akai MPK Mini MK3** and hear the result in `96` samples (`2.00 ms` at `48 kHz`)—faster than acoustic sound travels two feet from your studio monitors—backed by zero-allocation DSP and lock-free SPSC MIDI queues.
* **Complete Three-Engine Synth & FX Rig:** Combines a 16-voice dual-oscillator subtractive synth (sawtooth + `-1 octave` square sub-oscillator, 2-pole resonant SVF, ADSR envelopes, and cubic soft-clip drive) and an 8-voice synthesized drum machine with a 4-comb damped stereo reverb and `11%`-offset damped stereo tape delay—complete with a real-time 60 FPS oscilloscope and 64-band logarithmic FFT analyzer.

---

## 🏗️ `2:3:1` Physical Core Topology (`i5-9600K` / `x86_64`)

| Plane | CPU Core(s) | Scheduling | Role |
| :--- | :--- | :--- | :--- |
| **Audio Clock Domain** | **Core 0** (`0x01`) | `SCHED_FIFO` (Prio `80`) | Direct ALSA `hw:CARD=FC3,DEV=0` `S32_LE` output, USB xHCI IRQ affinity, zero-allocation audio loop. |
| **Video Clock Domain** | **Core 1** (`0x02`) | Standard / VSync | VBlank-locked OpenGL Studio HUD (256-sample stereo scope, 64-band log FFT, MIDI matrix) + rate-limited OLED SysEx flush. |
| **DSP & Synthesis** | **Cores 2–4** | Worker / Inline | 16-voice saw/sub SVF synth, 8-voice drum synth, `StereoReverbDsp`, `TapeDelayDsp`, and 1024-pt Radix-2 FFT. |
| **Control & Storage** | **Core 5** (`0x20`) | Non-RT Background | Non-blocking ALSA RawMIDI listener (`MpkMini3Listener`), `RamFilePreloader`, and FLAC/DSD stream decoding (`PersistentDecoderWorker`). |

---

## 🧰 Hardware Baseline

* **CPU / Host:** 6-core `x86_64` desktop processor (e.g., Intel Core `i5-9600K` / `i5-8600K`) with `16–32 GB` RAM running Linux (`Piccolo [AX]` `6.12-rt` or desktop Linux)
* **DAC:** **HiBy FC3** (`VID 0x32BB`, ESS Sabre `ES9281AC Pro` / `ES9281PRO`)
* **Controller (Optional for `ax_av_studio`):** **Akai MPK Mini MK3** (25 keys, 16 velocity pads, XY thumbstick, 8 rotary encoders K1–K8)

---

## 🛠️ Quick Build & Run

### 1. Install Dependencies (Ubuntu / Debian / Kubuntu)
```bash
sudo apt update
sudo apt install -y \
  build-essential clang lld cmake ninja-build pkg-config \
  libasound2-dev libflac-dev libflac++-dev libsdl2-dev libgl-dev
```

### 2. Configure & Compile (`-j 4` L3 Cache Discipline)
```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_AV_STUDIO=ON \
  -DFLAC_AX_NATIVE_ARCH=OFF \
  -DFLAC_AX_ENABLE_LTO=ON

ninja -C build -j 4
```

### 3. Run
```bash
# Headless bit-perfect FLAC / DSD DoP playback
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 /music/album/

# Live 2.0 ms AV Synthesizer & OpenGL Spectrum Studio
./build/ax_av_studio --fullscreen --device hw:CARD=FC3,DEV=0 --volume 0.80
```

---

## 📚 Documentation

* **[Complete User Guide & Build Manual](docs/FLAC_AX_AVStudio_Complete_User_Guide.md)** — Udev low-latency rules (`VID 0x32BB`, `nrpacks=1`), `/run/lock/ax-dac.lock` arbitration, CLI flags, Akai MPK Mini MK3 CC mappings, and CMake/Ninja build reference.
* **[Hi-Fi & Studio Enthusiast's Guide](docs/LAYMANS_GUIDE.md)** — Plain-English magazine-style overview for audiophiles and musicians covering the HiBy FC3, three-engine DSP trio, and hardware comparisons.
* **[Architecture Overview](docs/Architecture.md)** — Deep dive into the `2:3:1` thread topology, lock-free SPSC ring buffers, and memory-locking guarantees.
