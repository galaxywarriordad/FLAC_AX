# 🎛️ FLAC\_AX — Deterministic Bit-Perfect Linux Audio Appliance & `[AX]` Audio/Visual Studio

## **FLAC\_AX** is a zero-resampling, RAM-resident C++20 audio, live synthesis, video-upscaling, and appliance-launcher runtime engineered for **Piccolo $AX$** (`Linux 6.12-rt` / Buildroot) and standard `x86_64` Linux workstations (`Ubuntu/Kubuntu 24.04+`) within **The Silicon Preservation Society** ecosystem.

By bypassing user-space audio servers (`PipeWire` / `PulseAudio` / `dmix`), locking decoded lossless albums and native DSD streams into physical RAM (`mlockall`), and enforcing a strict **`2:3:1` physical CPU core topology** (`coffee_lake_9600k` baseline), `FLAC_AX` turns preserved multi-core `x86_64` PCs paired with a **£29–£35 UAC 2.0 USB-C DAC** (**HiBy FC3**, **TRN Black Pearl**, **FiiO KA11**, or **Moondrop Dawn Pro 2**) into four deterministic bare-metal appliances in one unified codebase.

---

## 🏗️ 1\. Suite Binaries & `2:3:1` Physical Core Topology

On the reference 6-core Intel Core `i5-9600K` (6C/6T, Hyper-Threading disabled, 32 GB DDR4-3600, NVIDIA GTX 980 Ti 6 GB GM200) topology, `FLAC_AX` isolates real-time audio clock submission from DSP synthesis, OpenGL rendering, and storage/MIDI/V4L2 I/O:

| Core Mask | Logical Core(s) | Domain Role | `flac_ax` (Audiophile Transport) | `ax_av_studio` (Live A/V Synth) | `ax_video_upscaler` (Retro & DVD Scaler) | `ax_appliance_tui` (Espresso Launcher) |
| :---- | :---- | :---- | :---- | :---- | :---- | :---- |
| **`0x01`** | **Core 0** | **ESS Audio Clock / IRQ** | `SCHED_FIFO` (`80`) direct ALSA `S32_LE` (`44.1–384 kHz`) & `DoP` (`DSD64` `176.4 kHz` / `DSD128` `352.8 kHz`) writer \+ xHCI USB DAC IRQ | `SCHED_FIFO` (`80`) ALSA `S32_LE` writer (`48 kHz`, `96`\-frame / `2.00 ms` period) \+ xHCI USB DAC IRQ | `SCHED_FIFO` (`80`) ALSA `S32_LE` writer advancing `AudioMasterPtsSync` master clock | Idle (`0%` CPU; zero interference with Core 0 RT audio) |
| **`0x02`** | **Core 1** | **Video & HUD Domain** | CLI status & signal handling (`SIGINT` / `SIGTERM` / `SIGUSR1`) | VBlank-locked (`SwapInterval 1`) GTX 980 Ti OpenGL Studio HUD (`64`\-band log FFT \+ `256`\-sample stereo scope \+ pad/encoder meters) | `Core1UpscaleStage` OpenGL 3.3 Core `Y`/`UV` (`GL_R8` \+ `GL_RG8`) `240p` / `Bob` / `DVD` shader paced via `AudioMasterPtsSync` | Idle (available for foreground workload) |
| **`0x1C`** | **Cores 2–4** | **Emulation & DSP Plane** | Reserved exclusively for foreground `[AX]` emulators (`Eden`, `Cemu`, `RMG`, `BlastEm`, `Cannonball`, `Frontier`, `Flycast`, `Hatari`) | `FullFeatureSynthEngine` (`16` saw+sub melodic voices \+ `8` drum voices \+ 2-pole SVF \+ cubic soft clipper \+ `StereoReverbDsp` \+ `TapeDelayDsp` \+ `1024`\-pt Hann FFT) | Reserved / idle for foreground emulation or studio workloads | Idle (isolated `nohz_full` domain untouched) |
| **`0x20`** | **Core 5** | **Housekeeping & I/O Plane** | `SCHED_OTHER` `RamFilePreloader` (`WholeAlbum` `8 GiB` / `Window3Files` `2 GiB` / `SingleFile` `512 MiB` or `64 MiB` on `<= 1.5 GiB` handhelds) + `PersistentDecoderWorker` | `SCHED_OTHER` `MpkMini3Listener` (non-blocking ALSA RawMIDI `1024`-slot SPSC ring) + `45 ms` `MpkMini3OledFeedback` + `WavBounceRecorder` | `V4l2LiveCaptureSource` (`MS2130S` `534d:2130` `YUYV`/`MJPEG` MMAP) with lock-free `FrameSlot` seqlock handoff | Pinned to Core `5`; renders 24-bit ANSI Espresso UI & probes `/run/lock/ax-dac.lock` + `/proc/meminfo` |


