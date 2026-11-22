# 🎛️ Piccolo \[AX\] & FLAC_AX — Complete Bare-Metal Audio, AV & Hardware Master Guide (UK)

**Target System:** Bare-Metal Audio, Realtime Synth (`2.00 ms`) & Retro/DVD AV Appliance  
**Reference `[AX]` Hardware Stack:** HiBy FC3 (`VID 0x32BB`, ESS Sabre `ES9281AC Pro`) + Nobsound Q10 Class-D Stereo Amp + Wharfedale Diamond 9.0 Speakers & Grado SR325x Open-Back Headphones  
**Runtime Platform:** Real-Time Linux (`6.12-rt` / `Piccolo [AX]`) & Desktop Linux (`Ubuntu/Kubuntu 24.04+`) + Windows 10/11 Fallback  
**Total System Cost:** **~£45 – £554** *(Reference Rig: **~£165 – £188** Speakers / **~£464 – £487** Complete Speaker + Grado Rig; End-to-end latency: **1.5 – 3.0 ms**)*

Pairing **`FLAC_AX`** on real-time Linux (`6.12-rt` / `Piccolo [AX]`) with a modular **£29–£35 reference USB-C DAC** (**HiBy FC3** or **TRN Black Pearl**), a compact **Nobsound Class-D stereo amplifier**, and **nearfield British hi-fi speakers** or **open-back headphones/IEMs** transforms an ordinary `x86_64` PC into a deterministic, sub-2 ms bare-metal audio appliance—delivering bit-perfect 32-bit PCM/DSD playback, a `2.00 ms` hardware-feel synth workstation, and sample-clock-locked retro/DVD upscaling.

---

## 🎯 Section 1: The Modular `FLAC_AX` Architecture

### 🚫 The Modern Desktop Audio Trap

Standard desktop operating systems (Windows 10/11 or stock Linux running PipeWire/PulseAudio) funnel audio through shared software mixers that silently resample streams, apply reduced-precision digital volume attenuation, and introduce `10` to `150+ ms` of buffer and spatial-DSP lag. Meanwhile, built-in motherboard audio chips sit millimetres from GPU VRMs and high-speed PCIe lanes, picking up coil whine, ground noise, and shared USB/SSD interrupt stutters.

### ⚡ The Modular `FLAC_AX` Breakthrough

Stripping away the OS audio server and active speaker DSP puts **75% to 85% of your budget** directly into DAC silicon, clean current amplification, and acoustic transducers:

1. **The Software Engine (`FLAC_AX` on Linux `6.12-rt` / `Piccolo [AX]`):** Runs a zero-allocation C++20 real-time engine across a strict `2:3:1` CPU core topology. Core 5 (`RamFilePreloader`) decodes and locks entire lossless FLAC albums (up to `32-bit/384kHz`) or native `DSD64/DSD128` masters into physical RAM (`mlockall`) before playback begins—allowing your SSD bus to go completely silent. Core 0 (`SCHED_FIFO`, Priority `80`, `isolcpus` / `nohz_full` / `rcu_nocbs`) streams bit-exact `S32_LE` or `DoP` frames directly to ALSA node `hw:CARD=FC3,DEV=0` (or `hw:CARD=BLACKPEARL,DEV=0`) at **1.5–2.00 ms** latency.
2. **The Digital Heart (HiBy FC3 `£29.67–£35` or TRN Black Pearl `£29.18`):** Thumb-sized UAC 2.0 USB-C DACs delivering a full **`2.0 Vrms`** (`4.0 Vrms` balanced on Black Pearl) line-level output with `+130 dB` SNR, physical hardware volume buttons, detachable USB-C umbilicals, and zero external power bricks—outclassing the UK-throttled `0.5 Vrms` Apple dongle by `14×` to `40×` in power.
3. **The Analog Muscle (Compact `TI TPA3116` Class-D Stereo Amp — Nobsound Q10 / NS-01G Pro, £21–£35):** Because `FLAC_AX` performs bit-perfect 32-bit volume scaling ahead of the DAC, your amplifier needs zero digital inputs, Bluetooth radios, or tone-control DSP chips—acting purely as a `<1 ms` analog current booster powered by a quiet, regulated `19V–20V` laptop PSU (`£0–£12`).
4. **The Acoustic Payoff (Passive Nearfield Speakers £99–£149/pair & Wired Headphones/IEMs £16–£299):** With zero spend wasted on bloated multi-hundred-pound integrated amps or wireless modules, your budget goes straight into genuine British nearfield standmounts (**Wharfedale Diamond 9.0** at £99 up to **Wharfedale Diamond 220** at £149) and flagship open-back headphones (**Grado SR325x** at £299) driven straight off the HiBy FC3 at sub-2 ms latency.

### 🎹 Three Real-Time Appliances in One Rig

| Appliance Mode | Binary / Engine | Key Capability & Hardware Clock Lock |
| :--- | :--- | :--- |
| **Bit-Perfect Hi-Fi Player** | `flac_ax` | RAM-locked (`mlockall`) lossless FLAC (`32-bit/384kHz`) and native `DoP` DSD64/128 playback directly to `hw:CARD=FC3` / `BLACKPEARL` at `1.5–2.00 ms`. |
| **Hardware-Feel Synthesizer** | `ax_av_studio` | 16-voice polyphonic subtractive synth, 8-voice percussion, stereo Schroeder reverb, and ping-pong tape echo at `96 samples` (`2.00 ms @ 48 kHz`) with 60 FPS OpenGL oscilloscope & 64-band FFT. |
| **Zero-Drift Retro & DVD Upscaler** | `ax_video_upscaler` | USB 3.0 HDMI capture (`240p` passthrough / `480i/576i` Bob deinterlace via MacroSilicon MS2130S) and GPU shader DVD upscaling (`720p/1080p/2K`), sample-clock-locked via `AudioMasterPtsSync`. |

