# **FLAC\_AX Technical Architecture & Ecosystem Specification**

**Project:** FLAC\_AX — Deterministic Bit-Perfect Linux Audio Appliance & \[AX\] Audio/Visual Studio

**Ecosystem:** The Silicon Preservation Society (Piccolo \[AX\] / ax-sdk v1\_36\_1 / Generation-X-AX)

**Target Baseline:** Intel Core i5-9600K (6C/6T, HT Disabled), 32 GB DDR4-3600, NVIDIA GTX 980 Ti (6 GB GM200), HiBy FC3 (ESS Sabre ES9281AC Pro / ES9281PRO) USB DAC \+ Nobsound Q10 / Wharfedale Diamond 9.0 & Grado SR325x, Akai MPK Mini MK3 (MPK mini 3\) USB-MIDI Controller

## **1\. System Topology & ESS DAC Clock-Domain Isolation**

FLAC\_AX is engineered as a deterministic, zero-resampling Linux audio and audio-visual runtime where the HiBy FC3 (ESS Sabre ES9281AC Pro / ES9281PRO) acts as the hardware audio clock source over USB Audio Class 2.0 (UAC 2.0). Desktop mixers (PulseAudio or PipeWire) and software sample-rate conversion are bypassed (snd\_pcm\_hw\_params\_set\_rate\_resample(0)), and all streams—PCM, DoP (DSD64 / DSD128), and synchronized video (AudioMasterPtsSync)—follow the ESS DAC clock domain.

### **Physical Core Domain Mapping (2:3:1 Core Ratio)**

On the reference 6-core i5-9600K topology (coffee\_lake\_9600k in ax-sdk), FLAC\_AX enforces strict thread affinity aligned with ax::topology:

| Core Mask | Logical Core(s) | Domain Role | flac\_ax (Audiophile Mode) | ax\_av\_studio (A/V Studio Mode) | ax\_video\_upscaler (Video Upscaler) |
| :---- | :---- | :---- | :---- | :---- | :---- |
| 0x01 | Core 0 | ESS Audio Clock / IRQ | Pinned SCHED\_FIFO (80) ALSA snd\_pcm\_writei writer \+ USB xHCI IRQ locked to HiBy FC3 clock (S32\_LE, 44.1-384 kHz, DoP 176.4/352.8 kHz) | Pinned SCHED\_FIFO (80) ALSA snd\_pcm\_writei writer \+ USB xHCI IRQ (S32\_LE, 48 kHz, 96-frame period) | Pinned SCHED\_FIFO (80) ALSA snd\_pcm\_writei writer advancing AudioMasterPtsSync from committed ESS frames |
| 0x02 | Core 1 | Video / HUD | Idle (available for foreground workload) | Pinned VBlank-locked (SDL\_GL\_SetSwapInterval(1)) OpenGL Studio HUD on GTX 980 Ti (60/120 Hz) | Pinned Core1UpscaleStage (720p / 1080p / 2k presets, VBlank-paced against ESS AudioMasterPtsSync) |
| 0x1C | Cores 2-4 | Emulation / DSP | Reserved exclusively for foreground emulators (Eden, Cemu, Generation-X-AX) | FullFeatureSynthEngine (16 melodic voices \+ 8 drum voices, 2-pole SVF low-pass, StereoReverbDsp, TapeDelayDsp & 1024-pt Hann FFT) | Reserved / idle for foreground emulation or studio workloads |
| 0x20 | Core 5 | Housekeeping / I/O | Pinned SCHED\_OTHER FLAC/DSD decoder & RamFilePreloader (FLAC::Decoder::File / DsdDopStreamer) | Pinned SCHED\_OTHER MpkMini3Listener (snd\_rawmidi\_open non-blocking reader \+ off-RT MpkMini3OledFeedback) | Pinned V4l2LiveCaptureSource (storage\_aux\_mask \== 0x20ULL \-\> Core 5\) MMAP capture (YUYV / MJPEG) |

### **End-to-End Signal & Clock-Domain Diagram**

