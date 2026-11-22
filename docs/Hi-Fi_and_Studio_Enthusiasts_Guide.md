🎧 FLAC_AX & AX AV Studio: Turning Classic PC Hardware into a Pure Hi-Fi Transport & Live Synth Rig
How an open-source, bare-metal Linux audio engine delivers boutique streamer purity and instant analog-modeled synthesis—without the £1,500 price tag.

🧰 What You Need to Get Started
Unlike boutique streamers or rack-mounted studio gear that lock you into expensive proprietary ecosystems, FLAC_AX and AX AV Studio run on approachable, proven hardware:
🖥️ A Multi-Core x86_64 PC (6 Cores Recommended): A classic desktop processor such as an Intel Core i5-9600K or i5-8600K with 16–32 GB of RAM running Linux (Piccolo [AX] real-time kernel or standard desktop Linux).
🔌 HiBy FC3 Portable USB DAC (VID 0x32BB, ESS Sabre ES9281AC Pro): A pocket-sized, £29.67–£35 audiophile digital-to-analog converter that unlocks native 384 kHz / 32-bit PCM and DSD64 / DSD128 playback straight into your headphones (Grado SR325x) or desktop stereo amplifier (Nobsound Q10 + Wharfedale Diamond 9.0).
🎹 Optional USB MIDI Controller (Akai MPK Mini MK3): Needed only if you want to play, patch, and sculpt sounds live in AX AV Studio—completely optional if you are using flac_ax purely as a hi-fi music transport.
📺 Standard HDMI / DisplayPort Monitor: Optional for headless hi-fi playback, or used to display the 60 FPS studio oscilloscope and 64-band FFT spectrum analyzer.

🎬 The Big Picture: Why Your Everyday PC Isn’t Built Like a Hi-Fi Component or Hardware Synth
If you’ve ever read What Hi-Fi? on the quiet authority of a dedicated network transport, or read Empire interviews where film composers explain why they still keep classic analog synthesizers within arm's reach of their scoring rig, you already know the secret: general-purpose computers are terrible at staying out of the way.
When you hit "Play" on a typical Windows or Mac desktop—or trigger a chord in a software DAW—your audio is quietly pushed through a crowded digital motorway. Background notifications, browser tabs, OS mixers, and power-saving USB polling all jostle for the processor's attention. For hi-fi listeners, that means timing jitter and a subtle haze over acoustic decays; for musicians, it means sluggish key-to-ear latency that robs a bassline or lead patch of its rhythmic snap.
FLAC_AX (flac_ax) and AX AV Studio (ax_av_studio) solve both problems by treating a regular multi-core desktop PC not as an office machine, but as a dedicated, bare-metal audiophile transport and live hardware-feel synthesizer.

🎼 Part 1: flac_ax — A Purist’s Digital Transport in Plain English
Think of flac_ax as the digital equivalent of a heavyweight, vibration-isolated CD transport—except it plays high-resolution FLAC files and ultra-high-resolution DSD (Super Audio CD) studio masters straight from memory.
1. Whole-Album Memory Loading (Zero Storage Bus Noise)
Most music players read tiny chunks of a song off your SSD or network every few milliseconds while the music is playing, constantly waking the storage controller mid-phrase.
The flac_ax approach: Before a single note plays, flac_ax loads the entire album straight into your computer's high-speed RAM (mlockall). Once playback begins, your drive goes completely idle—eliminating storage interrupts anywhere near the audio clock.
2. A Direct Wire to Your HiBy FC3 DAC (No Mixers, No Resampling)
Normally, your operating system acts like an unwanted mixing desk, converting sample rates, blending system alerts, and scaling volume digitally before handing audio to your DAC.
The flac_ax approach: It bypasses every software mixer and volume slider on the computer, opening a direct, bit-for-bit 32-bit hardware pipeline (hw:CARD=FC3,DEV=0) straight into the HiBy FC3. Whether you are spinning a 44.1 kHz CD rip, a 192 kHz / 32-bit studio master, or native DSD64 / DSD128 (DoP) recordings, the ESS Sabre chip receives the exact ones and zeros the mastering engineer printed.
3. Dedicated "Traffic Lanes" Inside Your Processor
A modern 6-core CPU is like a six-lane motorway. On a normal PC, Windows or macOS lets every background process swerve across all six lanes at once. FLAC_AX sets up permanent concrete barriers—a 2:3:1 physical core topology:
The Express Lane (Dedicated Audio Cores): Reserved exclusively for feeding bit-exact audio to your DAC. Nothing else is permitted in this lane.
The Prep Lane (Loading & MIDI Core): Handles reading album files into RAM and polling your MIDI controller in the background.
The Display Lane (Visual Cores): Drives the 60 FPS screen graphics without ever stealing a single microsecond from the audio engine.

