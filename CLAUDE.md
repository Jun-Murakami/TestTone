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
  - AAX 署名は `.env` に PACE 情報がある場合のみ自動実行
  - WrapGUID は `8BB21750-40C7-11F1-B00E-005056928F3B`（TestTone 専用、build スクリプトに埋め込み済み）
  - 環境変数 `PACE_ORGANIZATION` を立てれば一時的に別 GUID で署名可能

### Web デモ

ZeroComp と異なり、TestTone のリポジトリには現状 Web デモ（WASM / Vite Web 設定 / Firebase）を含めていない。
将来 Web デモを作る場合は ZeroComp の `wasm/`, `webui/vite.config.web.ts`, `webui/index.web.html`, `webui/.firebaserc`, `webui/firebase.json`, `webui/scripts/sync-web-demos.cjs`, `webui/public-web/` を参考にする。

### バージョン管理

- `VERSION` ファイルで一元管理。CMake と `build_windows.ps1` がここから読む
- `webui/package.json` の `version` も手動で同期する
- コミットは**ユーザが明示的に指示しない限り行わない**

### デフォルト挙動メモ

- 新規インスタンス時は `ON=false`、Sine / 1 kHz / -18 dBFS で立ち上がる（OFF なので無音）
- ON にすると即座にテスト信号が出る。Pink Noise を選ぶと Frequency UI は無効化される
- DSP 側では Frequency は `[20, 20000]` Hz、dBFS は `[-120, 0]` dB にクランプして安全側に倒している