### 📊 System Architecture Comparison

| System Metric | Modular `FLAC_AX` + HiBy FC3 + Nobsound Q10 Rig | Commercial Soundbars / AV Receivers (£600–£1,200+) |
| :--- | :--- | :--- |
| **Total UK System Cost** | **~£45 – £334** (Headphones) / **~£156 – £255** (Speakers) | £600 – £1,200+ |
| **End-to-End Hardware Latency** | **~1.5 – 2.00 ms** (Headphones) / **~2.0 – 3.0 ms** (Speakers) | 15 – 150+ ms (HDMI handshakes, room DSP, wireless buffers) |
| **Signal Path Purity** | Bit-exact `S32_LE` / native `DoP` from locked RAM to DAC | Resampled OS/HDMI audio + mandatory internal DSP |
| **Stereo Imaging** | True physical speaker triangle (`0.7–1.0 m`) or open-back stage | Narrow bar chassis relying on artificial phase widening |
| **Longevity & Serviceability** | Passive speakers & wired headphones last `15–25+` years; modular swap | Failed HDMI boards, dead batteries, or app shutdowns brick unit |

---

## 🔬 Section 2: Sub-£50 USB DAC Silicon Shootout (`Piccolo [AX]` Front-Ends)

Desktop Class-D speaker amplifiers are calibrated around a **2.0 Vrms** single-ended input into `10 kΩ – 47 kΩ`. EU voltage regulations throttle the UK Apple USB-C dongle (`A2155`) to `0.5 Vrms` (`~8 mW @ 32 Ω`) and cap sample rates at `24-bit/48 kHz`—forcing studio masters through resampling and starving Class-D input stages for gain.

```text
+======================================================================================+
|    PICCOLO [AX] (6.12-RT TORAM) -> HIBY FC3 / TRN BLACK PEARL -> NOBSOUND Q10 AMP    |
+======================================================================================+

[ CORE 5 (0x20): Non-RT Background (storage_aux) ]
  ├── RamFilePreloader (Locks FLAC / .dsf / .dff into RAM; storage bus goes idle)
  ├── MpkMini3Listener (Lock-free SPSC MIDI queue -> Cores 2–4 Synth/FX DSP)
  └── Intel iHD VA-API / MS2130S Capture (Zero-copy dma_buf -> Core 1 GPU Presentation)
         |
         v  (Lock-Free SPSC Ring Buffer / /run/lock/ax-dac.lock Arbitration)
[ CORE 0 (0x01): isolcpus + SCHED_FIFO Prio 80 + USB xHCI IRQ Affinity ]
  └── Direct ALSA hw:CARD=FC3,DEV=0 (or BLACKPEARL) (Bypasses PipeWire/PulseAudio/dmix)
         |
         |  USB 2.0 High-Speed (Raw S32_LE PCM up to 384 kHz or DoP DSD128/DSD256)
         v
+-------------------------------------------------------------------+
|  HIBY FC3 (ES9281AC PRO)          OR   TRN BLACK PEARL (2x CS43131)|
|  [Single-Die USB PHY + QUAD DAC]       [CB5100 + Dual-Mono DACs]   |
|  [Optional 0.69" OLED Telemetry]       [On-Board 8/10-Band PEQ]    |
|  [2.0 Vrms SE / 112 mW @ 32 Ohm]       [2.0 Vrms SE / 4.0 Vrms Bal]|
+-------------------------------------------------------------------+
         |                                           |
         | (3.5 mm -> RCA / AUX Interconnect)        | (Direct 3.5 mm / 4.4 mm Out)
         v                                           v
[ NOBSOUND Q10 CLASS-D AMP ] -> [ DIAMOND 9.0 ]    [ GRADO SR325x / IEMs ]
```

### 2.1 Silicon Architecture & Circuit Topology (Sub-£50 Contenders)

* 🎛️ **HiBy FC3 (`£29.67` Standard / `£35` OLED Display — `VID 0x32BB` Reference Target):** Consolidates USB 2.0 PHY, 32-bit HyperStream II QUAD DAC, and analogue output stage onto a **single piece of ESS Sabre `ES9281AC Pro` silicon**—eliminating external USB-to-I²S bridges and inter-chip jitter domains. Outputs `2.0 Vrms` (`112 mW @ 32 Ω`) SE with `< 1.0 Ω` output impedance, `+130 dB` SNR, native `32-bit/384 kHz` PCM, DoP DSD128, MQA rendering, physical volume buttons, and detachable USB-C cable.
* 🦪 **TRN Black Pearl (`£29.18` — Dual-Mono Value & Hardware PEQ):** Deploys **two separate Cirrus Logic `CS43131` DACs** in a true dual-mono configuration behind a `CB5100` USB bridge—dedicating one MasterHIFI converter per stereo channel. Delivers `2.0 Vrms` (`125 mW @ 32 Ω`) from 3.5mm SE and `4.0 Vrms` (`320 mW @ 32 Ω`) from 4.4mm Pentaconn balanced. Crucially integrates an **8- to 10-band hardware parametric EQ (PEQ)** stored directly in on-board flash (configured via Walk Play web/app), physical volume/playback buttons, a detachable USB-C cable, and a **TRRS headset microphone ADC**.
* ⚡ **FiiO KA11 (`£29.19` — Single-Ended Brute-Force Powerhouse):** Pairs a Cirrus Logic `CS43131` with an independent **`SGM8262` high-current operational amplifier**, swinging **`2.5 Vrms` (`200 mW @ 32 Ω`, `245 mW @ 16 Ω`)** out of a standard 3.5mm jack with `< 0.5 Ω` output impedance. Trade-offs: fixed braided copper lead, no physical volume buttons, higher thermal draw, and `+2 dB` hotter line output requiring lower `--volume` gain (`0.70–0.75`) into TPA3116 speaker amps.
* 💎 **Moondrop Dawn Pro 2 (`£49.79` — Micro-Linearity IEM Specialist):** Steps up to **dual standalone Cirrus Logic `CS43198` DACs** plus external dual-channel op-amps (`THD+N < 0.00014%`, `132 dB` dynamic range), `2.0 Vrms` (`~60 mW @ 32 Ω`) SE / `4.0 Vrms` (`~120 mW @ 32 Ω`) balanced, 100-step hardware volume control, and 8-band hardware PEQ. Ideal for ultra-sensitive IEMs, though its £20 premium yields diminishing returns on high-impedance (`10–47 kΩ`) Class-D speaker inputs.