🎹 Part 2: ax_av_studio — Classic Subtractive Synthesis & Studio FX at 2.0 ms Latency
While flac_ax is built for pure, sit-back listening, ax_av_studio turns the exact same machine into a stage- and studio-ready electronic instrument.
Plug in an Akai MPK Mini MK3, and ax_av_studio responds in 2.00 milliseconds (96 samples at 48 kHz)—faster than the time it takes acoustic sound to travel two feet from a studio monitor to your ears, and tighter than most standalone digital hardware synths.
16-Voice Dual-Oscillator Subtractive Synth: Built in the spirit of classic polyphonic analog workhorses (think Prophet-5 meets Juno-106), pairing a harmonically rich ramp/sawtooth main oscillator with a -1 octave square-wave sub-oscillator, odd/even stereo voice panning, a 2-pole resonant State-Variable Filter (SVF), and a cubic soft-clipper for warm console-style saturation.
8-Voice Synthesized Drum Engine: Dedicated percussion synthesis voices combining pitch-swept sine/triangle body oscillators with filtered white-noise transient bursts—delivering punchy 808/909-style kicks, snappy snares, and crisp metallic hats straight from the MPK Mini's 16 velocity-sensitive pads.
Cinema-Grade Live Visuals: On your monitor, a VBlank-locked 60 FPS stereo oscilloscope and 64-band logarithmic FFT spectrum analyzer dance in lockstep with every note, filter sweep, and drum transient.

⚖️ How Does It Compare to Boutique Hi-Fi & Studio Gear?
Feature
💿 Subscription Hi-Fi Apps (Roon / Audirvana)
🎛️ Standalone Hardware Boxes (Elektron / MPC Live)
⚡ FLAC_AX & AX AV Studio
Sound Purity
Very high, but still shares the PC with background OS tasks
Clean, self-contained hardware path
Direct-to-DAC bit-perfect path with whole-album RAM playback
DSD & Studio Masters
Supported (requires ongoing paid subscription)
Limited to standard 44.1 / 48 kHz internal audio
Native FLAC (384 kHz) & DSD64 / DSD128 out of the box
Synth & Drum Architecture
Playback only—not a musical instrument
Fixed-architecture embedded DSP (~3–5 ms latency)
16-voice saw/sub SVF synth + 8-voice drum synth + stereo reverb & tape delay at 2.00 ms
Visual Feedback
Album artwork & signal-path text
Small built-in LCD screen
Full-screen 60 FPS stereo oscilloscope & 64-band log FFT analyzer
Price Tag
£120–£700+ software fees + dedicated streamer
£900–£1,500+ dedicated hardware unit
Free & open-source—runs on preserved desktop PC hardware + a £29.67–£35 HiBy FC3 DAC


