# 🎛️ FLAC_AX, AX AV Studio & AX Video Upscaler

**Deterministic `[AX]` Bit-Perfect Audio Transport, Live Synthesizer Workstation, Single-Stream A/V Upscaler & ESS Sabre Appliance for `Piccolo [AX]`**

`FLAC_AX` unites three zero-compromise C++20 real-time applications built around a deterministic **`2:3:1` physical CPU core topology** on `Linux 6.12-rt` (`Piccolo [AX]`) and standard `x86_64` Linux workstations:

1. **`flac_ax`** — A headless, bit-perfect high-resolution audio transport playing lossless **FLAC** (`PCM` up to `384 kHz / 32-bit`) and native **DSD64 / DSD128** (`.dsf` & `.dff`) via **DoP** (`176.4 kHz` / `352.8 kHz` over `S32_LE`) straight from locked RAM to an **ESS Sabre USB DAC** (`HiBy FC3` / `ES9281AC Pro`).
2. **`ax_av_studio`** — A `2.00 ms` (`96 frames @ 48 kHz`) real-time 16-voice polyphonic subtractive synthesizer, 8-voice percussion synthesizer, 4-comb stereo Schroeder reverb (`StereoReverbDsp`), damped ping-pong tape echo (`TapeDelayDsp`), and VBlank-locked 60 FPS OpenGL oscilloscope & 64-band FFT spectrum HUD driven by the **Akai MPK Mini MK3**.
3. **`ax_video_upscaler`** — A deterministic, single-stream **720p / 1080p / 2K** (`1280x720`, `1920x1080`, `2560x1440`) DVD & live retro console HDMI upscaler (`MacroSilicon MS2130S` USB 3.0 UVC/UAC) featuring `240p` passthrough, `480i/576i` Bob deinterlacing, DVD adaptive cadence, anti-ringing `FP32` scaling within a `5.50 GiB` VRAM ceiling (`GTX 980 Ti`), and audio-master PTS synchronization (`AudioMasterPtsSync`) anchored to the ESS Sabre DAC.

---

## 🎯 What It Means For You

### 🎮 For Gamers & Retro Enthusiasts

* **Zero-Contention Hardware Arbitration:** `FLAC_AX` respects `/run/lock/ax-dac.lock` (`flock`) and clean `SIGUSR1` / `SIGTERM` handoff—meaning your music player, synth rig, or video upscaler immediately yields the ESS Sabre USB DAC when you switch modes or launch an emulator, and reclaims it cleanly when you exit without ALSA `EBUSY` lockups.
* **Sub-2 ms Audio & Low-Latency HDMI Capture:** Built with the exact same `SCHED_FIFO` real-time scheduling, `isolcpus` / `nohz_full` kernel isolation, and `2:3:1` core pinning required by competitive low-latency emulation on `Piccolo [AX]`. In `retro` mode, `ax_video_upscaler` pulls uncompressed `YUYV 4:2:2` (`MJPEG` fallback) frames over USB 3.0 from a **MacroSilicon MS2130S** on Core 5 (`MMAP` newest-frame handoff) and upscales to `720p`, `1080p`, or `2K` without queue lag.

### 🎧 For Audiophiles & Home Cinema Listeners

* **100% RAM-Resident Playback (Zero Storage Bus Noise):** `flac_ax` preloads entire FLAC albums and native `.dsf`/`.dff` DSD masters into locked memory on Core 5 (`RamFilePreloader`) before playback starts, letting your storage bus go completely idle during listening.
* **True Bit-Perfect `S32_LE`, DoP & Audio-Master DVD Sync:** Bypasses PipeWire, PulseAudio, `dmix`, software volume scaling, and kernel resampling completely—feeding raw `32-bit` PCM (`44.1 kHz – 384 kHz`) and `0x05`/`0xFA`-packed `DSD64`/`DSD128` streams directly to the ESS Sabre `ES9281AC Pro` DAC chip. For DVD playback (`ax_video_upscaler --mode dvd`), `AudioMasterPtsSync` anchors video presentation timestamps directly to the ESS Sabre `S32_LE` sample clock (with host-side 5.1-to-stereo downmix headroom ahead of the DAC).

