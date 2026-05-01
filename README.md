# TestTone

A minimal test-signal generator built on the same JUCE + WebView (Vite / React 19 / MUI 7) stack as the sibling plugins (ZeroComp / ZeroLimit / ZeroEQ). Generates a sine wave or pink noise; frequency and level are dialed in via a horizontal slider plus quick-jump preset buttons; an On / Off button mutes the output completely.

Ships as **VST3 / AU / AAX / Standalone** on Windows / macOS and **VST3 / LV2 / CLAP / Standalone** on Linux, plus a **WebAssembly browser demo that reuses the exact same DSP**.
https://testtone-demo.web.app/

<img width="471" height="349" alt="image" src="https://github.com/user-attachments/assets/f43d29a2-1133-43ca-84e9-9e1c3003120f" />

## Features

- One-click toggle between **Sine** and **Pink Noise** (the Frequency UI is disabled in Pink Noise mode).
- **Frequency**: 20 Hz .. 20 kHz (log skew, default 1 kHz).
  Presets: 20 / 40 / 50 / 60 / 100 / 440 / 1k / 2k / 10k / 20k Hz. Direct numeric entry is supported (including shorthand like `1k` or `1.5k`).
- **Level (dBFS)**: -90 .. 0 dB (default -18 dB).
  Presets: 0 / -0.1 / -1 / -3 / -6 / -12 / -18 / -20 / -24 / -40 dB. Direct numeric entry supported.
- **L / R** segmented toggle to mute either channel independently. Auto-collapses to a single **M** (mono) toggle when the host bus is mono.
- **On / Off** button with a hard-mute when off (output is exactly zero — safe to insert without surprises).
- Plugin window is **fixed at 450 × 265 px** (not resizable; enforced for both the embedded plugin and the Standalone host window).
- Slider fine-adjust on **Ctrl / Cmd / Shift + drag**; modifier + click resets to default. Numeric fields carry `block-host-shortcuts` so DAW key bindings don't fire while you type.
- All tooltips are in English.

The UI uses MUI v7 with a dark theme and the Jost / Red Hat Mono fonts.

## Requirements

