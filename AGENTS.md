# TestTone — AGENTS

このリポジトリで作業する AI コーディングエージェント向けの要点サマリです。詳細は [CLAUDE.md](./CLAUDE.md) を参照してください。

## プロジェクト

- JUCE + WebView（Vite/React/MUI）のシンプルなテスト信号ジェネレータ
- ベースは ZeroComp（同シリーズ）の構成をそのまま継承（ビルド設定 / テーマ / ライブラリ）
- ユーザが触るのは Sine / Pink Noise トグル、Frequency、dBFS、On/Off の 4 つだけ

## ディレクトリ概観

- `plugin/src/` — JUCE プラグイン本体（C++）
  - `PluginProcessor.*`, `PluginEditor.*`, `ParameterIDs.h`
  - `dsp/ToneGenerator.*` — Sine（位相累積）+ Pink Noise（Voss-McCartney 7-stage）
  - `KeyEventForwarder.*` — WebView → DAW ホストのキーイベント転送
- `webui/src/` — Vite + React 19 + MUI v7 フロントエンド
  - `App.tsx` — Sine/Pink トグル + Frequency / dBFS スライダー + On/Off
  - `components/PresetSliderCombo.tsx` — 横スライダー + プリセット入りエディタブルコンボ（直接入力可）
  - `components/HorizontalParameter.tsx` / `ParameterFader.tsx` — 汎用フェーダー（現状 App.tsx からは未使用、再利用用に温存）
  - `components/LicenseDialog.tsx`, `components/GlobalDialog.tsx` — ダイアログ基盤
  - `hooks/useJuceParam.ts` — APVTS 購読の React フック（useSyncExternalStore ベース）

## 作業するうえでの原則

1. オーディオスレッド上では確保 / ロック / I/O を行わない
2. パラメータは APVTS に集約、UI との双方向同期は `WebSliderRelay` / `WebToggleButtonRelay` / `WebComboBoxRelay` + `Web*ParameterAttachment` で行う
3. TypeScript は `any` 禁止。購読系は `useSyncExternalStore`、`useEffect` は最小限
4. 新規 DSP クラスは `plugin/src/dsp/` に置き、`namespace tt::dsp` に統一、`CMakeLists.txt` の `target_sources` に登録
5. WebUI コンポーネントは疎結合・小さく

## ビルド

- Windows（本番配布）: `powershell -File build_windows.ps1 -Configuration Release` — WebUI ビルド → VST3 / AAX / Standalone コンパイル → AAX 署名（`.env` に PACE 情報あれば）→ ZIP 梱包 → Inno Setup インストーラ生成
- Windows（開発）: `cmake -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Debug --target TestTone_VST3`
- macOS: `./build_macos.zsh`
- Linux（WSL2 Ubuntu 24.04 で動作確認）: `bash build_linux.sh` — VST3 / LV2 / CLAP / Standalone をビルドし `releases/<VERSION>/TestTone_<VERSION>_Linux_VST3_LV2_CLAP_Standalone.zip` を生成。LV2 / CLAP は `if(UNIX AND NOT APPLE)` で条件付き有効化されるので Win/Mac リリース経路には影響しない
- WebUI dev: `cd webui && npm install && npm run dev`
- 型チェックのみ: `cd webui && npx tsc --noEmit`

## よく使う拡張ポイント

- 新規 APVTS パラメータ追加: `ParameterIDs.h` に ID 追加 → `createParameterLayout()` で登録 → `PluginEditor` に Web*Relay / Attachment 追加 → `.withOptionsFrom()` に追加 → WebUI 側 hook で購読
- 別の波形タイプを足す: `dsp/ToneGenerator.h` の `Type` enum を拡張し、`renderBlock` の分岐に追加
- 新規 DAW 互換対応: `KeyEventForwarder.cpp` の `resolveHostWindowForForwarding` / `handleDefaultPostMessage`

## AAX / PACE

- 認証情報・WrapGUID は **すべて `.env`** に書く（`PACE_USERNAME` / `PACE_PASSWORD` / `PACE_ORGANIZATION` / `PACE_KEYPASSWORD`）。ビルドスクリプトにハードコードしない
- `.env` は `.gitignore` 対象。新規プラグイン立ち上げ時は姉妹リポジトリ（ZeroComp 等）の `.env` をコピーして `PACE_ORGANIZATION` を当該プラグインの WrapGUID に差し替える
- **PFX は旧形式 (PBE-SHA1-3DES + SHA1 MAC) に変換しておく**。Win11 の `Export-PfxCertificate` は PBES2/AES-256 で書き出すが wraptool が読めず `Key file ... doesn't contain a valid signing certificate` で署名失敗する。OpenSSL (Git for Windows 同梱) で in-place 詰め直し: `pkcs12 -in <pfx> -nodes -passin pass:<pw> -out tmp.pem` → `pkcs12 -export -in tmp.pem -out <pfx> -passout pass:<pw> -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg SHA1 -legacy`。詳細は `CLAUDE.md` 参照
