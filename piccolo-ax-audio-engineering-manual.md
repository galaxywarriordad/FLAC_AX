# 🎛️ Piccolo \[AX\] & FLAC_AX — Complete Bare-Metal Audio, AV & Hardware Engineering Manual (UK)

**Target System:** Bare-Metal Audio, Realtime Synth (`2.00 ms`) & Retro/DVD AV Appliance  
**Hardware Stack:** HiBy FC3 (`VID 0x32BB`, ESS Sabre ES9281AC Pro) + Class-D Stereo Amp + Passive/Active Hi-Fi Speakers / Wired Headphones / 2-in-1 DSP IEMs  
**Runtime Platform:** Real-Time Linux (`6.12-rt` / `Piccolo [AX]`) & Desktop Linux (`Ubuntu/Kubuntu 24.04+`) + Windows 10/11 Fallback  
**Total System Cost:** **~£50 – £730** (End-to-end deterministic hardware latency: **1.5 – 3.0 ms**)

Pairing **`FLAC_AX`** on real-time Linux (`6.12-rt` / `Piccolo [AX]`) with a modular **HiBy FC3 DAC**, a **Class-D stereo amplifier**, and **passive hi-fi speakers** or **wired headphones/IEMs** transforms an ordinary `x86_64` PC into a deterministic, sub-2 ms bare-metal audio appliance—delivering bit-perfect 32-bit PCM/DSD playback, a `2.00 ms` hardware-feel synth workstation, and sample-clock-locked retro/DVD upscaling.

---

## 🎯 Section 1: Real-Time Engine & `2:3:1` Core Topology

### 🚫 The Modern Desktop Audio Trap

Standard desktop operating systems (Windows 10/11 or stock Linux running PipeWire/PulseAudio) funnel audio through shared software mixers that silently resample streams, apply reduced-precision digital volume attenuation, and introduce `10` to `150+ ms` of buffer and spatial-DSP lag. Meanwhile, built-in motherboard audio chips sit millimetres from GPU VRMs and high-speed PCIe lanes, picking up coil whine, ground noise, and shared USB/SSD interrupt stutters.

### ⚡ The Modular `FLAC_AX` Breakthrough

Stripping away the OS audio server and active speaker DSP puts **70% to 80% of your budget** directly into silicon, clean current amplification, and acoustic drivers:

1. **The Software Engine (`FLAC_AX` on Linux `6.12-rt` / `Piccolo [AX]`):** Runs a zero-allocation C++20 real-time engine across a strict `2:3:1` CPU core topology. Core 5 (`RamFilePreloader`) decodes and locks entire lossless FLAC albums (up to `32-bit/384kHz`) or native `DSD64/DSD128` masters into physical RAM (`mlockall`) before playback begins—allowing your SSD bus to go completely silent. Core 0 (`SCHED_FIFO`, Priority `80`, `isolcpus` / `nohz_full` / `rcu_nocbs`) streams bit-exact `S32_LE` or `DoP` frames directly to ALSA node `hw:CARD=FC3,DEV=0` at **1.5–2.00 ms** latency.
2. **The Digital Heart (HiBy FC3 — ESS Sabre ES9281AC Pro, £35):** Thumb-sized USB DAC (`VID 0x32BB`) housing ESS Technology's HyperStream II QUAD DAC SoC, supporting native `32-bit/768kHz` PCM, `DSD512`, `+130 dB` SNR, and a full `~2.04 Vrms` line-level output with physical hardware volume buttons and zero external power bricks.
3. **The Analog Muscle (TI TPA3116 or TPA3255 Class-D Stereo Amp, £20–£105):** Because `FLAC_AX` performs bit-perfect 32-bit volume scaling ahead of the DAC, your amplifier needs zero digital inputs, Bluetooth radios, or tone-control DSP chips—acting purely as a `<1 ms` analog current booster.
4. **The Acoustic Payoff (Passive Speakers £99–£219/pair & Wired Headphones/IEMs £16–£329):** With zero spend wasted on internal speaker amps or wireless modules, you step straight into genuine British and European hi-fi standmounts (**Wharfedale Diamond 9.0** at £99 up to **Q Acoustics 3020c** and **Mission LX-2 MKII** at £219)—and unplugging the amp to plug wired IEMs or open-back headphones directly into the HiBy FC3 gives instant sub-2 ms late-night monitoring.

