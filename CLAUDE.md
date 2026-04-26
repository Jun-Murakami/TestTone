必ず日本語で回答すること。

## TestTone 開発用 ルール（AGENTS）

この文書は JUCE + WebView（Vite/React/MUI）構成で「サイン波 / ピンクノイズ生成のテストトーンプラグイン」を実装するための合意ルールです。開発時の意思決定や PR レビューの基準として用います。
ベースは ZeroComp の構成をそのまま継承しているため、ビルド設定・テーマ・コンポーネント基盤・スクリプトの構造は ZeroComp とほぼ同じです。

### 目的とスコープ

- **目的**: シンプルかつ実用的なテスト信号ジェネレータ。サイン波 / ピンクノイズの 2 系統と、On/Off ボタンによる即時ミュートを備える。
- **対象フォーマット**: VST3 / AU / AAX / Standalone
- **機能要件**:
  - Sine / Pink Noise トグル（Pink Noise 時は Frequency を無効化）
  - Frequency: 20 Hz..20 kHz（log skew、既定 1 kHz）
    プリセット: 1k, 10k, 100, 440, 60, 50, 40, 20, 20k Hz
  - dBFS: -90..0 dB（既定 -18 dB）
    プリセット: 0, -0.1, -1, -6, -12, -18, -20, -24, -40 dB
  - On / Off ボタン（OFF 時は完全に無音）

### アーキテクチャ

- **C++/JUCE**:
  - `PluginProcessor` (`TestToneAudioProcessor`) が APVTS を保持し、`processBlock` で `tt::dsp::ToneGenerator` に丸投げする
  - `tt::dsp::ToneGenerator` は Sine（位相累積）と Pink Noise（Voss-McCartney 7-stage）の 2 種を持ち、`renderBlock(buffer, start, len)` で全チャンネルに同じサンプルを書き込む
  - 入力バッファは無視して上書きする（テストツールとしての最も自然な挙動）
- **WebUI**:
  - APVTS とは `useJuceParam.ts` 経由で `useSyncExternalStore` 購読（tearing-free）
  - `PresetSliderCombo` が「横スライダー + プリセット入りエディタブルコンボ（直接入力可）」の汎用コンポーネント。Frequency / dBFS の両方で使う
  - Sine / Pink Noise 切替はラジオ風の自前トグル、On/Off は単独ボタン

### オーディオスレッド原則

- `processBlock` 内でのメモリ確保・ロック・ファイル I/O は禁止
- パラメータの読み取りは `getRawParameterValue(...)->load()` を使用し、`AudioProcessorValueTreeState::Listener` は使わない（UI スレッドからのコールバック発生を避ける）
- ピンクノイズの状態は `ToneGenerator` 内に閉じる（フレーム間で連続）

### UI/UX 原則

- ダークテーマ前提。MUI v7、`@fontsource/jost` をデフォルトフォントに使用
- 数値入力欄には `block-host-shortcuts` クラスを付与（DAW へのキーイベント転送を抑制）
- スライダーの修飾キー（Ctrl/Cmd/Shift）+ ドラッグで微調整、+ クリックで `defaultValue` リセット
- プラグインウィンドウ最小 420 × 220、初期 560 × 280

### ブリッジ / メッセージ設計

- JS → C++（コマンド系、`callNative` 経由）:
  - `system_action("ready")` — 初期化完了通知
  - `system_action("forward_key_event", payload)` — キー転送
  - `open_url(url)` — 外部 URL の起動
  - `window_action("resizeTo", w, h)` — Standalone 用リサイズ
- C++ → JS: 現状はメーター等を持たないので emit していない

### パラメータ一覧（APVTS）

- `TONE_TYPE`: choice [Sine, Pink Noise]、既定 Sine
- `FREQUENCY`: float, 20..20000 Hz, log skew, 既定 1000
- `DBFS`:      float, -90..0 dB, 既定 -18
- `ON`:        bool, 既定 OFF（プラグイン挿入時に意図せず音を出さないため）

### React 設計方針

- 外部ストア購読は `useSyncExternalStore`（`hooks/useJuceParam.ts`）。tearing-free で StrictMode 安全
- `useEffect` は最小限。JUCE の `valueChangedEvent` から呼び出すコールバックでは Latest Ref Pattern を使う