### 2.2 Complete USB Front-End Comparison Matrix

| Specification | Generic `UC02` Clone (`VID 0x0000`) | 🍎 Apple Dongle (UK `A2155`) | 🎛️ HiBy FC3 (`VID 0x32BB`) | ⚡ FiiO KA11 | 🦪 TRN Black Pearl | 💎 Moondrop Dawn Pro 2 | 🎙️ Neat Bumblebee (Used 2-in-1) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **UK Street Price** | ~£3 – £7 (Avoid) | **~£9.00** (New) | **£29.67** (`£35` OLED) | **£29.19** (New) | **£29.18** (New) | **£49.79** (Pre-Order) | **~£25 – £50** (Used) |
| **DAC / Amp Silicon** | Unprogrammed C-Media/Jieli | `CS46L06` Single-Die | `ES9281AC Pro` Single-Die | `CS43131` + `SGM8262` | **Dual `CS43131` + `CB5100`** | Dual `CS43198` + Ext. Amp | `3S SSS1629` + 24/96 Codec |
| **Max Output (3.5mm SE)** | ~0.4–0.8 Vrms | 0.5 Vrms (EU Cap) | **2.0 Vrms (112 mW)** | **2.5 Vrms (200 mW)** | **2.0 Vrms (125 mW)** | 2.0 Vrms (~60 mW) | ~1.26 Vrms (100 mW @ 16Ω) |
| **Max Output (4.4mm Bal)** | None | None | None | None | **4.0 Vrms (320 mW)** | 4.0 Vrms (~120 mW) | None |
| **Power Gain vs. UK Apple** | ~1× | 1× (Baseline) | 14× (3.5mm) | 25× (3.5mm) | 15.6× (SE) / **40× (Bal)** | 7.5× (SE) / 15× (Bal) | ~6.3× |
| **PCM / DSD Ceiling** | 16-bit / 48 kHz | 24-bit / 48 kHz (No DSD) | 32-bit / 384 kHz, DSD128 | 32-bit / 384 kHz, DSD256 | **32-bit / 384 kHz, DSD256** | 32-bit / 384 kHz, DSD256 | 24-bit / 96 kHz (PCM only) |
| **Dynamic Range / THD+N** | ~85 dB / 0.03% | ~113 dB / <0.002% | **+130 dB** / 0.0006% | **130 dB** / 0.00017% | **>130 dB** / <0.0002% | **132 dB** / <0.00014% | 98 dB / 0.05% |
| **On-Board Hardware EQ** | None | None | None | None (Filters only) | **Yes (8/10-band PEQ)** | Yes (8-band PEQ) | Analog EQ Switch |
| **In-Line Mic ADC** | Yes (Noisy) | Yes | No | No | **Yes (TRRS Mic ADC)** | No | **Yes (Cardioid Condenser)** |
| **Interconnect & Controls** | Fixed cable | Fixed TPE cable | Detachable + Vol Buttons | Fixed braided copper | **Detachable + Vol Buttons** | Detachable + 100-Step Vol | Detachable USB + Knobs |
| **`Piccolo [AX]` 2 ms Support** | Poor (ID collision) | Good (48 kHz fixed) | **Native Target (`FC3`)** | Very Good (UAC 2.0) | **Very Good (UAC 2.0)** | Very Good (UAC 2.0) | Poor (12 ms UAC 1.0) |

### 2.3 Front-End Buying Verdicts & Neat Bumblebee Speaker Rule

* 🥇 **HiBy FC3 (£29.67 Standard / £35 OLED — Reference `[AX]` Plug-and-Play Target):** Zero-config `VID 0x32BB` native target (`hw:CARD=FC3,DEV=0`), single-die ESS Sabre precision, MQA rendering, physical volume buttons, and optional `0.69"` OLED telemetry confirming sample rate and DoP lock.
* 🏆 **TRN Black Pearl (£29.18 — Best Alternative & Hardware Room-EQ Winner):** Dual-mono `CS43131` architecture, `2.0 Vrms` Class-D line-out, `320 mW` 4.4mm balanced headphone power, **flash-stored hardware PEQ** (solving bare-metal room EQ and cinema dialogue lift without host DSP latency), physical buttons, detachable cable, and **TRRS headset mic support** for Windows dual-boot gaming.
* ⚡ **FiiO KA11 (£29.19 — Best Single-Ended 3.5mm Headphone Power):** Delivers `200 mW` through a standard 3.5mm jack. Cap `flac_ax --volume` at `0.70–0.75` when feeding TPA3116 speaker amps due to its hotter `2.5 Vrms` output.
* 💎 **Moondrop Dawn Pro 2 (£49.79 — Best for Ultra-Sensitive IEMs):** Dual `CS43198` purity and 100-step attenuation banish background hiss on low-impedance IEMs, though its £20 premium is largely inaudible on speaker rigs.
* 🎙️ **Neat Bumblebee (~£25–£50 Used — Best 2-in-1 Desktop Condenser Mic + DAC):** Consolidates a studio cardioid condenser mic, shockmount, and 24-bit/96 kHz codec into one USB port. **Mandatory Speaker Safety Rule:** Because it has an always-live zero-latency analogue monitor tap with no mute button, keep the top switch on **NEUTRAL**, `HEADPHONE VOL` at **2 o'clock (~70%)**, and **`MIC GAIN` turned fully counter-clockwise (0%)** during speaker playback to prevent acoustic feedback loops.
* 🍎 **Apple USB-C Adapter (£9 — Stopgap) vs. `UC02` Clones (Avoid):** Keep the genuine `VID 0x05AC` dongle only as an emergency backup; never buy unbranded **`UC02` (`VID_0000&PID_0000`)** clones, which inject USB whine and break `udev` matching.