### 🎹 Three Real-Time Appliances in One Rig

| Appliance Mode | Binary / Engine | Key Capability & Hardware Clock Lock |
| :--- | :--- | :--- |
| **Bit-Perfect Hi-Fi Player** | `flac_ax` | RAM-locked (`mlockall`) lossless FLAC (`32-bit/384kHz`) and native `DoP` DSD64/128 playback directly to `hw:CARD=FC3,DEV=0` at `1.5–2.00 ms`. |
| **Hardware-Feel Synthesizer** | `ax_av_studio` | 16-voice polyphonic subtractive synth, 8-voice percussion, stereo Schroeder reverb, and ping-pong tape echo at `96 samples` (`2.00 ms @ 48 kHz`) with 60 FPS OpenGL oscilloscope & 64-band FFT. |
| **Zero-Drift Retro & DVD Upscaler** | `ax_video_upscaler` | USB 3.0 HDMI capture (`240p` passthrough / `480i/576i` Bob deinterlace via MacroSilicon MS2130S) and GPU shader DVD upscaling (`720p/1080p/2K`), sample-clock-locked via `AudioMasterPtsSync`. |

---

## 🔬 Section 2: USB DAC Silicon Shootout (`Piccolo [AX]` Front-Ends)

Consumer and desktop Class-D speaker amplifiers are calibrated around a **2.0 Vrms** single-ended input into `10 kΩ – 47 kΩ`.

```text
+======================================================================================+
|      PICCOLO [AX] (6.12-RT TORAM) -> HIBY FC3 (SCREEN) -> CLASS D AMPLIFIER           |
+======================================================================================+

[ CORE 5 (0x20): Non-RT Background (storage_aux) ]
  ├── RamFilePreloader (Locks FLAC / .dsf / .dff into RAM; storage bus goes idle)
  ├── MpkMini3Listener (Lock-free SPSC MIDI queue -> Cores 2–4 Synth/FX DSP)
  └── Intel iHD VA-API / MS2130S Capture (Zero-copy dma_buf -> Core 1 GPU Presentation)
         |
         v  (Lock-Free SPSC Ring Buffer / /run/lock/ax-dac.lock Arbitration)
[ CORE 0 (0x01): isolcpus + SCHED_FIFO Prio 80 + USB xHCI IRQ Affinity ]
  └── Direct ALSA hw:CARD=FC3,DEV=0 (Bypasses PipeWire/PulseAudio/dmix; nrpacks=1)
         |
         |  USB 2.0 High-Speed (VID 0x32BB, Raw S32_LE PCM up to 384 kHz or DoP DSD128)
         v
+-------------------------------------------------------+
|      NEW HIBY FC3 WITH DISPLAY (ES9281AC PRO)         |
|  [Integrated USB PHY] -> [32-bit HyperStream II DAC]  |
|  [OLED Telemetry: Sample Rate / Format / Hardware Vol]|
|  [Analogue Stage: 2.0 Vrms No-Load / 112 mW @ 32 Ohm] |
+-------------------------------------------------------+
         |  (Single-Ended 3.5 mm -> RCA / 3.5 mm AUX Interconnect)
         v
[ CLASS D SPEAKER AMPLIFIER (10 kOhm - 47 kOhm Input) ] -> [ PASSIVE SPEAKERS ]
```

### 2.1 Complete USB Front-End Comparison Matrix

