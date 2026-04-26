/**
 * juce-framework-frontend-mirror の Web 互換 shim（TestTone 版）。
 * Vite エイリアスで本家モジュールの代わりにこれが解決される。
 *
 * TestTone の APVTS パラメータ:
 *   TONE_TYPE (combo: Sine / Pink Noise)
 *   FREQUENCY (slider: 20..20000 Hz, log skew → ただし shim は線形で OK *1)
 *   DBFS      (slider: -90..0 dB, linear)
 *   ON        (toggle)
 *   CH_L/CH_R (toggle)
 *
 * *1: フェーダー側 (PresetSliderCombo) は `setScaled` を「scaled 値 → 線形正規化 →
 *     setNormalisedValue」で呼び、shim の `toScaled` も線形なので往復で値が保たれる。
 *     UI 側で `valueToNorm/normToValue` で対数表示位置に変換するため、shim 内で
 *     対数マッピングを持たなくても挙動は揃う（plugin 側と同じ仕掛け）。
 */

import {
  WebSliderState,
  WebToggleState,
  WebComboBoxState,
} from './WebParamState';
import { webAudioEngine } from './WebAudioEngine';

const sliderStates   = new Map<string, WebSliderState>();
const toggleStates   = new Map<string, WebToggleState>();
const comboBoxStates = new Map<string, WebComboBoxState>();

function makeLinearSlider(defaultScaled: number, min: number, max: number): WebSliderState
{
  return new WebSliderState({
    defaultScaled,
    min,
    max,
    toScaled:   (n: number) => min + n * (max - min),
    fromScaled: (v: number) => (v - min) / (max - min),
  });
}

function registerDefaults(): void
{
  // --- Slider 系 ---
  sliderStates.set('FREQUENCY', makeLinearSlider(1000, 20, 20000));
  sliderStates.set('DBFS',      makeLinearSlider(-18, -90, 0));

  // --- Toggle ---
  toggleStates.set('ON',   new WebToggleState(false));
  toggleStates.set('CH_L', new WebToggleState(true));
  toggleStates.set('CH_R', new WebToggleState(true));

  // --- Choice ---
  comboBoxStates.set('TONE_TYPE', new WebComboBoxState(0, 2)); // Sine / Pink Noise

  // --- 値変化 → WASM エンジンへ直送 ---
  sliderStates.get('FREQUENCY')!.valueChangedEvent.addListener(() => {
    webAudioEngine.setFrequencyHz(sliderStates.get('FREQUENCY')!.getScaledValue());
  });
  sliderStates.get('DBFS')!.valueChangedEvent.addListener(() => {
    webAudioEngine.setLevelDbfs(sliderStates.get('DBFS')!.getScaledValue());
  });
  toggleStates.get('ON')!.valueChangedEvent.addListener(() => {
    webAudioEngine.setOn(toggleStates.get('ON')!.getValue());
  });
  toggleStates.get('CH_L')!.valueChangedEvent.addListener(() => {
    webAudioEngine.setChannelEnabled(0, toggleStates.get('CH_L')!.getValue());
  });
  toggleStates.get('CH_R')!.valueChangedEvent.addListener(() => {
    webAudioEngine.setChannelEnabled(1, toggleStates.get('CH_R')!.getValue());
  });
  comboBoxStates.get('TONE_TYPE')!.valueChangedEvent.addListener(() => {
    webAudioEngine.setToneType(comboBoxStates.get('TONE_TYPE')!.getChoiceIndex());
  });

  // 初期値を WASM に反映（worklet 起動前は WebAudioEngine 内部で pending にバッファされる）
  webAudioEngine.setFrequencyHz(sliderStates.get('FREQUENCY')!.getScaledValue());
  webAudioEngine.setLevelDbfs(sliderStates.get('DBFS')!.getScaledValue());
  webAudioEngine.setOn(toggleStates.get('ON')!.getValue());
  webAudioEngine.setChannelEnabled(0, toggleStates.get('CH_L')!.getValue());
  webAudioEngine.setChannelEnabled(1, toggleStates.get('CH_R')!.getValue());
  webAudioEngine.setToneType(comboBoxStates.get('TONE_TYPE')!.getChoiceIndex());
}

registerDefaults();

// ---------- juce-framework-frontend-mirror 互換 API ----------

export function getSliderState(id: string): WebSliderState | null
{
  return sliderStates.get(id) ?? null;
}

export function getToggleState(id: string): WebToggleState | null
{
  return toggleStates.get(id) ?? null;
}

export function getComboBoxState(id: string): WebComboBoxState | null
{
  return comboBoxStates.get(id) ?? null;
}

export function getNativeFunction(
  _name: string,
): ((...args: unknown[]) => Promise<unknown>) | null
{
  return null;
}