---

## ⚡ Section 3: Compact Class-D Amplifiers & Safe Power Supplies

Desktop Class-D amplifiers switch DC supply rails directly into the speaker load via an LC reconstruction filter—meaning **power supply quality directly determines bass punch, noise floor, and hardware safety**. Because `FLAC_AX` performs bit-perfect 32-bit volume scaling ahead of a full `2.0 Vrms` DAC, expensive £70–£130 integrated amplifiers are unnecessary; compact **£21–£35 `TI TPA3116` mini stereo amps** deliver transparent nearfield power without inflating system cost.

### 3.1 Compact Stereo Amplifier Selection & Safe Voltage Matching

* 🏆 **Reference Desktop Amp — Nobsound Q10 (~£30–£35, `TI TPA3116` Stereo):** Compact desktop Class-D stereo amplifier with clean `2.0 Vrms`-compatible line inputs, tactile volume control, and gold-plated banana-plug speaker binding posts—delivering transparent `<1 ms` current amplification for the **Wharfedale Diamond 9.0** without wasting budget on unused Bluetooth/DSP circuits.
* 💸 **Entry Budget Amp — Nobsound NS-01G Pro (~£21, `TI TPA3116` Stereo):** Ultra-compact (`78 × 70 × 38 mm`) `TPA3116` stereo mini-amp with 3.5 mm `AUX` line-in and banana-plug speaker terminals—ideal for ultra-tight desk footprints. *(Always verify you are ordering a 2-channel stereo model, not a mono subwoofer amp like the Douk NS-03G.)*
* 🔌 **Zero-Waste PSU Strategy (`£0 – £18`):** Both units accept `12V–24V DC` (`5.5 × 2.5 mm` centre-positive). Repurpose a **genuine 19V/4.74A (90W) or 20V/4.5A (90W) Dell, Lenovo, or HP laptop power brick** (`£0` from a spare drawer or `~£12` used with barrel adapter), or use a regulated **Mean Well `GST90A19` (`19V`) / `24V/4A` PSU (`~£18`)**—staying safely below the TPA3116's 26V ceiling and 25V smoothing capacitors.

### 3.2 Power Supply Buying Rules

| Category | Recommended Power Supplies (Buy) | Hazardous Power Supplies (Avoid) |
| :--- | :--- | :--- |
| **Brand & Topology** | **Genuine Dell/Lenovo/HP 19V–20V laptop bricks** (`65W–90W` with `5.5 × 2.5 mm` tip) or **Mean Well** industrial units (`GST90A19` / `GST90A24`). | Unbranded generic `24V/48V` bricks from online marketplaces, adjustable multi-voltage rotary wall warts, or cheap `12V/2A` router adapters. |
| **Electrical Safety & Noise** | Proper UKCA/CE isolation, low switching ripple (`< 100 mV p-p`), genuine over-current/short-circuit protection, and tight barrel contacts. | Fake current ratings (voltage sags on bass transients), missing EMI filters (injects high-pitched whine into speakers), and poor mains isolation. |
| **Voltage Discipline** | Stay at least `2V–4V` below internal smoothing capacitor ratings (`25V` caps on standard `TPA3116` boards). | Feeding `24V+` into a `TPA3116` board with `25V` capacitors—causes capacitor venting/failure. |

---

## 🔊 Section 4: Nearfield Loudspeakers (UK Market — Richer Sounds)

Capping the passive loudspeaker lineup at **£149** keeps the focus on compact British and studio standmounts engineered specifically for **70–100 cm nearfield desk placement** without paying for oversized floorstanding/large-room cabinets.

| Tier & Model | UK Price | Impedance / Sensitivity | Freq. Range & Size (`H×W×D`) | Port & Acoustic Design | Best Piccolo \[AX\] Pairing & Character |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. JBL Control One** *(Entry)* | **~£99** | `4–8 Ω` / `89 dB` | `80 Hz – 20 kHz`<br>`228 × 155 × 139 mm` | Front / Compact Studio (+ desk/wall brackets) | **Nobsound NS-01G Pro or Q10 (`19V`).** High `89 dB` efficiency; retro game SFX and synth leads jump out cleanly at low volume. |
| **2. Wharfedale Diamond 9.0** *(Reference)* | **~£99** | `6 Ω` / `86 dB` | `60 Hz – 24 kHz`<br>`236 × 145 × 165 mm` | **Front-firing bass reflex** | **Nobsound Q10 + HiBy FC3 (`[AX]` Reference).** Warm, smooth Kevlar midrange; front port allows placement flush against a shallow desk wall without bass boom. |
| **3. Q Acoustics 3010i** *(Higher)* | **~£119** | `6 Ω` / `86 dB` | `65 Hz – 30 kHz`<br>`253 × 150 × 252 mm` | Rear port (foam bungs included) | **Nobsound Q10 (`19V–24V`).** Narrow `150 mm` front baffle projects a wide, fatigue-free 3D desktop stereo stage. |
| **4. Wharfedale Diamond 220** *(Ceiling)* | **~£149** | `8 Ω` (`4.1 Ω` min) / `86 dB` | `56 Hz – 20 kHz`<br>`315 × 174 × 255 mm` | **Downward-firing slot port** | **Nobsound Q10 (`24V` PSU).** Vents through a calibrated plinth gap to eliminate rear-wall boom; true studio-grade acoustic neutrality. |