```
+-----------------------------------------------------------------------------+
|                CONTROL & STORAGE DOMAIN — CORE 5 (0x20)                     |
|                                                                             |
|  [Akai MPK Mini MK3] -> [MpkMini3Listener (ALSA RawMIDI)]                   |
|   Keys/Pads, K1-K8 (CC 70-77), Pitch/Mod, Sustain, OLED SysEx               |
|                               |                                             |
|                               v  Lock-Free SPSC Event Ring (1024)          |
|  [NVMe / EROFS toram] -> [RamFilePreloader] -> [FlacRingDecoder / DoP]      |
|  [V4L2 /dev/video0] ----> [V4l2LiveCaptureSource (MMAP YUYV/MJPEG)]         |
+-------------------------------|---------------------------------------------+
                                |
                                v
+-----------------------------------------------------------------------------+
|              DSP & MIXING DOMAIN — CORES 2-4 (0x1C / AVX2+FMA)              |
|                                                                             |
|  [FullFeatureSynthEngine: 16 Melodic + 8 Drum + SVF + Reverb + Delay]       |
|   + 1024-pt Hann FFT Tap                                                    |
|          |                                          |                       |
|          v Lock-Free StereoFrameRing                v Double-Buffered       |
|       (S32_LE PCM / DoP)                          VisualSnapshot          |
+----------|------------------------------------------|-----------------------+
           |                                          |
           v                                          v
+-----------------------------------+   +-------------------------------------+
| ESS DAC CLOCK DOMAIN — CORE 0     |   | VIDEO CLOCK DOMAIN — CORE 1 (0x02)  |
| (0x01 / SCHED_FIFO 80)            |   |                                     |
|                                   |   | [GTX 980 Ti OpenGL HUD / Upscaler]  |
| [ALSA Direct hw:CARD=FC3,DEV=0]   |   |  - VBlank FIFO Lock (SwapInterval 1)|
|  - S32_LE, 2ch, Rate-Resample = 0 |   |  - 720p / 1080p / 2k + Bob/Cubic    |
|  - 96/128-frame + EssHardwareMixer|   |  - AudioMasterPtsSync Frame Pacing  |
|  - AudioMasterPtsSync Master Clock|   |                 |                   |
|                 |                 |   |                 v                   |
|                 v                 |   | [HDMI 2.0 / DP 1.2 Display Output]  |
| [HiBy FC3 / ESS ES9281AC Pro]     |   |  - Zero-tearing studio / upscaler   |
|  - Hardware Audio Clock (UAC 2.0) |   |                                     |
|  - Quad DAC / DoP (DSD64 / DSD128)|   |                                     |
+-----------------------------------+   +-------------------------------------+
```

## **2\. Memory, Storage & USB Bus Engineering**

### **Memory & Storage Topology (toram / tmpfs / mlockall)**

* **Process Page Locking (mlockall(MCL\_CURRENT \| MCL\_FUTURE)):** Attempted at startup in flac\_ax, ax\_av\_studio, and ax\_video\_upscaler (logging a warning if CAP\_IPC\_LOCK / RLIMIT\_MEMLOCK is not granted) to reduce page faults on real-time audio paths  
* **Adaptive RAM Preload Tiers (DetectHostPreloadPolicy() in src/preload\_policy.hpp):**  
  * WholeAlbum (\>= 16 GiB RAM, default on 32 GB rig): Loads tracks into RamFilePreloader up to an 8 GiB cap (8ULL \* 1024ULL \* 1024ULL \* 1024ULL)  
  * Window3Files (6.5 GiB to \< 16 GiB RAM): Maintains a sliding 3-track window in RAM (2 GiB cap), evicting completed entries behind current\_index  
  * SingleFile (\< 6.5 GiB RAM or \< 2 GiB MemAvailable): Restricts preloading to a 512 MiB cap  
* **Stream Decoding (FlacRingDecoder & DsdDopStreamer):** PersistentDecoderWorker refreshes the preload window on Core 5 (preloader.RefreshWindow(tracks\_, idx)), inspects preloader.Find(path), and routes .dsf / .dff tracks through DsdTrackSession / DsdDopStreamer (176.4 kHz DoP for DSD64, 352.8 kHz DoP for DSD128 via DopPacker::PackStereoFrame) or FLAC tracks through FlacRingDecoder into StereoFrameRing (262,144 frames)

### **Physical USB Host Controller Port Separation**

* **Dedicated Audio Bus (Core 0 IRQ):** Connect the HiBy FC3 (ES9281AC Pro / ES9281PRO) to a dedicated xHCI root port with USB runtime autosuspend disabled (power/control=on for HiBy VID 0x32BB)  
* **Isolated Control / Capture Bus (Core 5):** Keep the Akai MPK Mini MK3 or V4L2 capture device (V4l2LiveCaptureSource) on its own port/bus so control and capture I/O do not contend with audio submission

## **3\. Internal Subsystems (src/ & CMakeLists.txt)**

1. **Build Targets (CMakeLists.txt):** Builds flac\_ax (src/main.cpp) plus optional ax\_av\_studio (FLAC\_AX\_BUILD\_AV\_STUDIO=ON) and ax\_video\_upscaler (FLAC\_AX\_BUILD\_VIDEO\_UPSCALER=ON), linking ax::core (ax-sdk v1\_36\_1 via find\_package(ax-sdk) with FetchContent fallback), FLAC++ / FLAC, asound, and optional SDL2 \+ OpenGL (FLAC\_AX\_ENABLE\_SDL2\_GL / FLAC\_AX\_HAS\_SDL2\_GL)  
2. **ESS Sabre Hardware Hooks, DoP Packer & Mixer (src/ess\_dac\_hooks.hpp & src/dsd\_dop\_reader.hpp):**  
   * Resolves ES9281AC Pro and ES9281PRO profiles (MakeEssProfile, ResolveEssChipHook), recommended load volumes (RecommendedVolumeForTarget), sample rate negotiation (NegotiateEssRate within \[44100 Hz, 384000 Hz\]), and buffer period counts (RecommendedBufferPeriods: 8 at \>= 176.4 kHz, 4 otherwise)  
   * Provides DopPacker (0x05 / 0xFA markers, PackStereoFrame, PackStereoInterleaved) and ParseDsdMemoryHeader / ParseDsdFileHeader / DsdDopStreamer for .dsf and .dff streams  
   * Provides EssHardwareMixer (--volume / \--mixer-card) to control ALSA playback volume on the target card (FC3 default)  
