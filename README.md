# TestTone

シリーズ姉妹プラグイン（ZeroComp / ZeroLimit / ZeroEQ）と同じ JUCE + WebView 構成で作った、極めてシンプルなテスト信号ジェネレータ。Sine / Pink Noise を生成し、周波数とレベルを横スライダー + プリセット入りコンボボックスで指定して On/Off で出力を切り替える。

VST3 / AU / AAX / Standalone をサポート。

## Features

- **Sine** または **Pink Noise** をワンクリックで切替（Pink 時は Frequency 無効）
- **Frequency**: 20 Hz..20 kHz（log skew、既定 1 kHz）
  プリセット: 1k, 10k, 100, 440, 60, 50, 40, 20, 20k Hz（直接入力可）
- **Level (dBFS)**: -90..0 dB（既定 -18 dB）
  プリセット: 0, -0.1, -1, -6, -12, -18, -20, -24, -40 dB（直接入力可）
- **On / Off** ボタンで完全ミュート（OFF=出力 0）

UI は MUI v7 ダークテーマ、フォントは Jost / Red Hat Mono。

## Requirements

- CMake 3.22+
- C++17 toolchain
  - Windows: Visual Studio 2022（C++ デスクトップ ワークロード）
  - macOS: Xcode 14+
- Node.js 18+ と npm（WebUI ビルド用）
- JUCE（git submodule として同梱）
- 任意: AAX SDK（Pro Tools 用、`aax-sdk/` に配置）
- 任意: Inno Setup 6（Windows インストーラ生成）

## Getting started

```bash
# 1. submodule 込みで取得
git submodule update --init --recursive

# 2. WebUI 依存解決
cd webui && npm install && cd ..

# 3. ビルド
# Windows
powershell -ExecutionPolicy Bypass -File build_windows.ps1 -Configuration Release
# macOS
./build_macos.zsh
```

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

# Terminal B: Standalone を Debug でビルドして起動
cmake --build build --config Debug --target TestTone_Standalone
```

Debug ビルドは `http://127.0.0.1:5173` から WebUI を読みに行く。Release ビルドは `juce_add_binary_data` で zip 埋め込み。

## Parameters (APVTS)

| ID          | Type             | Range                | Default | Notes                                            |
| ----------- | ---------------- | -------------------- | ------- | ------------------------------------------------ |
| `TONE_TYPE` | choice           | Sine / Pink Noise    | Sine    | Pink Noise 時は `FREQUENCY` UI を無効化           |
| `FREQUENCY` | float (Hz, log)  | 20 .. 20000          | 1000    | Pink Noise では無視                              |
| `DBFS`      | float (dB)       | -90 .. 0             | -18     | Sine / Pink ともに ±-90 dB の範囲で振幅を変える   |
| `ON`        | bool             | off / on             | off     | OFF は完全無音（プラグイン挿入時の安全側既定）   |

## DSP

- **Sine**: 位相累積で生成（lookup 無し、`std::sin` 直）。`phaseInc = freq / sampleRate` をブロック先頭で更新
- **Pink Noise**: Voss-McCartney 7-stage 法（`counter` の trailing zero bits で更新する row を選ぶ）+ 1 個の独立ホワイトを足してフラットに寄せる
- ステレオ以上のチャンネルには同じサンプルを書き込む（モノラル基準のテスト信号）
- 入力バッファは捨てて上書き（テスト信号としては最も自然な挙動）

## AAX / PACE 署名

- WrapGUID: `8BB21750-40C7-11F1-B00E-005056928F3B`（TestTone 固有、build スクリプトに埋め込み済み）
- `.env` に `PACE_USERNAME` / `PACE_PASSWORD` / `PACE_KEYPASSWORD` と PFX 証明書（`testtone-dev.pfx` をプロジェクトルート、または `PACE_PFX_PATH` で指定）が揃っていれば、Windows ビルド時に自動署名する
- 別 GUID で署名したい場合は `PACE_ORGANIZATION` env 変数で上書きできる

## Directory layout

```
TestTone/
├─ plugin/
│  ├─ src/
│  │  ├─ PluginProcessor.*    # APVTS と processBlock
│  │  ├─ PluginEditor.*       # WebView 初期化と APVTS リレー
│  │  ├─ ParameterIDs.h
│  │  ├─ KeyEventForwarder.*  # WebView → DAW ホスト キー転送
│  │  └─ dsp/
│  │     └─ ToneGenerator.*   # Sine + Pink Noise
│  └─ CMakeLists.txt
├─ webui/
│  ├─ src/
│  │  ├─ App.tsx
│  │  ├─ components/
│  │  │  ├─ PresetSliderCombo.tsx   # 横スライダー + プリセット入りコンボ
│  │  │  ├─ HorizontalParameter.tsx / ParameterFader.tsx  # 汎用フェーダー（再利用用）
│  │  │  └─ LicenseDialog.tsx, GlobalDialog.tsx
│  │  ├─ bridge/juce.ts             # juce-framework-frontend-mirror ラッパー
│  │  └─ hooks/useJuceParam.ts      # APVTS 購読フック
│  ├─ vite.config.ts
│  └─ package.json
├─ cmake/
├─ scripts/
├─ JUCE/                  # submodule
├─ aax-sdk/               # 任意（AAX SDK）
├─ installer.iss
├─ build_windows.ps1
├─ build_macos.zsh
├─ VERSION
└─ LICENSE
```

> Note: Web デモ（WebAssembly + Firebase Hosting）は今回のスタート時点では含まれていない。後で追加する場合は ZeroComp の `wasm/`, `webui/vite.config.web.ts`, `webui/index.web.html`, `webui/.firebaserc`, `webui/firebase.json`, `webui/scripts/sync-web-demos.cjs`, `webui/public-web/` を参考に。

## License

Plugin source: see `LICENSE`. Third-party SDKs（JUCE / VST3 / AAX / WebView2 等）は別ライセンス。ランタイム依存は内蔵の Licenses ダイアログで確認できる。

## Credits

Developed by **Jun Murakami**. Based on the ZeroComp framework (same author).
