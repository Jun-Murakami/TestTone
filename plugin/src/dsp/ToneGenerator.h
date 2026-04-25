#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cstdint>

namespace tt::dsp {

// シンプルなテストトーン生成器。
//  - Sine: 位相累積の純音 (lookup 無し、std::sin で十分軽い)
//  - Pink Noise: Voss-McCartney 7-stage 法（白色 → ピンク化、軽量で十分自然）
//  - dBFS から線形ゲインへの変換 + ON/OFF ミュート（OFF=完全無音）
//  - 出力サンプルレート変更時の状態リセットは prepare() で。
class ToneGenerator {
public:
    enum class Type : int { Sine = 0, PinkNoise = 1 };

    void prepare(double sampleRateHz) noexcept;
    void reset() noexcept;

    void setType(Type t) noexcept            { type = t; }
    void setFrequencyHz(float hz) noexcept   { frequencyHz = hz; }
    void setLevelDbfs(float db) noexcept     { levelDb = db; }
    void setOn(bool isOn) noexcept           { on = isOn; }

    // L/R が独立な numChannels チャンネルへ同一サンプルを書き込む。
    //  ピンクノイズも単一ストリームで生成して全チャンネルに同じものを流す（モノラル基準）。
    //  ON=false の時は単純に buffer.clear() 相当（完全無音）。
    void renderBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

private:
    float nextSineSample() noexcept;
    float nextPinkSample() noexcept;
    float nextWhiteSample() noexcept;

    double sampleRate = 48000.0;

    // Sine 状態
    Type  type        = Type::Sine;
    float frequencyHz = 1000.0f;
    float levelDb     = -18.0f;
    bool  on          = false;

    double phase     = 0.0;   // [0, 1)
    double phaseInc  = 0.0;   // freq / sampleRate（renderBlock の冒頭で再計算）

    // Voss-McCartney pink noise 状態
    static constexpr int kPinkStages = 7;
    std::array<float, kPinkStages> pinkRows{};
    std::uint32_t pinkCounter = 0;
    float pinkRunningSum = 0.0f;

    // 簡易 LCG（白色ノイズ用）— 軽量で再現性あり、processBlock 内で alloc/lock 無し。
    std::uint32_t rngState = 0x9E3779B9u;
};

} // namespace tt::dsp