### 🎹 For Musicians

* **Hardware-Feel `2.00 ms` Response:** Strike a key or pad on the **Akai MPK Mini MK3** and hear the result in `96` samples (`2.00 ms` at `48 kHz`)—faster than acoustic sound travels two feet from your studio monitors—backed by zero-allocation DSP and lock-free SPSC MIDI queues.
* **Complete Three-Engine Synth & FX Rig:** Combines a 16-voice dual-oscillator subtractive synth (sawtooth + `-1 octave` square sub-oscillator, 2-pole resonant SVF, ADSR envelopes, and cubic soft-clip drive) and an 8-voice synthesized drum machine with a 4-comb damped stereo reverb and `11%`-offset damped stereo tape delay—complete with a real-time 60 FPS oscilloscope and 64-band logarithmic FFT analyzer.

---

## 🏗️ `2:3:1` Physical Core Topology (`i5-9600K` / `x86_64`)

| Plane | CPU Core(s) | Scheduling | Role |
| :--- | :--- | :--- | :--- |
| **Audio Clock Domain** | **Core 0** (`0x01`) | `SCHED_FIFO` (Prio `80`) | Direct ALSA `hw:CARD=FC3,DEV=0` `S32_LE` output, USB xHCI IRQ affinity, zero-allocation audio loop, and `AudioMasterPtsSync` clock updates. |
| **Video Clock Domain** | **Core 1** (`0x02`) | Standard / VSync | VBlank-locked OpenGL Studio HUD (256-sample stereo scope, 64-band log FFT, MIDI matrix), rate-limited OLED SysEx flush, and `720p`/`1080p`/`2K` frame pacing & shader pass (`GTX 980 Ti`). |
| **DSP & Synthesis** | **Cores 2–4** | Worker / Inline | 16-voice saw/sub SVF synth, 8-voice drum synth, `StereoReverbDsp`, `TapeDelayDsp`, and 1024-pt Radix-2 FFT. |
| **Control & Storage** | **Core 5** (`0x20`) | Non-RT Background (`storage_aux`) | Non-blocking ALSA RawMIDI listener (`MpkMini3Listener`), `RamFilePreloader`, FLAC/DSD stream decoding (`PersistentDecoderWorker`), and `V4l2LiveCaptureSource` (`MS2130S` `MMAP` capture). |

---

## 🧰 Hardware Baseline (`[AX]` Reference Rig)

* **🖥️ CPU / Host:** 6-core `x86_64` desktop processor (e.g., Intel Core `i5-9600K` / `i5-8600K`) with `16–32 GB` RAM running Linux (`Piccolo [AX]` `6.12-rt` or desktop Linux)
* **🎮 GPU (`ax_av_studio` & `ax_video_upscaler`):** **NVIDIA GTX 980 Ti** (`6 GB` GDDR5, governed by `5.50 GiB` VRAM ceiling and `5.00 GiB` reap trigger in `ax-sdk` `hardware_profiles.json` `v2.1`)
* **🔌 Reference DAC:** **HiBy FC3** (`VID 0x32BB`, ESS Sabre `ES9281AC Pro`, PCM `S32_LE` & DoP) — *also supports **TRN Black Pearl** (`Dual CS43131` + on-board hardware PEQ)*
* **🔊 Reference Speakers & Amp:** **Nobsound Q10** (`TI TPA3116` Class-D stereo mini-amp + `19V–20V` PSU) driving **Wharfedale Diamond 9.0** front-ported bookshelf speakers via pre-terminated `£6.45` HiFi banana-plug speaker cables
* **🎧 Reference Open-Back Headphones:** **Grado SR325x** (`38 Ω`, CNC-machined alloy cups) driven directly from the HiBy FC3 (`--volume 0.50–0.65`)
* **📹 HDMI Capture (`ax_video_upscaler`, Optional):** **MacroSilicon MS2130S** (`VID:PID 534d:2130`, USB 3.0 `5000M` UVC `YUYV 4:2:2` / `MJPEG` + UAC `48 kHz` audio)
* **🎹 Controller (`ax_av_studio`, Optional):** **Akai MPK Mini MK3** (25 keys, 16 velocity pads, XY thumbstick, 8 rotary encoders K1–K8)