| Specification | Generic `UC02` Clone (`VID 0x0000`) | Genuine Apple Dongle (`VID 0x05AC`) | Older HiBy FC3 (Pre-Owned) | New HiBy FC3 with Display (`VID 0x32BB`) | Cirrus Logic CS43131 Dongle *(Fosi DS2 / FiiO KA11)* | Neat Bumblebee (Pre-Owned Mic+DAC) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **UK Price & Condition** | ~£3 – £7 (New) | **~£9** (New) | **~£25 – £35** (Used Only) | **£35** (New) | **~£49 – £53** (New) | **~£25 – £50** (Used Only) |
| **USB Hardware ID** | `VID_0000&PID_0000` (Blank) | `VID_05AC&PID_110A` | `VID_32BB` | `VID_32BB` | Vendor-Specific UAC 2.0 | `VID_0C76&PID_1617` |
| **`Piccolo [AX]` Support** | Poor (udev ID collision) | Plug & Play (UAC 1/2) | **Native Target** (`hw:CARD=FC3`) | **Native Target** (`hw:CARD=FC3`) | Manual ALSA/udev Config | Manual ALSA Config (UAC 1.0) |
| **DAC / Bridge Chipset** | Unprogrammed C-Media/Jieli | Cirrus Logic 46L06-CWZR | ESS Sabre ES9281PRO | **ESS Sabre ES9281AC Pro** | Cirrus Logic CS43131 | 3S SSS1629 + Ext. 24/96 Codec |
| **On-Device Telemetry** | None | None | RGB Status LED | **0.69" OLED + RGB LED** | RGB Status LED | Gain/Vol Knobs + EQ Switch |
| **Max Resolution (`flac_ax`)** | 16-bit / 48 kHz (No DSD) | 24-bit / 48 kHz (No DSD) | **32-bit / 384 kHz**, DoP DSD128 | **32-bit / 384 kHz**, DoP DSD128 | **32-bit / 384 kHz**, DSD256 | 24-bit / 96 kHz (PCM only) |
| **Max Line-Out Voltage** | ~0.4 – 0.8 Vrms (Noisy) | **0.5 Vrms** (UK) / 1.0 Vrms (US) | ~1.4 Vrms (`70 mW @ 32 Ω`) | **2.0 Vrms** (`112 mW @ 32 Ω`) | **2.0 Vrms** (Single-ended) | ~1.26 Vrms (`100 mW @ 16 Ω`) |
| **Dynamic Range / SNR** | ~85 – 92 dB | ~113 dB | 118 dB | **+130 dB SNR** / 124 dB DNR | **130 dB** | 98 dB DR / 90 dB SNR |
| **Distortion (THD+N)** | ~0.03% (-70 dB) | < 0.002% (-98 dB) | 0.0008% (-102 dB) | **0.0006%** (-104 dB) | **0.00017%** (-115 dB) | 0.05% (-66 dB) |
| **Output Impedance** | ~5 – 15 Ω | < 0.9 Ω | < 1.0 Ω | < 1.0 Ω | **< 0.5 Ω** | ~2 – 5 Ω |
| **`SCHED_FIFO` 2 ms Stability** | Unreliable (UAC 1.0) | Good (48 kHz fixed) | **Exceptional** (`nrpacks=1`) | **Exceptional** (`nrpacks=1`) | Very Good (UAC 2.0) | Poor (12 ms UAC 1.0 polling) |
| **Built-in Microphone?** | Inline Headset Mic ADC | Inline Headset Mic ADC | **No** | **No** | **No** | **Yes** (Cardioid Condenser) |

### 2.2 Front-End Engineering Notes & Neat Bumblebee Feedback Isolation

* **New HiBy FC3 with Display (£35 New — Native `[AX]` Reference):** Single-die USB PHY + ES9281AC Pro eliminates external USB-to-I²S jitter, sustains 96-frame (`2.00 ms @ 48 kHz`) `SCHED_FIFO` buffers, arbitrates `/run/lock/ax-dac.lock` cleanly across emulators and `flac_ax`, and outputs a full **2.0 Vrms** into Class-D amplifiers.
* **Neat Bumblebee (~£25–£50 Used — 2-in-1 Studio Mic + DAC):** Integrates a discrete JFET condenser preamp and external 24-bit/96 kHz ADC/DAC codec (`100 mW @ 16 Ω`). Because the zero-latency headphone monitor tap sums directly ahead of the ADC with no physical mute switch, builders driving Class-D speakers must keep the top switch on **NEUTRAL**, `HEADPHONE VOL` at **2 o'clock (~70%)**, and **`MIC GAIN` at 0% (fully counter-clockwise)** during speaker playback to prevent electro-acoustic feedback loops.