🎛️ Part 3: Inside the Three-Engine Sound Trio & MIDI Control Map
Under the hood, ax_av_studio chains three purpose-built DSP engines in series—so your synth leads, chords, and drum hits flow directly through a lush stereo acoustic chamber and a warm vintage tape echo before hitting the HiBy FC3 DAC. Every key, pad, thumbstick axis, and rotary encoder on the Akai MPK Mini MK3 maps directly to a musical parameter across these three engines:
1. The Three Sound & Effects Engines
🎹 Engine 1: 16-Voice Saw/Sub SVF Synth & 8-Voice Drum Synth (FullFeatureSynthEngine)
Architecture: Each of the 16 melodic voices stacks a primary sawtooth oscillator with a -1 octave square-wave sub-oscillator, shaped by an ADSR amplitude envelope (2 ms–600 ms attack, 180 ms decay to 75% sustain, 20 ms–1.8 s release) and routed through a stereo 2-pole State-Variable Filter (SVF) with cubic soft-clip drive (1.0x–4.5x). Simultaneously, an 8-voice percussion engine synthesizes pitch-enveloped sine/noise kicks, snares, and hats across the stereo field.
Musical character: Thick, commanding sub-bass weight, liquid resonant filter sweeps, and punchy transient attack that never steals voices during two-handed chord voicings.
🌊 Engine 2: Stereo Acoustic Chamber (StereoReverbDsp)
Architecture: A 4-tap prime-spaced stereo comb-filter reverb network with independent left/right delay lengths and per-tap one-pole high-frequency damping.
Musical character: Adds natural studio room air and depth around dry sawtooth leads and snare hits while rolling off harsh high-end metallic reflections—reminiscent of classic early digital studio reverbs.
📼 Engine 3: Damped Stereo Tape Echo (TapeDelayDsp)
Architecture: A stereo interpolated delay line (5 ms–950 ms) with an 11% right-channel stereo time offset, high-frequency feedback damping, and soft-saturation in the regeneration loop.
Musical character: Creates wide, spacious Space Echo-style rhythmic repeats that naturally darken and saturate with each regeneration, gluing staccato arpeggios and synth stabs into a cohesive mix.
2. Akai MPK Mini MK3 Hardware-to-Engine Control Map
MPK Mini MK3 Control
Target Sound Engine
Musical Function & Sound-Design Behavior
🎹 25-Key Keybed (Ch 1)
16-Voice Melodic Synth
Plays up to 16 polyphonic saw + sub-octave voices with velocity-sensitive dynamics
🥁 Pads 1–16 (Ch 10 / Ch 2)
8-Voice Drum Synth
Triggers velocity-sensitive synthesized kicks, snares, and hi-hats panned across the stereo image
🕹️ Thumbstick X (Left / Right)
Melodic Oscillators
Smooth ±2 semitone pitch bend across all active melodic voices
🕹️ Thumbstick Y (CC 1)
LFO & SVF Cutoff
Introduces expressive pitch vibrato (4.5–9.5 Hz) while opening the low-pass filter cutoff
🦶 Sustain Pedal (CC 64)
Voice Allocation / ADSR
Holds sustained notes at their sustain level until damper pedal release
🎛️ Knob K1 (CC 70)
2-Pole SVF Cutoff
Sweeps the low-pass filter from warm sub-bass (5%) to wide-open harmonic brilliance (95%)
🎛️ Knob K2 (CC 71)
2-Pole SVF Resonance
Emphasizes the cutoff frequency (5%–90%) for vocal formants and acid-style squelch
🎛️ Knob K3 (CC 72)
ADSR Attack
Shapes note onset along a musical quadratic curve from a 2 ms percussive pluck to a 600 ms pad swell
🎛️ Knob K4 (CC 73)
ADSR Release
Extends note decay tails from tight 20 ms staccato stabs to 1.8 s ambient chords
🎛️ Knob K5 (CC 74)
Sub-Oscillator Mix
Blends in the -1 octave square-wave sub-oscillator for authoritative low-end weight
🎛️ Knob K6 (CC 75)
Pre-Filter Soft Clipper
Pushes harmonic drive (1.0x–4.5x) into the SVF for warm analog console saturation
🎛️ Knob K7 (CC 76)
Voice Spread + Stereo Reverb
Widens odd/even voice stereo panning while smoothly increasing StereoReverbDsp wet ambience (8%–40%)
🎛️ Knob K8 (CC 77)
Master Output Bus
Controls final master bus level feeding the HiBy FC3 32-bit DAC stage
🌊 Bank B CC 78 / CC 79
Stereo Reverb & Tape Delay
Directly dials in deep hall size/mix (CC 78) and regenerative stereo tape echo trails (CC 79)

3. Why It Sounds Pure and Responds in 2.0 Milliseconds
Zero Zipper Noise or Buffer Glitches: Every delay line, comb filter, and lookup table across all three DSP engines is statically allocated upfront with zero heap calls on the audio thread. Delay-time transitions use sample-by-sample exponential smoothing, so sweeping knobs feels as liquid and immediate as turning a physical potentiometer on an analog synthesizer.
Lock-Free 2.00 ms Signal Chain (Core 5 → Core 0 → HiBy FC3): The instant your finger strikes a key or twists an encoder on the MPK Mini MK3, Core 5 captures the raw MIDI byte stream into a cache-line-aligned lock-free ring buffer and passes it straight to the real-time synthesis core—delivering unbuffered 32-bit audio to the HiBy FC3 in 96 samples (2.00 ms at 48 kHz) with zero pops, clicks, or OS hesitation.

🏁 The Bottom Line
You don’t need a £2,000 brushed-aluminum network streamer to hear what your hi-fi system is truly capable of, nor do you need a rack full of vintage studio hardware to feel zero-latency analog-style synthesis under your fingers. Pair a preserved multi-core PC with a £29.67–£35 HiBy FC3—feeding a Nobsound Q10 + Wharfedale Diamond 9.0 desktop rig or Grado SR325x open-back headphones (plus an optional Akai MPK Mini MK3)—and FLAC_AX turns everyday silicon into a dead-quiet audiophile transport and an expressive, stage-ready synthesizer rig.