---

## 🛠️ Quick Build & Run

### 1. Install Dependencies (Ubuntu / Debian / Kubuntu)

```bash
sudo apt update
sudo apt install -y \
  build-essential clang lld cmake ninja-build pkg-config \
  libasound2-dev libflac-dev libflac++-dev libsdl2-dev libgl-dev v4l-utils
```

### 2. Configure & Compile (`-j 4` L3 Cache Discipline)

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_AV_STUDIO=ON \
  -DFLAC_AX_BUILD_VIDEO_UPSCALER=ON \
  -DFLAC_AX_NATIVE_ARCH=OFF \
  -DFLAC_AX_ENABLE_LTO=ON

ninja -C build -j 4
```

### 3. Run

```bash
# 1. Headless bit-perfect FLAC / DSD DoP playback (Nobsound Q10 + Diamond 9.0)
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 /music/album/

# 2. Live 2.0 ms AV Synthesizer & OpenGL Spectrum Studio
./build/ax_av_studio --fullscreen --device hw:CARD=FC3,DEV=0 --volume 0.80

# 3. Single-stream 720p / 1080p / 2K Retro HDMI Capture (MacroSilicon MS2130S)
./build/ax_video_upscaler --mode retro --preset 1080p --v4l2 /dev/video0 --scan bob --device hw:CARD=FC3,DEV=0 --volume 0.80

# 4. Single-stream 720p / 1080p / 2K DVD Upscaler (Audio-Master PTS Sync)
./build/ax_video_upscaler --mode dvd --preset 2k --input /media/dvd_title.iso --scan dvd --device hw:CARD=FC3,DEV=0 --volume 0.80
```

---

## 📚 Documentation

## 📚 Documentation

* **[Bare-Metal Audio, AV & Hardware Master Guide (UK)](docs/FLAC_AX_Bare-Metal_Audio_AV_Appliance_Guide.md)** — Complete UK hardware tiers anchored around the `[AX]` Reference Rig (`HiBy FC3` + `Nobsound Q10` + `Wharfedale Diamond 9.0` + `Grado SR325x`), Linux `6.12-rt` udev/WirePlumber rules, and Windows `FlexASIO.toml` setup.
* **[Complete User Guide & Build Manual](docs/FLAC_AX_AVStudio_Complete_User_Guide.md)** — `2:3:1` CPU core topology, `/run/lock/ax-dac.lock` arbitration, CLI flags, and CMake/Ninja build reference.
* **[Technical Architecture & Ecosystem Spec](docs/Architecture.md)** — Deep dive into clock-domain isolation, `mlockall` RAM preloading, `ax-sdk` (`v1_36_1`) integration, and internal DSP/HUD subsystems.
* **[Speakers, Headphones & Wiring Guide](docs/Speakers_Headphones_and_Wiring.md)** — Nearfield equilateral placement, boundary EQ compensation, and cabling guide.
* **[Hi-Fi & Studio Enthusiast's Guide](docs/Hi-Fi_and_Studio_Enthusiasts_Guide.md)** — Plain-English overview of whole-album RAM playback, bit-perfect ESS Sabre transport, and the `2.00 ms` three-engine synthesizer trio.
* **[USB DAC Silicon Reference](docs/DACS.md)** — Comparative silicon breakdown and USB hardware IDs for supported UAC 2.0 front-ends.