---

## ⚡ Section 3: Class-D Amplifiers & Safe Power Supplies

Desktop Class-D amplifiers switch DC supply rails directly into the speaker load via an LC reconstruction filter—meaning **power supply quality directly determines bass punch, noise floor, and hardware safety**.

### 3.1 Amplifier Tiers & Safe Voltage Matching

* **Ultra-Budget Tier (`TI TPA3116`, ~£20–£43):** **Nobsound NS-20G** (dual TPA3116, 30-step digital encoder + front 3.5 mm `AUX` jack), **Nobsound NS-01G Pro**, or **Fosi Audio V1.0G**. *(Ensure you pick a 2-channel stereo amp, not a mono sub amp like the Douk NS-03G.)*
  * **Safe PSU:** Rated 12V–24V DC (`5.5 × 2.5 mm` centre-positive). Use a regulated **19V/4.74A (90W)** or **20V/4.5A (90W)** brick (e.g., genuine Dell/Lenovo/HP laptop brick with adapter, or Mean Well `GST90A19`), or a regulated `24V/4A–5A` supply—staying safely below the TPA3116's 26V ceiling and 25V smoothing capacitors.
* **Sweet-Spot / High-Headroom Tier (`TI TPA3255`, ~£65–£105):** **Fosi Audio V3** (logarithmic Taper A pot), **Fosi TB10D**, or **Aiyima A07**.
  * **Safe PSU:** Use **32V/5A (160W)** for cool nearfield desktop listening, or upgrade to a **48V/5A (240W) GaN PSU** when driving lower-sensitivity speakers (`85–86 dB`) at higher volumes.

### 3.2 Power Supply Buying Rules

| Category | Recommended Power Supplies (Buy) | Hazardous Power Supplies (Avoid) |
| :--- | :--- | :--- |
| **Brand & Topology** | **Fosi Audio / Aiyima OEM Bricks** (`32V/5A` or `48V/5A GaN`), **Mean Well** industrial units (`GST90A19` / `GST160A24`), or **genuine Dell/Lenovo/HP 19V–20V laptop bricks**. | Unbranded generic `48V/10A` bricks from online marketplaces, adjustable multi-voltage rotary wall warts, or cheap `12V/2A` router adapters. |
| **Electrical Safety & Noise** | Proper UKCA/CE isolation, low switching ripple (`< 100 mV p-p`), genuine over-current/short-circuit protection, and tight barrel contacts. | Fake current ratings (voltage sags on bass transients), missing EMI filters (injects high-pitched whine into speakers), and poor mains isolation. |
| **Voltage Discipline** | Stay at least `2V–4V` below internal smoothing capacitor ratings (`25V` caps on TPA3116; `50V/63V` caps on TPA3255). | Feeding `24V+` into a TPA3116 board with `25V` capacitors—causes capacitor venting/failure. |

---

## 🔊 Section 4: Loudspeakers (UK Market — Richer Sounds & Active Pro)

