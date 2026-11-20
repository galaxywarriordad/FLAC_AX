# FLAC_AX

### A deterministic bare-metal Linux semi-pro audio player for `[AX]`, engineered for low-latency, zero-underrun playback harnessing the ESS Sabre ES9281PRO and ES9281AC Pro architecture.

`FLAC_AX` is a lean, deterministic bare-metal Linux semi-pro audio player built for the `[AX]` ecosystem, designed to harness the **ESS Sabre ES9281PRO** and **ES9281AC Pro** 32-bit HyperStream® II Quad DAC™ / Time Domain Jitter Eliminator architecture with minimal system overhead. Pairing non-real-time CPU decoding and lock-free ring-buffer transport with a `SCHED_FIFO` ALSA output path (`hw:...`), it targets bit-perfect high-resolution PCM playback (up to 32-bit/384 kHz) and DoP/DSD workflows while keeping hardware preservation and appliance simplicity front and center.

## Current Prototype Status & Architecture

### Repository & Build Overview
* **Repository Files:** `.gitattributes`, `.gitignore`, `CMakeLists.txt`, `LICENSE` (MIT), `README.md`, and `src/main.cpp`.
* **Build Setup:** CMake + Ninja configured for C++20, linking `flac++`, `alsa` (`asound`), and `pthread`, with an optional `-DFLAC_AX_USE_AX_SDK=ON` toggle (`FetchContent` targeting `ax-sdk` `v1_35_1c`).
* **Target Platform:** Bare-metal Linux driving ALSA directly (`hw:CARD=FC3,DEV=0` by default) for the HiBy FC3's ESS Sabre ES9281AC Pro DAC.

### Supported Audio Formats
* **Supported:** FLAC (`.flac`) via `FLAC::Decoder::File` (`libFLAC++`).
* **Sample & Channel Handling:** Reads `STREAMINFO` (sample rate, bits per sample, channel count) and converts decoded samples into 2-channel interleaved `S32_LE` (left-shifting samples up to 32 bits; duplicating mono to stereo for single-channel streams).
* **Not Yet Supported:** Additional codecs (`WAV`, `ALAC`, `Opus`, `Ogg Vorbis`, retro chip formats), playlists/queues, seeking, or DoP/DSD.

### Playback Pipeline
1. **Decoder Thread (Core 5, Non-RT):** Opens the `.flac` file, decodes frames into a reusable scratch buffer, converts samples to `S32_LE` stereo, and pushes chunks into a 64-byte-aligned SPSC stereo ring buffer (`65,536` frames—using `ax::audio::LockFreeStereoRing` when `ax-sdk` is enabled, or the built-in fallback ring otherwise).
2. **ALSA Configuration:** Reads the stream's sample rate from metadata and configures the target ALSA device (`hw:CARD=FC3,DEV=0` by default) for `SND_PCM_ACCESS_RW_INTERLEAVED`, `SND_PCM_FORMAT_S32_LE`, 2 channels, near-matched sample rate, and a configurable period size (default `96` frames, `4x` buffer).
3. **Real-Time Audio Output Thread (Core 0, `SCHED_FIFO` Priority 80):** Pulls frames from the ring buffer in `period_frames` chunks and writes them via `snd_pcm_writei`, handling `-EAGAIN` (`snd_pcm_wait`) and `-EPIPE` underruns (`snd_pcm_recover` with an `xruns` counter), then drains and closes cleanly on EOF or `SIGINT`/`SIGTERM`.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Enable `ax-sdk` (`v1_36`) via `FetchContent`

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLAC_AX_USE_AX_SDK=ON
cmake --build build
```

## Run

```bash
./build/flac_ax /path/to/track.flac hw:CARD=FC3,DEV=0
```
---

## 🔗 The Silicon Preservation Society — Official Repository Network

*(See `REPO_TOPOLOGY.md` for the full cross-host dependency diagram and the **Policy-in-SDK / Adapter-in-Consumer** boundary contract.)*

| Project | Role | Canonical Repository | Base / Upstream |
| :--- | :--- | :--- | :--- |
| **`ax-sdk`** | Shared C++20 Runtime Core (`libax_core`) | [GitHub (`ax-sdk`)](https://github.com/galaxywarriordad/ax-sdk) | Dependency-Free C++20 Core |
| **Eden Espresso Wave `[AX]`** | Nintendo Switch (Windows 10/11 PGO + Linux) | [Eden Forgejo (`Eden-Espresso-Wave-AX`)](https://git.eden-emu.dev/galaxywarriordad/Eden-Espresso-Wave-AX.git) | Upstream: [Eden](https://git.eden-emu.dev/eden-emu/eden/) |
| **Piccolo `[AX]`** | `< 300 MB` Bare-Metal Linux Console OS | [GitHub (`Piccolo-AX`)](https://github.com/galaxywarriordad/Piccolo-AX) | Base: [Batocera Linux](https://github.com/batocera-linux/batocera.linux) |
| **Cemu Latte `[AX]`** | Nintendo Wii U (Win10/11 PGO + Batocera `[AX]`) | [GitHub (`Cemu-Latte-AX`)](https://github.com/galaxywarriordad/Cemu-Latte-AX) | Upstream: [Cemu](https://github.com/cemu-project/Cemu) |
| **`FLAC_AX`** | Bare-Metal Linux ESS Sabre Audio Appliance | [GitHub (`FLAC_AX`)](https://github.com/galaxywarriordad/FLAC-AX) | Standalone `libFLAC++` + ALSA |

## License

MIT — see [LICENSE](LICENSE).