---

## ✨ 2\. Key Engineering Highlights

* **Bit-Perfect `S32_LE` & Native DoP (`DSD64` / `DSD128`) Transport (`src/main.cpp`, `src/ess_dac_hooks.hpp`, `src/dsd_dop_reader.hpp`):**  
  * Disables ALSA software resampling (`snd_pcm_hw_params_set_rate_resample(0)`), locks process pages in RAM (`mlockall(MCL_CURRENT | MCL_FUTURE)`), and streams 16-bit/24-bit PCM left-shifted cleanly to `S32_LE` alongside `.dsf` / `.dff` `DSD64` (`176.4 kHz`) and `DSD128` (`352.8 kHz`) packed with alternating `0x05` / `0xFA` DoP markers (`DopPacker`).  
  * Supports both **HiBy FC3 (`ES9281AC Pro`, `VID 0x32BB`)** output-stage revisions (`2.0 Vrms / 112 mW` `fc3_2v0` at `--volume 0.80` and early `1.3 Vrms / 70 mW` `fc3_1v3` at `--volume 0.95`), plus `EssHardwareMixer` ALSA hardware attenuator presets for Class-D desktop amps (`0.80` / `0.95` / `0.72`), `Grado SR325x` (`0.55`), `HeadphonesHighZ` (`0.70`), `HeadphonesClosed` (`0.50`), and `LowOhmIem` (`0.22`).  
* **Three-Engine `2.00 ms` Subtractive Synthesizer & Effects Studio (`ax_av_studio` — `src/studio_main.cpp`, `src/dsp/`, `src/midi/`, `src/visual/`):**  
  * Zero-allocation real-time audio callback (`96` frames @ `48 kHz` \= `2.00 ms`) driven by **Akai MPK Mini MK3 (`MPKmini3`)** 25-key keybed, 16 velocity pads, pitch/mod thumbstick, sustain pedal (`CC 64`), and `K1–K8` rotary encoders (`CC 70–77`, plus `CC 78–79` reverb/delay), feeding a 4-tap `StereoReverbDsp`, `11%` stereo-offset `TapeDelayDsp`, and `< 64 MB` VRAM VBlank-locked OpenGL Studio HUD.  
* **Single-Stream `720p` / `1080p` / `2K` Retro & DVD Upscaler (`ax_video_upscaler` — `src/video/`):**  
  * Captures uncompressed `YUYV 4:2:2` (or `MJPEG`) from USB 3.0 HDMI capture dongles (**MacroSilicon MS2130S**) on Core `5` into double-buffered `FrameSlot` entries guarded by per-slot atomic sequence counters (`std::atomic<uint64_t> seq`), preventing torn reads on Core `1`.  
  * Uploads planar `Y` (`GL_R8`) and `UV` (`GL_RG8`) textures into an OpenGL 3.3 Core fullscreen triangle shader (`kUpscale720pFragmentShader`) supporting `240p` progressive passthrough, `480i`/`576i` single-field `Bob` deinterlacing, `DVD` adaptive cadence, anti-ringing clamping, and `AudioMasterPtsSync` clock locking.  
* **Core `5` Coffee/Espresso Appliance TUI & Exclusive DAC Arbitration (`ax_appliance_tui` — `src/tui/`):**  
  * Zero-dependency 24-bit ANSI TrueColor launcher matching the `Piccolo-AX` Espresso theme (`#0B0806` canvas, `#C88A55` coffee accent, `#F4EDE4` cream text), with live `/run/lock/ax-dac.lock` (`flock`) status, `/proc/meminfo` RAM telemetry, CPU thermal readout, and clean handoff to `piccolo-ax-desktop`.

---

## 🛠️ 3\. Quick Build (`CMake` \+ `Ninja`)

Build Dependencies (Ubuntu / Debian / Buildroot)

sh

```
sudo apt-get update && sudo apt-get install -y --no-install-recommends \
  cmake ninja-build pkg-config \
  libflac++-dev libasound2-dev libopenmpt-dev libsdl2-dev libgl1-mesa-dev
```

