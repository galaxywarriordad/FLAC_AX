# FLAC_AX

### A deterministic bare-metal Linux semi-pro audio player for `[AX]`, engineered for low-latency, zero-underrun playback harnessing the ESS Sabre ES9281PRO and ES9281AC Pro architecture.

`FLAC_AX` is a lean, deterministic bare-metal Linux semi-pro audio player built for the `[AX]` ecosystem, designed to harness the **ESS Sabre ES9281PRO** and **ES9281AC Pro** 32-bit HyperStream® II Quad DAC™ / Time Domain Jitter Eliminator architecture with minimal system overhead. Pairing non-real-time CPU decoding and lock-free ring-buffer transport with a `SCHED_FIFO` ALSA output path (`hw:...`), it targets bit-perfect high-resolution PCM playback (up to 32-bit/384 kHz) and DoP/DSD workflows while keeping hardware preservation and appliance simplicity front and center.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Enable `ax-sdk` (`v1_35_1c`) via `FetchContent`

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLAC_AX_USE_AX_SDK=ON
cmake --build build
```

## Run

```bash
./build/flac_ax /path/to/track.flac hw:CARD=FC3,DEV=0
```

## License

MIT — see [LICENSE](LICENSE).
