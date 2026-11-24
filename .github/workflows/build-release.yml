name: Build Release Artifacts

on:
  workflow_dispatch:
    inputs:
      skylake_arch:
        description: "Enable Skylake / AVX2 tuning (-march=skylake / BMI/BMI2)"
        type: boolean
        default: true

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - name: Checkout FLAC_AX
        uses: actions/checkout@v5

      - name: Install Build Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends \
            clang lld cmake ninja-build pkg-config \
            libflac++-dev libasound2-dev libopenmpt-dev libsdl2-dev libgl1-mesa-dev

      - name: Configure & Build (Release + LTO, Clang)
        run: |
          cmake -S . -B build -G Ninja \
            -DCMAKE_C_COMPILER=clang \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_BUILD_TYPE=Release \
            -DFLAC_AX_USE_AX_SDK=ON \
            -DFLAC_AX_BUILD_STUDIO=ON \
            -DFLAC_AX_BUILD_UPSCALER=ON \
            -DFLAC_AX_BUILD_APPLIANCE_TUI=ON \
            -DFLAC_AX_BUILD_ENCODE_2GO=ON \
            -DFLAC_AX_ENABLE_NATIVE_OPT=${{ inputs.skylake_arch }} \
            -DFLAC_AX_ENABLE_LTO=ON
          ninja -C build -j"$(nproc)"
          strip build/flac_ax build/ax_av_studio build/ax_video_upscaler build/ax_appliance_tui

      - name: Upload Linux Release Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: flac-ax-linux-x86_64
          path: |
            build/flac_ax
            build/ax_av_studio
            build/ax_video_upscaler
            build/ax_appliance_tui
          retention-days: 14

  build-windows:
    runs-on: windows-latest
    steps:
      - name: Checkout FLAC_AX
        uses: actions/checkout@v5

      - name: Configure & Build (Release + LTO, clang-cl)
        shell: pwsh
        run: |
          $env:CXXFLAGS="-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH=1"
          cmake -S . -B build -G Ninja `
            -DCMAKE_C_COMPILER=clang-cl `
            -DCMAKE_CXX_COMPILER=clang-cl `
            -DCMAKE_BUILD_TYPE=Release `
            -DFLAC_AX_USE_AX_SDK=ON `
            -DFLAC_AX_BUILD_STUDIO=ON `
            -DFLAC_AX_BUILD_UPSCALER=ON `
            -DFLAC_AX_BUILD_APPLIANCE_TUI=OFF `
            -DFLAC_AX_BUILD_ENCODE_2GO=ON `
            -DFLAC_AX_ENABLE_NATIVE_OPT=${{ inputs.skylake_arch }} `
            -DFLAC_AX_ENABLE_LTO=ON
          cmake --build build --config Release --parallel

      - name: Upload Windows Release Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: flac-ax-windows-x86_64
          path: |
            build/flac_ax.exe
            build/ax_av_studio.exe
            build/ax_video_upscaler.exe
          retention-days: 14