| Tier & Model | UK Price | Impedance / Sensitivity | Freq. Range & Size (`H×W×D`) | Port & Acoustic Design | Best Piccolo \[AX\] Pairing & Character |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1A. Wharfedale Diamond 9.0** | **~£99** | `6 Ω` / `86 dB` | `60 Hz – 24 kHz`<br>`236 × 145 × 165 mm` | **Front-firing bass reflex** | **Nobsound NS-20G or Fosi V3.** Warm, smooth Kevlar midrange; front port allows placement flush against a shallow desk wall without bass boom. |
| **1B. JBL Control One** | **~£99** | `4–8 Ω` / `89 dB` | `80 Hz – 20 kHz`<br>`228 × 155 × 139 mm` | Front / Compact Studio (+ desk/wall brackets) | **Nobsound NS-20G or Fosi V3.** High `89 dB` efficiency; retro game SFX and synth leads jump out cleanly at low volume. |
| **2. Q Acoustics 3010i / 3010c** | **~£119 / £179** | `6 Ω` / `85–86 dB` | `60 Hz – 30 kHz`<br>`254 × 155 × 251 mm` | Rear port (foam bungs included) | **Fosi Audio V3 (`32V/48V` PSU).** Narrow `150–155 mm` front baffle projects a wide, fatigue-free 3D desktop stereo stage. |
| **3. Wharfedale Diamond 220** | **~£149** | `8 Ω` (`4.1 Ω` min) / `86 dB` | `56 Hz – 20 kHz`<br>`315 × 174 × 255 mm` | **Downward-firing slot port** | **Fosi Audio V3 or ZA3.** Vents through calibrated plinth gap to prevent rear-wall boom; reference acoustic neutrality. |
| **4. Fyne Audio F300i** | **~£179** | `8 Ω` / `87 dB` | `54 Hz – 34 kHz`<br>`250 × 156 × 211 mm` | Rear port | **TPA3116 (`24V`) or Fosi V3.** Easy `8 Ω` load grips bass down to `54 Hz` while titanium tweeters resolve DSD harmonics. |
| **5. Mission LX-1 MKII** | **~£184** | `8 Ω` (`4.4 Ω` min) / `86 dB` | `58 Hz – 20 kHz`<br>`260 × 160 × 208 mm` | Rear port + **Inverted Driver Geometry** | **Fosi Audio V3 or ZA3.** Tweeter mounted *below* woofer equalizes acoustic path lengths at seated ear height for pinpoint vocals/dialogue. |
| **6. Q Acoustics 3020c / Mission LX-2 MKII** | **~£219** | `6–8 Ω` / `87 dB` | `55–60 Hz – 30 kHz`<br>Larger `5"` Standmount | Rear port (`120–130 mm` woofer) | **Fosi Audio V3 / TB10D (`32V–48V`).** Full-bodied bass slam, symphonic weight, and maximum dynamic headroom. |
| **Pro. Wharfedale Pro Diamond Studio (5-BT / 7-BT)** | **~£199–£269** | Active Bi-Amped (`60W–100W` RMS) | `45–50 Hz – 20 kHz` | Front-ported studio monitor | **Direct from HiBy FC3 (`2.0 Vrms`).** Built-in discrete LF/HF Class-D amps with RCA/TRS/XLR inputs; skips external amp and speaker wire. |

---

## 🎧 Section 5: Wired Headphones & IEMs (`1.5–2.00 ms` Direct from HiBy FC3)

### 5.1 Open-Back & Closed-Back Wired Headphones

| Model | UK Price | Design & Weight | Impedance / Sensitivity | Music | Gaming | Movies | Sonic Signature & `FLAC_AX` Gain Note |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Koss Porta Pro** | **~£39** | Open On-Ear (`60 g`) | `60 Ω` / `101 dB` | ★★★★☆ | ★★★☆☆ | ★★★☆☆ | Lightweight retro comfort; punchy warmth on any dongle. |
| **Sennheiser HD400S** | **~£59** | Closed Over-Ear (`217 g`) | `18 Ω` / `120 dB` | ★★★☆☆ | ★★★★☆ | ★★★★☆ | Budget closed-back bass warmth; set `--volume 0.25`. |
| **Beyerdynamic DJ 300 Pro X** | **~£85** | Closed On/Over-Ear (`315 g`) | `48 Ω` / `109 dB` | ★★★★☆ | ★★★★☆ | ★★★★★ | Visceral `5 Hz` sub-bass extension and `20 dB` passive isolation. |
| **Austrian Audio Hi-X15 / Hi-X20** | **~£89 / £129** | Closed Over-Ear (`255 g`) | `25 Ω` / `113 dB/V` | ★★★★☆ | **★★★★★** | ★★★★☆ | Fast, snappy closed-back monitoring; blocks room noise & mic bleed. |
| **Grado SR60x / SR80x / Classic SR60** | **~£99 – £125** | Open On-Ear | `38 Ω` / `98–99.8 dB` | **★★★★★** | ★★★★☆ | ★★★☆☆ | Brooklyn X-drivers; explosive transient snap for rock, synth & arcade. |
| **Sennheiser HD 560S** | **~£99 – £139** | Open Over-Ear (`240 g`) | `120 Ω` / `110 dB/V` | **★★★★★** | **★★★★★** | ★★★★☆ | Angled transducers, flat studio accuracy & wide stage; FC3's `2V` drives `120 Ω` easily (`--volume 0.65–0.80`). |
| **Sennheiser HD 569** | **~£119 – £149** | Closed Over-Ear | `23 Ω` / `115 dB/V` | ★★★★☆ | **★★★★★** | **★★★★★** | Deep sub-bass extension and plush velour comfort for movies & RPGs. |
| **Grado SR325x / Classic** | **~£299 – £329** | Open On-Ear (Alloy, `280 g`) | `38 Ω` / `98–99.8 dB` | **★★★★★** | ★★★★☆ | ★★★☆☆ | CNC-machined aluminium cups; flagship DSD detail straight out of HiBy FC3. |