### コーディング規約（C++）

- 明示的な型、早期 return、2 段以上の深いネスト回避
- 例外は原則不使用。戻り値でエラー伝搬
- コメントは「なぜ」を中心に要点のみ
- 新規 DSP クラスは `plugin/src/dsp/` 配下、`namespace tt::dsp` で統一
- 名前空間 `tt`（TestTone）と `tt::dsp` を共通プレフィクスとして使う

### コーディング規約（Web）

- TypeScript 必須。any 型は禁止
- ESLint + Prettier。コンポーネントは疎結合・小さく
- MUI テーマはダーク優先

### ビルド

- Dev: WebView は `http://127.0.0.1:5173`（Vite dev server）
- Prod: `webui build` を zip 化 → `juce_add_binary_data` で埋め込み
- AAX SDK は `aax-sdk/` 配下に配置された場合のみ自動的に有効化
- Windows 配布ビルド: `powershell -File build_windows.ps1 -Configuration Release`
  - 成果物: `releases/<VERSION>/Windows/...` と `TestTone_<VERSION>_Windows_Setup.exe`（Inno Setup 6 必須）
  - AAX 署名は `.env` に PACE 情報（`PACE_USERNAME` / `PACE_PASSWORD` / `PACE_ORGANIZATION` / `PACE_KEYPASSWORD`）が揃っている場合のみ自動実行
  - WrapGUID は `PACE_ORGANIZATION` env (= `.env`) に指定する（**ビルドスクリプトには埋め込まない**。プラグインごとに異なるため）。新規プラグイン立ち上げ時は姉妹リポジトリの `.env` をコピーして GUID だけ差し替える
  - **PFX は必ず旧形式 (PBE-SHA1-3DES + SHA1 MAC) に詰め直す**。Windows 11 の `Export-PfxCertificate` は PBES2/AES-256 で書き出すが、PACE wraptool の内部 Windows コード署名 API はその新形式から鍵を取り出せず `Key file ... doesn't contain a valid signing certificate` で落ちる。PowerShell の `X509Certificate2Collection.Import(...)` も同じパスワードで `指定されたネットワーク パスワードが間違っています` を返すが `new X509Certificate2(...)` 単発コンストラクタは開ける、という症状が出たらこれ。`Export-PfxCertificate` 直後に下記を 1 度だけ流して in-place で旧形式に置き換える:
    ```powershell
    $env:OPENSSL_MODULES = 'C:\Program Files\Git\mingw64\lib\ossl-modules'
    $ossl = 'C:\Program Files\Git\mingw64\bin\openssl.exe'
    $tmp = "$env:TEMP\dev.pem"
    & $ossl pkcs12 -in testtone-dev.pfx -nodes -passin 'pass:dev-pass-123' -out $tmp
    & $ossl pkcs12 -export -in $tmp -out testtone-dev.pfx -passout 'pass:dev-pass-123' `
      -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg SHA1 -legacy
    Remove-Item $tmp
    ```
    `C:\Program Files\OpenSSL-Win64\bin\openssl.exe` を使う場合は `OPENSSL_MODULES` の解決先が壊れているので Git for Windows 同梱の openssl を使うのが安全。署名後に出る `Warning! ... doesn't have a trusted root in the system.` は自己署名 dev cert ゆえの想定挙動なので無視で OK

### Web デモ（WASM）

`wasm/src/tone_generator.h` がプラグイン版と同等の挙動を持つ純 C++ DSP。`wasm_exports.cpp` で JS 側に C ABI を露出する。

エクスポート関数（C ABI）:
- `dsp_init(sampleRate)` / `dsp_destroy()` / `dsp_reset()`
- `dsp_alloc_buffer(n)` / `dsp_free_buffer(p)`
- `dsp_set_type(int)` / `dsp_set_frequency_hz(float)` / `dsp_set_level_dbfs(float)`
- `dsp_set_on(int)` / `dsp_set_channel_enabled(int ch, int enabled)`
- `dsp_process_block(float* outL, float* outR, int n)`