- CMake 3.22+
- A C++17 toolchain
  - Windows: Visual Studio 2022 (Desktop development with C++ workload)
  - macOS: Xcode 14+
  - Linux: gcc 13+ / clang + the apt packages listed under [Building on Linux](#building-on-linux)
- Node.js 18+ and npm (for the WebUI build)
- JUCE (included as a git submodule — run `git submodule update --init --recursive`)
- `clap-juce-extensions` (also a git submodule, used only for the Linux CLAP target)
- Optional: AAX SDK (place under `aax-sdk/` to enable AAX builds)
- Optional: Inno Setup 6 (for the Windows installer)
- Optional: [Emscripten](https://emscripten.org) (for building the Web demo WASM; this repo expects an emsdk checkout at `D:/Synching/code/JUCE/emsdk` on Windows)

## Getting started

```bash
# 1. Clone with submodules
git submodule update --init --recursive

# 2. Install WebUI dependencies
cd webui && npm install && cd ..

# 3. Build (production)
# Windows
powershell -ExecutionPolicy Bypass -File build_windows.ps1 -Configuration Release
# macOS
./build_macos.zsh
# Linux (see "Building on Linux" below)
bash build_linux.sh
```

The Windows production script builds the WebUI, compiles VST3 / AAX / Standalone, signs the AAX (when `.env` provides PACE credentials), packages a ZIP, and runs Inno Setup to produce `TestTone_<VERSION>_Windows_Setup.exe`. Output lands under `releases/<VERSION>/Windows/`.

### Building on Linux

Tested on **WSL2 Ubuntu 24.04**, but should work on any modern glibc-based distro with `webkit2gtk-4.1` available.

```bash
sudo apt update
sudo apt install -y \
  build-essential pkg-config cmake ninja-build git \
  libasound2-dev libjack-jackd2-dev libcurl4-openssl-dev \
  libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev \
  libwebkit2gtk-4.1-dev libglu1-mesa-dev mesa-common-dev libgtk-3-dev

git submodule update --init --recursive   # JUCE + clap-juce-extensions
bash build_linux.sh                        # Release VST3 / LV2 / CLAP / Standalone + zip
```

Output:

- Build artefacts: `build-linux/plugin/TestTone_artefacts/Release/{VST3,LV2,CLAP,Standalone}/`
- Auto-installed: `~/.vst3/TestTone.vst3`, `~/.lv2/TestTone.lv2`, `~/.clap/TestTone.clap`
- Distribution zip: `releases/<VERSION>/TestTone_<VERSION>_Linux_VST3_LV2_CLAP_Standalone.zip`

LV2 and CLAP are gated behind `if(UNIX AND NOT APPLE)` in CMake, so existing Windows / macOS release flows are unaffected. AU and AAX are skipped on Linux as expected.

### Manual CMake build (development)

```bash
# Windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --target TestTone_VST3

# macOS
cmake -B build -G Xcode
cmake --build build --config Debug --target TestTone_VST3
```

### Dev mode (hot-reload WebUI)

```bash
# Terminal A: Vite dev server
cd webui && npm run dev

# Terminal B: build & launch the Standalone in Debug
cmake --build build --config Debug --target TestTone_Standalone
```

Debug builds load the WebUI from `http://127.0.0.1:5173`. Release builds embed the bundled assets via `juce_add_binary_data`.

### Web demo (WebAssembly)

The same DSP (`tt::dsp::ToneGenerator`) is mirrored as JUCE-free C++ in `wasm/src/tone_generator.h`, compiled to WebAssembly with Emscripten, and driven by an `AudioWorklet` in the browser. The React UI is reused verbatim — `vite.config.web.ts` swaps `juce-framework-frontend-mirror` for a local shim (`src/bridge/web/juce-shim.ts`) that owns parameter state and forwards changes through `WebAudioEngine` → AudioWorklet → WASM.

> **Important:** if you change the DSP, update `wasm/src/tone_generator.h` to match and rebuild the WASM. Otherwise the browser demo will silently drift from the plugin.

#### Build the WASM (Windows)

```powershell
& 'D:\Synching\code\JUCE\emsdk\emsdk_env.ps1'
cd D:\Synching\code\JUCE\TestTone\wasm
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
New-Item -ItemType Directory build | Out-Null
cd build
emcmake cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
# Mirror the artifact into the locations the Vite dev server / build serve from
Copy-Item -Force dist\testtone_dsp.wasm ..\dist\
Copy-Item -Force dist\testtone_dsp.wasm ..\..\webui\public-web\wasm\
```

If `emsdk_env.ps1` hangs, set the env vars manually and call `emcmake.bat` directly:

```powershell
$env:EMSDK_PYTHON = 'D:\Synching\code\JUCE\emsdk\python\3.13.3_64bit\python.exe'
$env:EM_CONFIG    = 'D:\Synching\code\JUCE\emsdk\.emscripten'
$env:EMSDK        = 'D:\Synching\code\JUCE\emsdk'
$env:PATH         = 'D:\Synching\code\JUCE\emsdk\upstream\emscripten;' + $env:PATH
emcmake cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

#### Build the WASM (macOS / Linux)

```bash
source /path/to/emsdk_env.sh
cd wasm && ./build.sh
```

#### Run / build / deploy the Web demo

```bash
# Web demo dev server
cd webui && npm run dev:web      # http://127.0.0.1:5174

# Web demo production build
npm run build:web                # emits webui/dist with index.html, /wasm and /worklet

# Deploy to Firebase Hosting (project alias: testtone-demo)
npm run deploy:web               # requires the firebase CLI; first run: `firebase login`
```

> Browsers can only resume an `AudioContext` after a user gesture, so the Web demo shows a **START** overlay on first load. Clicking it bootstraps the AudioContext + AudioWorklet + WASM, after which the regular UI becomes interactive.

The drawer menu in the Web demo links to the sister plugins. Its source of truth is `web_demos.json` at the parent `JUCE/` directory (shared across all sibling repos); `npm run dev:web` / `npm run build:web` automatically mirror it into `webui/src/assets/web_demos.json` via `scripts/sync-web-demos.cjs`.

## Parameters (APVTS)

| ID          | Type             | Range              | Default | Notes                                                                              |
| ----------- | ---------------- | ------------------ | ------- | ---------------------------------------------------------------------------------- |
| `TONE_TYPE` | choice           | Sine / Pink Noise  | Sine    | When set to Pink Noise, the `FREQUENCY` UI is disabled.                            |
| `FREQUENCY` | float (Hz, log)  | 20 .. 20000        | 1000    | Ignored while Pink Noise is selected.                                              |
| `DBFS`      | float (dB)       | -90 .. 0           | -18     | Output level for both Sine and Pink Noise.                                         |
| `ON`        | bool             | off / on           | off     | OFF produces complete silence — the safe default when the plugin is just inserted. |
| `CH_L`      | bool             | off / on           | on      | Per-channel mute for the left channel.                                             |
| `CH_R`      | bool             | off / on           | on      | Per-channel mute for the right channel (no-op on a mono bus).                      |

The DSP additionally clamps `FREQUENCY` to `[20, 20000]` Hz and `DBFS` to `[-120, 0]` dB internally, regardless of UI state.

## DSP

- **Sine**: phase-accumulator generator, no lookup table — `std::sin` is fast enough. `phaseInc = freq / sampleRate` is recomputed at the top of each block from the current parameter snapshot.
- **Pink Noise**: classic Voss-McCartney 7-stage method — a counter's trailing-zero-bit count selects which of seven white-noise rows is updated per sample, plus one always-updated white-noise row to flatten the high end. The accumulated sum is rescaled so the output sits at roughly ±1 with a -3 dB/oct slope.
- The same sample is written to all output channels (the test signal is mono by design); the per-channel `CH_L` / `CH_R` mutes are applied after the generator pass.
- The input buffer is overwritten — the most natural behavior for a test-tone effect.
- The WASM port (`wasm/src/tone_generator.h`) implements the same algorithm in pure C++ (no JUCE dependency) so the browser demo is bit-equivalent to the plugin.

## AAX / PACE signing

- All credentials — `PACE_USERNAME`, `PACE_PASSWORD`, `PACE_ORGANIZATION` (the per-plugin WrapGUID issued by PACE Central Web), and `PACE_KEYPASSWORD` — are read from `.env` at the project root (or from process environment variables). **Nothing is hard-coded in the build scripts**; copy `.env` from a sibling plugin repo and replace the values for this plugin (the WrapGUID is per-plugin, the rest can typically stay the same across the series).
- A code-signing PFX certificate is also required (either `testtone-dev.pfx` at the project root, or a path pointed to by `PACE_PFX_PATH`). The password must match `PACE_KEYPASSWORD`.
- When everything above is in place, the Windows build script signs the AAX automatically. If any of the four PACE variables is missing the build still completes but emits an unsigned AAX.
- `.env` and the PFX are `.gitignore`d.

### PFX format pitfall (Windows 11)

`Export-PfxCertificate` on Windows 11 / modern PowerShell writes PKCS#12 with **PBES2 + AES-256** encryption. The PACE `wraptool` Windows code-signing path cannot extract the private key from that format and fails with:

```
wraptool Error: ... Key file ...-dev.pfx doesn't contain a valid signing certificate.
```

(A useful sanity check: with the broken PFX, .NET's `X509Certificate2Collection.Import(...)` also rejects the password with `指定されたネットワーク パスワードが間違っています` / "wrong password," even though the single-arg `new X509Certificate2(path, pw)` constructor opens it fine.)

Fix: re-pack the PFX in the legacy `PBE-SHA1-3DES` + `SHA1` MAC format with OpenSSL (the build of OpenSSL bundled with Git for Windows resolves the `legacy` provider correctly out of the box):

```powershell
$env:OPENSSL_MODULES = 'C:\Program Files\Git\mingw64\lib\ossl-modules'
$ossl = 'C:\Program Files\Git\mingw64\bin\openssl.exe'
$tmp  = "$env:TEMP\dev.pem"

# 1) PFX → PEM
& $ossl pkcs12 -in testtone-dev.pfx -nodes -passin 'pass:dev-pass-123' -out $tmp

# 2) PEM → legacy PFX (in-place overwrite)
& $ossl pkcs12 -export -in $tmp -out testtone-dev.pfx -passout 'pass:dev-pass-123' `
  -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg SHA1 -legacy

Remove-Item $tmp
```

After signing you may see `Warning! ... doesn't have a trusted root in the system.` — that is expected for a self-signed dev certificate and does not indicate a failure.

## Versioning

`VERSION` at the project root is the single source of truth. CMake reads it for `PROJECT_VERSION`, the Windows build script reads it to name the installer, and `webui/package.json`'s `version` field is kept in sync manually (or via `update_version.ps1`).

## Directory layout

```
TestTone/
├─ plugin/
│  ├─ src/
│  │  ├─ PluginProcessor.*         # APVTS + processBlock
│  │  ├─ PluginEditor.*            # WebView init, APVTS relays, fixed-size enforcement
│  │  ├─ ParameterIDs.h
│  │  ├─ KeyEventForwarder.*       # WebView → host DAW key forwarding
│  │  └─ dsp/
│  │     └─ ToneGenerator.*        # Sine + Pink Noise generator
│  └─ CMakeLists.txt
├─ webui/
│  ├─ src/
│  │  ├─ App.tsx
│  │  ├─ components/
│  │  │  ├─ PresetSliderCombo.tsx  # Horizontal slider + preset-jump buttons + numeric entry
│  │  │  ├─ HorizontalParameter.tsx / ParameterFader.tsx  # Generic faders (kept for future reuse)
│  │  │  ├─ WebDemoMenu.tsx        # Web-demo-only side drawer linking to sister plugins
│  │  │  └─ LicenseDialog.tsx, GlobalDialog.tsx
│  │  ├─ bridge/juce.ts            # Wrapper around juce-framework-frontend-mirror
│  │  ├─ bridge/web/               # Web-demo replacements (juce-shim, WebAudioEngine, etc.)
│  │  └─ hooks/useJuceParam.ts     # APVTS subscription hooks (useSyncExternalStore)
│  ├─ scripts/sync-web-demos.cjs   # Mirrors ../../web_demos.json into src/assets/
│  ├─ public-web/
│  │  ├─ wasm/testtone_dsp.wasm    # WASM build artifact (mirrored from wasm/dist)
│  │  └─ worklet/dsp-processor.js  # AudioWorklet that loads the WASM
│  ├─ vite.config.ts               # Plugin build (outputs to ../plugin/ui/public)
│  ├─ vite.config.web.ts           # Web-demo build (outputs to dist/)
│  ├─ index.html                   # Plugin entry
│  ├─ index.web.html               # Web-demo entry
│  ├─ firebase.json
│  └─ .firebaserc
├─ wasm/
│  ├─ src/
│  │  ├─ tone_generator.h          # JUCE-free pure C++ DSP (matches the plugin behavior)
│  │  └─ wasm_exports.cpp          # C ABI consumed by the AudioWorklet
│  ├─ CMakeLists.txt               # Emscripten build configuration
│  └─ build.sh                     # macOS / Linux build script
├─ cmake/
├─ scripts/
├─ JUCE/                           # submodule
├─ aax-sdk/                        # optional (AAX SDK)
├─ installer.iss
├─ build_windows.ps1
├─ build_macos.zsh
├─ update_version.ps1
├─ VERSION
├─ .env                            # PACE credentials (gitignored)
├─ testtone-dev.pfx                # Dev code-signing cert (gitignored)
└─ LICENSE
```

## License

This project is licensed under the **GNU Affero General Public License v3.0 or later** (AGPL-3.0-or-later) — see the [LICENSE](LICENSE) file for the full text.

It uses [JUCE](https://juce.com/) under the AGPLv3 option of its dual-licensing scheme. Other third-party SDKs (VST3 / AAX / WebView2 / etc.) are governed by their own licenses; the runtime dependency list is shown in the in-app *Licenses* dialog.

## Credits

Developed by **Jun Murakami**. Built on the same framework as the ZeroComp series (same author).