### 5.2 In-Ear Monitors (IEMs — `-26 dB` Passive Isolation & 2-in-1 USB-C DSP Options)

> **Crucial `FLAC_AX` Gain Rule:** When plugging `16–32 Ω` IEMs into `hw:CARD=FC3,DEV=0`, buy the **`3.5 mm`** cable version and drop volume to **`--volume 0.20–0.25`** before playback. Choose the **`USB-C DSP`** editions (which embed a `24-bit/96kHz` DAC inside the USB-C plug) for standalone 2-in-1 use on phones/PCs without a separate dongle.

| Category & Model | UK Price | Driver Topology | Impedance / Sensitivity | Built-In USB-C DAC? | Best Use Case |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Under £30: Moondrop Chu II / DSP** | **~£16 – £24** | `1×` Al-Mg Dynamic (`6.4 g/ear`) | `18 Ω` / `119 dB/V` | **Yes (`Chu II DSP 24/96`)** | Sub-£25 metal-shell 2-in-1 all-rounder (`3.5mm` for FC3). |
| **Under £30: 7Hz × Crinacle Zero:2** | **~£22 – £25** | `1×` PU+Metal Dynamic | `32 Ω` / `108 dB/V` | **Optional USB-C DAC version** | Sub-£25 `+3 dB` warm sub-bass, gaming & movie immersion. |
| **Over £30: Moondrop May** | **~£65 – £75** | **`1DD + 1 Planar` Hybrid** | `30 Ω` / `120 dB/V` | **Yes (`FreeDSP 24/96` + 5-band PEQ)** | Flagship 2-in-1 USB-C DAC + planar hybrid (swappable `0.78 mm` 2-pin). |
| **Over £30: Truthear Hexa** | **~£75 – £85** | **`1DD + 3BA` Hybrid** | `20.5 Ω` / `120 dB/V` | Optional (`0.78 mm` 2-pin cable) | Neutral 3-way studio reference & competitive FPS gaming. |

---

## 📐 Section 6: Physical Acoustics, OFC Cabling & Gain Staging

### 6.1 Nearfield Desk Geometry & Boundary EQ

1. **Equilateral Triangle & Toe-In:** Space left and right tweeters `70–100 cm` apart matching the distance to your ears, angled inward (`toe-in`) by `15°–20°`.
2. **Acoustic Isolation Pads (~£10–£15):** Raise tweeters to ear height on angled foam isolation pads to prevent `150–400 Hz` desk-bounce chestiness and cabinet vibration transfer.
3. **Rear-Wall Boundary Control:** Leave `15–25 cm` behind rear-ported speakers (Q Acoustics 3010i/3020c, Mission LX-1/LX-2 MKII) or insert the included **foam port bungs** (or choose the front-ported **Wharfedale Diamond 9.0** / downward-ported **Diamond 220**).
4. **Desktop OS Subtractive EQ (When Not Using Bare-Metal `flac_ax`):** Cut `-2.5` to `-4.0 dB` at `100–140 Hz` (`Q=2.0`) for rear-wall boom; `-2.0` to `-3.0 dB` at `220–320 Hz` (`Q=2.5`) for desk bounce; `-1.0` to `-1.5 dB` high shelf (`6–10 kHz`) for nearfield tweeter glare. *(Note: Bare-metal `flac_ax` bypasses all host-side DSP for bit-perfect `S32_LE`/DoP playback—rely on physical placement.)*