Web ブリッジ層（`webui/src/bridge/web/`）:
- `WebParamState.ts` — `WebSliderState` / `WebToggleState` / `WebComboBoxState`
  - juce-framework-frontend-mirror と同じ `valueChangedEvent.addListener` インターフェース
- `juce-shim.ts` — TestTone のパラメータ（TONE_TYPE / FREQUENCY / DBFS / ON / CH_L / CH_R）を登録、
  値変化を `webAudioEngine` 経由で WASM に流す
- `WebAudioEngine.ts` — AudioContext 起動、worklet 接続、WASM ロード、パラメータ転送
  - 起動前のパラメータは pendingParams に貯めておき、worklet 起動後に flush
- `WebBridgeManager.ts` — `juceBridge` のドロップイン置換（`ensureStarted()` を追加）
- `web-juce.ts` — `bridge/juce.ts` の置換用エントリ

AudioWorklet（`webui/public-web/worklet/dsp-processor.js`）:
- `init-wasm` メッセージで `WebAssembly.instantiate` → `dsp_init(sampleRate)`
- `set-param` メッセージで `dsp_set_*` を呼ぶ
- `process()` で 128 frame ごとに `dsp_process_block()` → outL/outR にコピー

Vite 構成:
- `vite.config.ts` — プラグインビルド（`outDir: ../plugin/ui/public`）
- `vite.config.web.ts` — Web デモビルド（`outDir: dist`、`juce-framework-frontend-mirror` を `juce-shim.ts` に alias、`public-web/` を配信、`VITE_RUNTIME=web` を埋め込む）

App.tsx の Web/プラグイン切替:
- `IS_WEB_MODE = import.meta.env.VITE_RUNTIME === 'web'` で判定
- Web 時は 450×265 のカード状レイアウトで中央寄せ + 起動オーバーレイ（ユーザジェスチャ前）を出す
- プラグイン時はホストウィンドウ全面（450×265 固定）

WASM ビルド（Windows）:
```powershell
& 'D:\Synching\code\JUCE\emsdk\emsdk_env.ps1'
cd D:\Synching\code\JUCE\TestTone\wasm
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
New-Item -ItemType Directory build | Out-Null
cd build
emcmake cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
Copy-Item -Force dist\testtone_dsp.wasm ..\dist\
Copy-Item -Force dist\testtone_dsp.wasm ..\..\webui\public-web\wasm\
```

emsdk_env.ps1 が遅い場合は env 変数を直接設定して `emcmake.bat` を絶対パスで呼ぶ:
```powershell
$env:EMSDK_PYTHON = "D:\Synching\code\JUCE\emsdk\python\3.13.3_64bit\python.exe"
$env:EM_CONFIG = "D:\Synching\code\JUCE\emsdk\.emscripten"
$env:EMSDK = "D:\Synching\code\JUCE\emsdk"
$env:PATH = "D:\Synching\code\JUCE\emsdk\upstream\emscripten;$env:PATH"
emcmake cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

**DSP に変更を入れたら必ず WASM (`wasm/src/tone_generator.h`) も同じ変更を反映して再ビルドし、`webui/public-web/wasm/` を更新する。** WASM を更新せず `npm run build:web` すると Web デモだけ旧ロジックのままになる。

Firebase Hosting:
- `webui/.firebaserc` のプロジェクト ID は `testtone-demo`（仮置き、実プロジェクトを Firebase Console で作成して合わせる）
- `webui/firebase.json` で `dist/` を public、`*.wasm` に `Content-Type: application/wasm` ヘッダを付与
- `npm run deploy:web` で `build:web` → `firebase deploy --only hosting`

### バージョン管理

- `VERSION` ファイルで一元管理。CMake と `build_windows.ps1` がここから読む
- `webui/package.json` の `version` も手動で同期する
- コミットは**ユーザが明示的に指示しない限り行わない**

### デフォルト挙動メモ

- 新規インスタンス時は `ON=false`、Sine / 1 kHz / -18 dBFS で立ち上がる（OFF なので無音）
- ON にすると即座にテスト信号が出る。Pink Noise を選ぶと Frequency UI は無効化される
- DSP 側では Frequency は `[20, 20000]` Hz、dBFS は `[-120, 0]` dB にクランプして安全側に倒している