Configure & Compile (`-j 4` L3 Cache Discipline)

sh

```
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFLAC_AX_USE_AX_SDK=ON \
  -DFLAC_AX_BUILD_AV_STUDIO=ON \
  -DFLAC_AX_BUILD_VIDEO_UPSCALER=ON \
  -DFLAC_AX_BUILD_APPLIANCE_TUI=ON \
  -DFLAC_AX_ENABLE_SDL2_GL=ON \
  -DFLAC_AX_SKYLAKE_ARCH=ON \
  -DFLAC_AX_ENABLE_LTO=ON

ninja -C build -j 4
```

*(Note: When `ax-sdk` is not pre-installed locally via `AX_SDK_DIR` or `../ax-sdk`, CMake automatically fetches `ax-sdk` tag `v1_36_2` via `FetchContent`.)*

---

## 🎛️ 4\. Reference Execution & Multi-DAC Gain Staging

All audio-capable binaries acquire `/run/lock/ax-dac.lock` (`flock(LOCK_EX | LOCK_NB)`) and release the DAC cleanly on `SIGUSR1`, `SIGTERM`, or `SIGINT`.

sh

```
# 1. Speakers — 2.0 Vrms HiBy FC3 (112 mW) -> Nobsound Q10 (TPA3116) -> Wharfedale Diamond 9.0
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 ~/Music/FLAC_Album/*.flac

# 2. Speakers — Early 1.3 Vrms HiBy FC3 Revision (70 mW) -> Class-D Amp
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.95 ~/Music/FLAC_Album/*.flac

# 3. Speakers — TRN Black Pearl / Moondrop Dawn Pro 2 (2.0 Vrms) or FiiO KA11 (2.5 Vrms)
./build/flac_ax --device hw:CARD=BLACKPEARL,DEV=0 --mixer-card BLACKPEARL --volume 0.80 ~/Music/FLAC_Album/*.flac
./build/flac_ax --device hw:CARD=KA11,DEV=0 --mixer-card KA11 --volume 0.72 ~/Music/FLAC_Album/*.flac

# 4. Reference Open-Back Headphones (Grado SR325x, 38 Ohm) & High-Z Studio Cans (HD 560S, 120 Ohm)
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.55 ~/Music/FLAC_Album/*.flac
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.70 ~/Music/FLAC_Album/*.flac

# 5. Sensitive IEMs (16–32 Ohm, e.g., Moondrop Chu II / Truthear Hexa)
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.22 ~/Music/FLAC_Album/*.flac

# 6. Amiga / Atari ST Tracker Modules (.mod/.xm/.s3m/.it) via libopenmpt -> S32_LE
./build/flac_ax --device hw:CARD=FC3,DEV=0 --volume 0.80 --mod-stereo 75 ~/Music/Amiga/*.mod

# 7. Live 2.00 ms AV Synthesizer & OpenGL Studio HUD (+ Lock-Free 24-bit WAV Bounce)
./build/ax_av_studio --fullscreen --record ~/Music/jam.wav --device hw:CARD=FC3,DEV=0 --volume 0.80

# 8. Single-Stream Retro HDMI Capture Upscaler (MacroSilicon MS2130S -> 1080p Bob) & DVD 2K Upscaler
./build/ax_video_upscaler --mode retro --preset 1080p --v4l2 /dev/video0 --scan bob --device hw:CARD=FC3,DEV=0 --volume 0.80
./build/ax_video_upscaler --mode dvd --preset 2k --input /userdata/dvd/movie.iso --scan dvd --device hw:CARD=FC3,DEV=0 --volume 0.80

# 9. Core 5 Coffee/Espresso Appliance TUI Launcher
./build/ax_appliance_tui
```

---

## 📚 5\. Documentation