### 6.2 OFC Speaker Cable + 4 mm Banana Plugs

* **Pure Oxygen-Free Copper (OFC) vs. CCA:** Avoid thin `24 AWG` copper-clad aluminium (CCA) wire (`~55%` higher resistance). Use pure **16 AWG (`1.3 mm²`) to 14 AWG (`2.0 mm²`) OFC**—such as **QED Classic 42** (`~£1.70/m`) or **QED XT25** (`~£5.99/m`) at Richer Sounds—keeping loop resistance `< 0.05 Ω` to preserve Class-D damping factor and LC filter linearity.
* **4 mm Banana Plugs (~£10–£15):** Terminate all bare ends with **gold-plated 4 mm banana plugs** (e.g., QED Screwloc or Fisual Easy Fix) so stray copper whiskers cannot bridge tightly spaced mini-amp binding posts and short-circuit the TPA3116/TPA3255 output stage. Strictly match **Red (`+`) to Red (`+`)** and **Black (`–`) to Black (`–`)**.

### 6.3 Gain-Staging Summary

* **Speaker Mode:** Set `flac_ax --volume 0.80` (and HiBy FC3 hardware buttons near max) so the DAC outputs a clean `~1.6–2.0 Vrms` signal, then control room volume solely on the amplifier's physical dial (always turn the amp knob down before powering on).
* **High-Impedance Open-Back (`120 Ω` HD 560S):** Start at `--volume 0.65–0.80`.
* **Sensitive IEMs / Low-Impedance (`16–32 Ω` Chu II, Zero:2, HD400S):** Always drop to `--volume 0.20–0.25` before plugging in.

---

## 🛠️ Section 7: Complete Tiered System Blueprints (UK Pricing)

| System Tier | USB Front-End (`Piccolo [AX]`) | Class-D Amp & PSU | Speakers + Personal Audio | Cabling & Isolation | Approx. Total |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **0. Desk IEM Starter** | New HiBy FC3 w/ Display (`£35`) | *None (Direct from FC3)* | **Moondrop Chu II (`3.5mm`)** (`£16–£19`) | *Included* | **~£51 – £54** |
| **1. Ultra-Budget Rig** | Older HiBy FC3 (`~£30` used) | Nobsound NS-20G (`£35`) + 19V/4.74A Laptop PSU (`£15`) | **Wharfedale Diamond 9.0** (`£99`) *or* **JBL Control One** (`£99`) + **Chu II** (`£19`) or **Koss Porta Pro** (`£39`) | 3 m QED Classic 42 (`£5`) + 8× Banana Plugs (`£12`) | **~£215 – £235** |
| **2. Compact Audiophile** | New HiBy FC3 w/ Display (`£35`) | Stereo `TPA3116` + 24V PSU (`~£45`) | **Fyne Audio F300i** (`£179`) | 3 m QED Classic 42 (`£5`) + 8× Banana Plugs (`£12`) + Foam Pads (`£12`) | **~£288** |
| **3. Sweet-Spot Hi-Fi** | New HiBy FC3 w/ Display (`£35`) | Fosi Audio V3 w/ 32V/5A PSU (`~£95`) | **Q Acoustics 3010i** (`£119`) + **Austrian Audio Hi-X15** (`£89`) *or* **Grado SR80x** (`£119`) | 3 m QED Classic 42 (`£5`) + 8× Banana Plugs (`£12`) + Foam Pads (`£12`) | **~£367 – £397** |
| **4. Reference Studio** | New HiBy FC3 w/ Display (`£35`) | Fosi Audio V3 w/ 48V/5A GaN PSU (`~£105`) | **Wharfedale Diamond 220** (`£149`) *or* **Mission LX-1 MKII** (`£184`) + **Sennheiser HD 560S** (`£99`) | 3 m QED XT25 (`£18`) + 8× Banana Plugs (`£12`) + Foam Pads (`£12`) | **~£430 – £465** |
| **5. Ultimate Head-Fi & Room** | New HiBy FC3 w/ Display (`£35`) | Fosi Audio V3 w/ 48V/5A GaN PSU (`~£105`) *(or Active Wharfedale Pro £229)* | **Mission LX-2 MKII** (`£219`) *or* **Q Acoustics 3020c** (`£219`) + **Grado SR325x** (`£299–£329`) | 3 m QED XT25 (`£18`) + 8× Banana Plugs (`£12`) + Foam Pads (`£12`) | **~£700 – £730** |