3. **Akai MPK Mini MK3 ALSA RawMIDI & OLED Feedback (src/midi/mpk\_mini3.hpp, .cpp & src/midi/mpk\_mini3\_oled.hpp):**  
   * Auto-discovers MPKmini3 in /proc/asound/cards (hw:CARD=MPKmini3,DEV=0), reads non-blocking ALSA RawMIDI on Core 5, parses running status \+ SysEx framing (0xF0..0xF7), and pushes events into a 1,024-entry lock-free SPSC queue  
   * MpkMini3OledFeedback rate-limits (45 ms default interval) off-RT parameter/SysEx feedback for K1-K8 (CC 70-77)  
4. **Synthesizer & Effects DSP (src/dsp/full\_feature\_synth.hpp, src/dsp/stereo\_reverb\_dsp.hpp, src/dsp/tape\_delay\_dsp.hpp & src/dsp/avx2\_synth.hpp/.cpp):**  
   * FullFeatureSynthEngine (used by studio\_main.cpp) runs 16 melodic voices \+ 8 drum voices, a 2-pole SVF low-pass filter \+ soft saturation, StereoReverbDsp, TapeDelayDsp, and a 1024-point Hann-windowed radix-2 FFT \+ 256-sample stereo scope into double-buffered VisualSnapshot slots  
   * PolySynthEngine (src/dsp/avx2\_synth.hpp/.cpp) provides the standalone 16-voice saw+sub \+ 2-pole SVF \+ 1024-pt FFT engine  
5. **720p/1080p/2K Video Upscaler & Audio-Master Sync (src/video\_upscaler\_main.cpp & src/video/\*.hpp):**  
   * Resolves hardware guardrails via Resolve720pGuardrails (5.50 GiB VRAM ceiling, 5.00 GiB reap trigger, 0x20 storage/aux mask)  
   * Captures live retro video on Core 5 via V4l2LiveCaptureSource (YUYV / MJPEG MMAP) or paces DVD streams, paces frames via AudioMasterPtsSync (AudioMasterDvd / LiveLowLatency) against the ESS audio clock, and runs Core1UpscaleStage on Core 1 with kUpscale720pFragmentShader (240p, bob, dvd scan modes)  
6. **Shared DAC Arbitration (src/studio\_main.cpp, src/main.cpp, src/video\_upscaler\_main.cpp):** Coordinates exclusive access to hw:CARD=FC3,DEV=0 via /run/lock/ax-dac.lock (flock(LOCK\_EX \| LOCK\_NB)) and SIGINT/SIGTERM/SIGUSR1 handlers

## **4\. Wider Silicon Preservation Society Repo Topology**

FLAC\_AX integrates with ax-sdk (v1\_36\_1), Piccolo-AX (batocera-ax-external), piccolo-ax-desktop, Generation-X-AX, Cemu-Latte-AX, and Eden-Espresso-Wave-AX across the Silicon Preservation Society ecosystem, sharing ax-sdk profiles/core topology and /run/lock/ax-dac.lock DAC arbitration.

## **5\. Current Status & Next Steps**

### **Delivered in Current Tree**

* Core audio (src/main.cpp), studio synth/MIDI/OLED runner (src/studio\_main.cpp), and 720p/1080p/2K video upscaler runner (src/video\_upscaler\_main.cpp) wired in CMakeLists.txt with FLAC\_AX\_ENABLE\_SDL2\_GL / FLAC\_AX\_HAS\_SDL2\_GL  
* Reconciled DopPacker::PackStereoFrame (src/ess\_dac\_hooks.hpp) with DsdDopStreamer (src/dsd\_dop\_reader.hpp) and RamFilePreloader windowing (src/preload\_policy.hpp) in src/main.cpp  
* src/midi/mpk\_mini3.\*, src/midi/mpk\_mini3\_oled.hpp, src/dsp/full\_feature\_synth.hpp, src/dsp/stereo\_reverb\_dsp.hpp, src/dsp/tape\_delay\_dsp.hpp, src/dsp/avx2\_synth.\*, and src/video/\*.hpp

### **Remaining Fixes & Milestones**

1. **Harden V4L2 frame handoff & CLI validation:** Protect CapturedFrame slot copies in src/video/v4l2\_live\_capture.hpp against torn reads between Core 5 and Core 1, and replace std::stod / std::stoul in src/studio\_main.cpp with validated parsing  
2. **Complete Core1UpscaleStage shader/texture upload:** Compile kUpscale720pFragmentShader and bind CapturedFrame Y/UV textures in Core1UpscaleStage::RenderFrame instead of clearing to a solid color  
3. **Package in Piccolo-AX (batocera-ax-external):** Wire flac\_ax, ax\_av\_studio, and ax\_video\_upscaler into Buildroot and deploy udev rules for VID 0x32BB