* **Technical Architecture & Ecosystem Specification** — `2:3:1` CPU core topology, ESS DAC clock-domain isolation, RAM preload tiers, lock-free V4L2 seqlock handoff, and OpenGL 3.3 `Y`/`UV` shader pipeline.  
* **Complete User Guide & Build Manual** — End-to-end build manual, commercial software/hardware comparison matrix, and gain-staged execution recipes.  
* **Bare-Metal Audio, AV & Hardware Master Guide (UK)** — Complete UK hardware blueprints (`£45–£554`), Class-D PSU electrical safety, multi-DAC `99-ax-usb-dacs.rules`, `6.12-rt` Linux setup, and Windows `FlexASIO.toml`.  
* **Sub-£50 USB-C DAC Silicon Shootout** — Circuit-level benchmark of **HiBy FC3 (`2.0 Vrms` & `1.3 Vrms`)**, **TRN Black Pearl**, **FiiO KA11**, **Moondrop Dawn Pro 2**, and **Apple USB-C (UK A2155)**.  
* **Enthusiast Guide to `FLAC_AX` & `AX AV Studio`** — Plain-English guide to whole-album RAM transport, the three-engine synth/reverb/tape-delay DSP chain, and **Akai MPK Mini MK3** control mapping.  
* **Transducer & Cabling Guide (Richer Sounds UK)** — Nearfield bookshelf speakers (`£99–£149`), wired headphones/IEMs (`£16–£299`), desk acoustics, and pre-terminated `£6.45` banana-plug speaker cables.  
* **Sprint Progress Report** — Detailed engineering log of completed deliverables and hardware validation gates.

---

## 🌐 6. Silicon Preservation Society Ecosystem

### Core Repositories (8)

1. **Shared Silicon SDK (GitHub):** [`galaxywarriordad/ax-sdk`](https://github.com/galaxywarriordad/ax-sdk.git) — Shared dependency-free C++20 deterministic hardware runtime (`ax::sdk` / `v1_36_2`), dynamic Intel SMT / AMD CCX topology resolver, platform shims, and master architecture specs.
2. **Linux USB Console & A/V Appliance (GitHub):** [`galaxywarriordad/Piccolo-AX`](https://github.com/galaxywarriordad/Piccolo-AX.git) — Standalone `< 300 MB` `toram` EROFS Batocera / Buildroot `Linux 6.12-rt` `x86_64` appliance (`batocera-ax-external`) with direct ALSA `SCHED_FIFO` and `iHD` VA-API `dma_buf`.
3. **Linux Handheld Appliance (GitHub):** [`galaxywarriordad/Piccolo-2go`](https://github.com/galaxywarriordad/Piccolo-2go.git) — Deterministic Buildroot `aarch64` (`RK3326` `1:2:1`) retro handheld OS for **BATLEXP G350** and **R36S**.
4. **Optional Desktop Tier (GitHub):** [`galaxywarriordad/piccolo-ax-desktop`](https://github.com/galaxywarriordad/piccolo-ax-desktop.git) — On-demand C/C++ LXQt/X11 desktop workbench (`< 42 MB` budget).
5. **Commodore Amiga Engine (GitHub):** [`galaxywarriordad/Amiberry-AX`](https://github.com/galaxywarriordad/Amiberry-AX.git) — Deterministic Commodore Amiga 500 / 1200 / CD32 engine linked against `ax-sdk`.
6. **Bare-Metal Audio & A/V Engine (GitHub):** [`galaxywarriordad/FLAC_AX`](https://github.com/galaxywarriordad/FLAC_AX.git) — RAM-resident bit-perfect audio player, `ax_av_studio` MIDI/DSP synth workstation, and `ax_video_upscaler` stream pipeline.
7. **Retro Multi-System Engine (GitHub):** [`galaxywarriordad/Generation-X-AX`](https://github.com/galaxywarriordad/Generation-X-AX.git) — Deterministic multi-console `[AX]` integration across `Flycast` and `RMG`.
8. **Wii U Engine (GitHub):** [`galaxywarriordad/Cemu-Latte-AX`](https://github.com/galaxywarriordad/Cemu-Latte-AX.git) — Deterministic Nintendo Wii U (Latte GPU) AX engine powered by `ax::sdk`.

### Sister & Downstream Forks

- **Main Switch Engine Fork (Forgejo):** [`galaxywarriordad/Eden-Espresso-Wave-AX`](https://git.eden-emu.dev/galaxywarriordad/Eden-Espresso-Wave-AX.git) — Deterministic Nintendo Switch fork tuned for `ax::sdk` core topology, Vulkan telemetry, and low-latency audio, kept aligned with upstream for frictionless PR cherry-picks.
- **Upstream Ecosystem Hub (Forgejo):** [`eden-emu/eden`](https://git.eden-emu.dev/eden-emu/eden/) — Upstream Eden Switch emulator reference repository.

---

## 📄 7\. License

Released under the MIT License.