---

## 🐧 Section 8: Universal Real-Time Linux (`6.12-rt` / Buildroot / Desktop) Setup

This hardened configuration works identically across **bare-metal Buildroot/Batocera (`Piccolo [AX]`)** and **desktop Linux (`Ubuntu/Kubuntu 24.04+` or `piccolo-ax-desktop`)**.

### 8.1 Universal `udev` Rules & Real-Time Limits

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

# Speakers (TPA3116/TPA3255) or Sennheiser HD 560S (120 Ohm)
~/bin/ax-launch.sh music 0.80 ~/Music/FLAC_Album/

# Sensitive 16-18 Ohm IEMs/headphones (Moondrop Chu II/III, Sennheiser HD400S)
~/bin/ax-launch.sh music 0.25 ~/Music/FLAC_Album/

# Instant handoff to 2.00 ms synth workstation & verify Core 0 SCHED_FIFO 80
~/bin/ax-launch.sh synth 0.75 &
ps -eLo pid,cls,rtprio,psr,comm | grep ax_av_studio
```

---

## 🪟 Section 9: Windows 10/11 Dual-Boot Fallback Setup

1. **Disable Audio Enhancements:** Open *Sound Settings → Device Properties → Additional Device Properties → Enhancements* and check **Disable all enhancements** (plus disable Windows Sonic / Spatial Sound) to cut `~20–30 ms` of OS buffer delay.
2. **Match Sample Rate / WASAPI Exclusive:** Set default format under *Advanced* to **24-bit, 44100 Hz** (music) or **24-bit, 48000 Hz** (gaming/movies), or enable **WASAPI Exclusive Mode** in Foobar2000/MusicBee.
3. **Gain Staging:** Keep Windows volume at **100%** when feeding a `TPA3116`/`TPA3255` speaker amp and adjust volume via the physical amp dial; drop Windows volume to **20–30%** before plugging in sensitive `16–18 Ω` IEMs/headphones.
## 🪟 Section 9: Windows 10/11 Dual-Boot Fallback Setup

1. **Disable Audio Enhancements:** Open *Sound Settings → Device Properties → Additional Device Properties → Enhancements* and check **Disable all enhancements** (plus disable Windows Sonic / Spatial Sound) to cut `~20–30 ms` of OS buffer delay.
2. **Match Sample Rate / Exclusive Bit-Exact Mode:** Set the default format under *Advanced* to **24-bit, 44100 Hz** (music) or **24-bit, 48000 Hz** (gaming/movies). For bit-perfect, bypass-mixer playback, use **ASIO** (`FlexASIO` in Exclusive mode targeting the HiBy FC3 `VID 0x32BB`) or **WASAPI Exclusive Mode** in Foobar2000 / MusicBee / `flac_ax`.
3. **Gain Staging:** Keep Windows master volume at **100%** when feeding a `TPA3116` speaker amplifier and control listening level solely via the physical amp dial; drop Windows volume to **20–30%** before plugging in sensitive `16–18 Ω` IEMs or headphones.