---

## 🎧 Section 5: Wired Headphones & IEMs (`1.5–2.00 ms` Direct from HiBy FC3)

### 5.1 Open-Back & Closed-Back Wired Headphones

| Model | UK Price | Design & Weight | Impedance / Sensitivity | Music | Gaming | Movies | Sonic Signature & `FLAC_AX` Gain Note |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Koss Porta Pro** | **~£39** | Open On-Ear (`60 g`) | `60 Ω` / `101 dB` | ★★★★☆ | ★★★☆☆ | ★★★☆☆ | Lightweight retro comfort; punchy warmth on any dongle. |
| **Sennheiser HD400S** | **~£59** | Closed Over-Ear (`217 g`) | `18 Ω` / `120 dB` | ★★★☆☆ | ★★★★☆ | ★★★★☆ | Budget closed-back bass warmth; set `--volume 0.25`. |
| **Beyerdynamic DJ 300 Pro X** | **~£85** | Closed On/Over-Ear (`315 g`) | `48 Ω` / `109 dB` | ★★★★☆ | ★★★★☆ | ★★★★★ | Visceral `5 Hz` sub-bass extension and `20 dB` passive isolation. |
| **Austrian Audio Hi-X15 / Hi-X20** | **~£89 / £129** | Closed Over-Ear (`255 g`) | `25 Ω` / `113 dB/V` | ★★★★☆ | **★★★★★** | ★★★★☆ | Fast, snappy closed-back monitoring; blocks room noise & supports TRRS boom mic on TRN Black Pearl. |
| **Sennheiser HD 560S** | **~£99 – £139** | Open Over-Ear (`240 g`) | `120 Ω` / `110 dB/V` | **★★★★★** | **★★★★★** | ★★★★☆ | Angled transducers, flat studio accuracy & wide stage; HiBy FC3's `2.0 Vrms` drives `120 Ω` cleanly (`--volume 0.65–0.80`). |
| **Sennheiser HD 569** | **~£119 – £149** | Closed Over-Ear | `23 Ω` / `115 dB/V` | ★★★★☆ | **★★★★★** | **★★★★★** | Deep sub-bass extension, plush velour comfort, and inline mic compatibility with TRN Black Pearl. |
| **Grado SR325x** *(Reference Head-Fi)* | **£299** | Open On-Ear (Alloy, `280 g`) | `38 Ω` / `98–99.8 dB` | **★★★★★** | ★★★★☆ | ★★★☆☆ | **Reference Open-Back Partner.** CNC-machined aluminium cups; explosive transient snap & flagship DSD detail straight out of HiBy FC3 (`--volume 0.50–0.65`). |

### 5.2 In-Ear Monitors (IEMs — `-26 dB` Passive Isolation & 2-in-1 USB-C DSP Options)

> ⚠️ **Crucial `FLAC_AX` Gain Rule:** When plugging `16–32 Ω` IEMs into `hw:CARD=FC3` or `BLACKPEARL`, buy the **`3.5 mm`** cable version and drop volume to **`--volume 0.20–0.25`** before playback (or use the **Moondrop Dawn Pro 2**'s 100-step hardware attenuation for zero background hiss). Choose the **`USB-C DSP`** editions for standalone 2-in-1 use on phones/PCs without a separate dongle.

| Category & Model | UK Price | Driver Topology | Impedance / Sensitivity | Built-In USB-C DAC? | Best Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Under £30: Moondrop Chu II / DSP** | **~£16 – £24** | `1×` Al-Mg Dynamic (`6.4 g/ear`) | `18 Ω` / `119 dB/V` | **Yes (`Chu II DSP 24/96`)** | Sub-£25 metal-shell 2-in-1 all-rounder (`3.5mm` for DACs). |
| **Under £30: 7Hz × Crinacle Zero:2** | **~£22 – £25** | `1×` PU+Metal Dynamic | `32 Ω` / `108 dB/V` | **Optional USB-C DAC version** | Sub-£25 `+3 dB` warm sub-bass, gaming & movie immersion. |
| **Over £30: Moondrop May** | **~£65 – £75** | **`1DD + 1 Planar` Hybrid** | `30 Ω` / `120 dB/V` | **Yes (`FreeDSP 24/96` + 5-band PEQ)** | Flagship 2-in-1 USB-C DAC + planar hybrid (swappable `0.78 mm` 2-pin). |
| **Over £30: Truthear Hexa** | **~£75 – £85** | **`1DD + 3BA` Hybrid** | `20.5 Ω` / `120 dB/V` | Optional (`0.78 mm` 2-pin cable) | Neutral 3-way studio reference & competitive FPS gaming. |

---

## 📐 Section 6: Physical Acoustics, Pre-Terminated Cabling & Gain Staging

### 6.1 Nearfield Desk Geometry & Bare-Metal Hardware PEQ

1. **Equilateral Triangle & Toe-In:** Space left and right tweeters `70–100 cm` apart matching the distance to your ears, angled inward (`toe-in`) by `15°–20°`.
2. **Acoustic Isolation Pads (~£12):** Raise tweeters to ear height on angled foam isolation pads to prevent `150–400 Hz` desk-bounce chestiness and cabinet vibration transfer.
3. **Rear-Wall Boundary Control:** Leave `15–25 cm` behind rear-ported speakers (**Q Acoustics 3010i**) or insert the included **foam port bungs**—or use the front-ported **Wharfedale Diamond 9.0** / downward-ported **Diamond 220**, which sit cleanly near a back wall without bass boom.
4. **Solving the Bare-Metal Room-EQ Paradox (TRN Black Pearl Hardware PEQ):** Because bare-metal `flac_ax` bypasses all host-side software DSP to preserve bit-exact `S32_LE`/DoP delivery, DACs without on-board DSP (like the HiBy FC3) rely strictly on physical placement. With the **TRN Black Pearl**, you can save subtractive parametric EQ directly into the DAC's on-board flash via a web browser (`-2.5` to `-4.0 dB` at `100–140 Hz`, `Q=2.0` for rear-wall boom; `-2.0` to `-3.0 dB` at `220–320 Hz`, `Q=2.5` for desk bounce; or a `+2 dB` `2.5–3.5 kHz` vocal lift for cinema)—retaining bit-perfect host transport with **zero software DSP latency**.

### 6.2 Pre-Terminated HiFi Speaker Cable (`£6.45`)

Instead of stripping raw wire and crimping separate plugs, use a ready-terminated stereo speaker cable pair (**2pcs/1Pair HiFi RCA to 2 Banana Speaker Cable for Amplifier — `£6.45`**):

* **High-Quality HiFi Audio Connection:** Delivers clear, distortion-free audio transmission between your amplifier and passive speakers with reliable signal transfer.
* **Compatible with Standard Speaker Systems:** Designed for seamless integration with standard home audio and mini Class-D amplifiers (verify connector polarity: match **Red `+` to Red `+`** and **Black `–` to Black `–`**).
* **Multiple Length Options Available:** Choose from **0.5 m, 1 m, 1.5 m, 2 m, and 3 m** to match your desk span without excess cable coils.
* **Compact and Durable Design:** Sturdy yet flexible `4.0 mm` outer diameter jacket supplied in protective, tangle-free polybag packaging.
* **Simple and Efficient Setup:** Supplied as a complete **1Pair (two-cable) stereo bundle** for left and right channels straight out of the bag.

### 6.3 Gain-Staging Summary

* **Speaker Mode (`2.0 Vrms` DACs — HiBy FC3 / TRN Black Pearl -> Nobsound Q10):** Set `flac_ax --volume 0.80` (and DAC hardware buttons near max) so the DAC outputs a clean `~1.6–2.0 Vrms` signal, then control room volume solely on the **Nobsound Q10's** physical dial. *(If using the `2.5 Vrms` **FiiO KA11**, cap `--volume` at **`0.70–0.75`**.)*
* **Reference Open-Back (`38 Ω` Grado SR325x):** Start at `--volume 0.50–0.65` on the HiBy FC3.
* **High-Impedance Open-Back (`120 Ω` Sennheiser HD 560S):** Start at `--volume 0.65–0.80`.
* **Sensitive IEMs / Low-Impedance (`16–32 Ω` Chu II, Zero:2, HD400S):** Always drop to `--volume 0.20–0.25` before plugging in.

---

## 🛠️ Section 7: Complete Tiered System Blueprints (Built Around the `[AX]` Reference Rig)

The table below anchors the entire ecosystem around **Tier 2 (The `[AX]` Reference Rig)**—pairing the **HiBy FC3**, **Nobsound Q10**, **Wharfedale Diamond 9.0**, and **Grado SR325x**—with streamlined lower and higher tiers:

| System Tier & Role | USB Front-End (`Piccolo [AX]`) | Class-D Amp & PSU | Speakers & Personal Audio | Cabling & Isolation | Approx. Total |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Tier 0: Sub-£50 IEM Starter** *(Lowest)* | **HiBy FC3** (`£29.67`) *or* **TRN Black Pearl** (`£29.18`) | *None (Direct from DAC)* | **Moondrop Chu II (`3.5mm`)** (`£16–£19`) | *Included* | **~£45 – £49** |
| **Tier 1: Entry Speaker Rig** *(Lower)* | **HiBy FC3** (`£29.67`) *or* **TRN Black Pearl** (`£29.18`) | **Nobsound NS-01G Pro** (`~£21`) + repurposed 19V Laptop PSU (`£0–£12`) | **JBL Control One** (`~£99`, `89 dB` efficiency) *or* **Wharfedale Diamond 9.0** (`~£99`) | `£6.45` Speaker Cable | **~£156 – £168** |
| **Tier 2: `[AX]` Reference Rig** *(Baseline Anchor)* | **HiBy FC3 (`VID 0x32BB`)** (`£29.67–£35`) | **Nobsound Q10** (`~£30–£35`) + 19V–20V Laptop PSU (`£0–£12`) | **Wharfedale Diamond 9.0** (`~£99`) + **Grado SR325x** (`£299`) | `£6.45` Speaker Cable | **~£165 – £188** *(Speakers)* / **~£464 – £487** *(Full)* |
| **Tier 3: Wide-Stage Nearfield** *(Higher)* | **HiBy FC3 OLED** (`£35`) *or* **TRN Black Pearl** (`£29.18`, on-board PEQ) | **Nobsound Q10** (`~£30–£35`) + 19V–20V Laptop PSU (`£12`) | **Q Acoustics 3010i** (`~£119`, narrow `150 mm` baffle + port bungs) + **Sennheiser HD 560S** (`£99`) | `£6.45` Speaker Cable + Foam Pads (`£12`) | **~£297 – £308** |
| **Tier 4: Studio Reference Ceiling** *(Highest)* | **HiBy FC3 OLED** (`£35`) *or* **TRN Black Pearl** (`£29.18`) | **Nobsound Q10** (`~£30–£35`) + regulated `24V/4A` PSU (`~£18`) | **Wharfedale Diamond 220** (`~£149`, downward slot port) + **Grado SR325x** (`£299`) | `£6.45` Speaker Cable + Foam Pads (`£12`) | **~£543 – £554** *(or **~£244 – £255** speakers-only)* |

---

## 🐧 Section 8: Universal Real-Time Linux (`6.12-rt` / Buildroot / Desktop) Setup

This hardened configuration works identically across **bare-metal Buildroot/Batocera (`Piccolo [AX]`)** and **desktop Linux (`Ubuntu/Kubuntu 24.04+` or `piccolo-ax-desktop`)**, supporting both the **HiBy FC3 (`VID 0x32BB`)** and **TRN Black Pearl** (plus other UAC 2.0 dongles).

### 8.1 Universal `udev` Rules & Real-Time Limits

First, plug in your DAC and confirm its Vendor/Product IDs via `lsusb` (HiBy FC3 is `32bb:0002` / `32bb:*`; TRN Black Pearl `CB5100` bridges typically enumerate under `1a86:*` or `3302:*`—verify with `lsusb` and adjust if needed).

Save to `/etc/udev/rules.d/99-hiby-fc3-ax.rules` (or `/userdata/system/udev/rules.d/` on `Piccolo [AX]`):

```udev
# /etc/udev/rules.d/99-hiby-fc3-ax.rules
# 1. Prevent PulseAudio / legacy PipeWire ACP from claiming HiBy FC3 (VID 0x32BB)
SUBSYSTEM=="sound", ATTRS{idVendor}=="32bb", ATTR{id}="FC3", ENV{PULSE_IGNORE}="1", ENV{ACP_IGNORE}="1", GROUP="audio", MODE="0660"

# 2. Disable USB autosuspend and grant audio group raw USB access
ACTION=="add", SUBSYSTEM=="usb", ATTR{idVendor}=="32bb", MODE="0660", GROUP="audio", TEST=="power/control", ATTR{power/control}="on", TEST=="power/autosuspend", ATTR{power/autosuspend}="-1"

# 3. Optional: Neat Bumblebee (VID 0x0C76 / PID 0x1617)
SUBSYSTEM=="sound", ATTRS{idVendor}=="0c76", ATTRS{idProduct}=="1617", ATTR{id}="BUMBLEBEE", GROUP="audio", MODE="0660"
```

Remove any deprecated `nrpacks=1` modprobe file on `6.12+` kernels and configure PAM real-time limits in `/etc/security/limits.d/99-flac-ax.conf`:

```bash
sudo rm -f /etc/modprobe.d/hiby-fc3-ax.conf
```

```conf
# /etc/security/limits.d/99-flac-ax.conf
@audio   -  rtprio     95
@audio   -  memlock    unlimited
@audio   -  nice      -19
```

Apply and verify:

```bash
sudo usermod -aG audio "$USER"
sudo udevadm control --reload-rules && sudo udevadm trigger

# Replug HiBy FC3 and verify USB autosuspend is locked "on" and group is "audio":
grep -l "32bb" /sys/bus/usb/devices/*/idVendor | xargs -I{} dirname {} | xargs -I{} cat {}/power/control
ls -l /dev/snd/pcmC*
```

### 8.2 WirePlumber `0.5+` Device Exclusion Rule (Desktop Linux Only)

If PipeWire/WirePlumber is installed (`Ubuntu/Kubuntu` or `piccolo-ax-desktop`), create `/etc/wireplumber/wireplumber.conf.d/51-ignore-hiby-fc3.conf` *(skip on bare-metal Buildroot/Batocera)*:

```hocon
# /etc/wireplumber/wireplumber.conf.d/51-ignore-hiby-fc3.conf
monitor.alsa.rules = [
  {
    matches = [
      {
        device.vendor.id = "0x32bb"
      }
    ]
    actions = {
      update-props = {
        device.disabled = true
      }
    }
  }
]
```

Restart and verify WirePlumber no longer holds the device:

```bash
systemctl --user restart wireplumber
wpctl status          # HiBy FC3 must NOT appear under Audio Devices
fuser -v /dev/snd/*   # Must return empty for the FC3 card node
```

### 8.3 Deterministic Core 0 Launcher & Switcher (`~/bin/ax-launch.sh`)

```bash
#!/usr/bin/env bash
# ~/bin/ax-launch.sh — Deterministic Core 0 launcher for HiBy FC3 (Buildroot & Desktop Linux)
set -euo pipefail

# Use systemd XDG_RUNTIME_DIR if available; otherwise create a group-shared dir in /tmp
if [[ -n "${XDG_RUNTIME_DIR:-}" && -d "$XDG_RUNTIME_DIR" ]]; then
    RUNTIME_DIR="$XDG_RUNTIME_DIR"
else
    RUNTIME_DIR="/tmp/ax-runtime"
    mkdir -p "$RUNTIME_DIR"
    chmod 0770 "$RUNTIME_DIR" 2>/dev/null || true
fi

PIDFILE="${RUNTIME_DIR}/ax-dac.pid"
DEVICE="hw:CARD=FC3,DEV=0"
CARD_NAME="FC3"

MODE="${1:-music}"
VOL="${2:-0.80}"
TARGET="${3:-}"

usage() {
    echo "Usage: $0 {music|synth|retro|dvd} [volume: 0.01-1.00] [album_dir_or_iso]" >&2
    exit 1
}

# 1. Validate arguments BEFORE tearing down any active audio engine
case "$MODE" in
    music|dvd)
        [[ -z "$TARGET" || ! -e "$TARGET" ]] && { echo "Error: Missing or invalid target path for $MODE." >&2; usage; }
        ;;
    synth|retro)
        ;;
    *)
        usage
        ;;
esac

if ! awk -v v="$VOL" 'BEGIN { exit !(v > 0.0 && v <= 1.0) }'; then
    echo "Error: Volume '$VOL' out of safe range (0.01 - 1.00)." >&2
    exit 1
fi

# 2. Verify DAC presence before proceeding
if ! grep -q "\[$CARD_NAME" /proc/asound/cards 2>/dev/null; then
    echo "Error: HiBy FC3 ($DEVICE) not detected in /proc/asound/cards." >&2
    exit 1
fi

# 3. Cleanly terminate previous engine and wait for ALSA PCM release
if [[ -f "$PIDFILE" ]]; then
    OLD_PID=$(cat "$PIDFILE" 2>/dev/null || true)
    if [[ -n "$OLD_PID" ]] && kill -0 "$OLD_PID" 2>/dev/null; then
        kill -TERM "$OLD_PID" 2>/dev/null || true
        for _ in {1..100}; do
            kill -0 "$OLD_PID" 2>/dev/null || break
            sleep 0.01
        done
        if kill -0 "$OLD_PID" 2>/dev/null; then
            kill -KILL "$OLD_PID" 2>/dev/null || true
        fi
    fi
    rm -f "$PIDFILE"
fi

# Ensure kernel ALSA PCM subdevice is closed (avoids EBUSY on fast switch)
CARD_NUM=$(awk -F'[][]' "/\[$CARD_NAME/ {print \$1}" /proc/asound/cards | tr -d ' ')
if [[ -n "$CARD_NUM" && -f "/proc/asound/card${CARD_NUM}/pcm0p/sub0/status" ]]; then
    for _ in {1..50}; do
        grep -q "closed" "/proc/asound/card${CARD_NUM}/pcm0p/sub0/status" && break
        sleep 0.01
    done
fi

# 4. Cleanup trap to prevent stale PID files on exit
CHILD_PID=""
cleanup() {
    if [[ -n "$CHILD_PID" ]] && kill -0 "$CHILD_PID" 2>/dev/null; then
        kill -TERM "$CHILD_PID" 2>/dev/null || true
        wait "$CHILD_PID" 2>/dev/null || true
    fi
    rm -f "$PIDFILE"
}
trap cleanup EXIT INT TERM

# 5. Launch requested engine pinned to Core 0 (SCHED_FIFO 80)
CMD=()
case "$MODE" in
    music)
        CMD=(flac_ax --device "$DEVICE" --volume "$VOL" "$TARGET")
        ;;
    synth)
        CMD=(ax_av_studio --device "$DEVICE" --volume "$VOL")
        ;;
    retro)
        CMD=(ax_video_upscaler --mode retro --preset 1080p --device "$DEVICE" --volume "$VOL")
        ;;
    dvd)
        CMD=(ax_video_upscaler --mode dvd --preset 2k --device "$DEVICE" --volume "$VOL" "$TARGET")
        ;;
esac

chrt -f 80 taskset -c 0 "${CMD[@]}" &
CHILD_PID=$!
echo "$CHILD_PID" > "$PIDFILE"
wait "$CHILD_PID"
```

Make executable and verify real-time pinning (`CLS=FF`, `RTPRIO=80`, `PSR=0`):

```bash
chmod +x ~/bin/ax-launch.sh

# Speakers (Nobsound Q10 + Wharfedale Diamond 9.0) or Sennheiser HD 560S (120 Ohm)
~/bin/ax-launch.sh music 0.80 ~/Music/FLAC_Album/

# Reference Open-Back Headphones (Grado SR325x, 38 Ohm)
~/bin/ax-launch.sh music 0.55 ~/Music/FLAC_Album/

# Sensitive 16-18 Ohm IEMs/headphones (Moondrop Chu II, Sennheiser HD400S)
~/bin/ax-launch.sh music 0.25 ~/Music/FLAC_Album/

# Instant handoff to 2.00 ms synth workstation & verify Core 0 SCHED_FIFO 80
~/bin/ax-launch.sh synth 0.75 &
ps -eLo pid,cls,rtprio,psr,comm | grep ax_av_studio
```

---

## 💻 Section 9: Windows 10/11 Dual-Boot Fallback Setup

1. **Disable Audio Enhancements:** Open *Sound Settings → Device Properties → Additional Device Properties → Enhancements* and check **Disable all enhancements** (plus disable Windows Sonic / Spatial Sound) to cut `~20–30 ms` of OS buffer delay.
2. **Match Sample Rate / Exclusive Bit-Exact Mode:** Set the default format under *Advanced* to **24-bit, 44100 Hz** (music) or **24-bit, 48000 Hz** (gaming/movies). For bit-perfect, bypass-mixer playback, use **ASIO** (`FlexASIO` in Exclusive mode targeting the HiBy FC3 `VID 0x32BB`) or **WASAPI Exclusive Mode** in Foobar2000 / MusicBee / `flac_ax`.
3. **Gain Staging:** Keep Windows master volume at **100%** when feeding the **Nobsound Q10** (`TPA3116`) speaker amplifier and control listening level solely via the physical amp dial; drop Windows volume to **50–65%** for the **Grado SR325x**, or **20–30%** before plugging in sensitive `16–18 Ω` IEMs or headphones.

### 9.1 Ready-to-Copy `FlexASIO.toml` (HiBy FC3 WASAPI Exclusive)

Save this configuration to `%USERPROFILE%\FlexASIO.toml` so ASIO-capable players (`flac_ax`, `Eden-Espresso-Wave-AX`, Foobar2000) bypass the Windows audio mixer at low latency:

```toml
# %USERPROFILE%\FlexASIO.toml — Bit-perfect WASAPI Exclusive profile for HiBy FC3
backend = "Windows WASAPI"
bufferSizeSamples = 128

[input]
device = ""

[output]
# Match the exact Windows Sound Control Panel playback device name for your HiBy FC3
device = "Speakers (HiBy FC3)"
wasapiExclusiveMode = true
wasapiAutoConvert = false
suggestedLatencySeconds = 0.003
```

*Verification:* Open the `FlexASIO` GUI or run `FlexASIOUtil`—confirm the active output reports `WASAPI Exclusive` at `32-bit` / `24-bit` integer without sample-rate conversion (`wasapiAutoConvert = false`).
