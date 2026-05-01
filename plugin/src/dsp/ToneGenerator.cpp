// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
#include "ToneGenerator.h"

#include <cmath>
#include <algorithm>

namespace tt::dsp {

namespace {
    // Voss-McCartney pink noise: counter の最下位ビットの位置に応じて
    //  該当 row だけを更新し、各 row の和を出力する。row が小さいほど低い更新頻度
    //  → 周波数特性が約 -3 dB/oct（ピンク）になる。kPinkStages を増やすとレンジが広がる。
    inline int trailingZeroBits(std::uint32_t x) noexcept
    {
       #if defined(__GNUC__) || defined(__clang__)
        return x == 0 ? 0 : __builtin_ctz(x);
       #elif defined(_MSC_VER)
        unsigned long idx;
        return _BitScanForward(&idx, x) ? static_cast<int>(idx) : 0;
       #else
        int n = 0;
        while ((x & 1u) == 0u && n < 31) { x >>= 1; ++n; }
        return n;
       #endif
    }
}

void ToneGenerator::prepare(double sampleRateHz) noexcept
{
    sampleRate = (sampleRateHz > 0.0) ? sampleRateHz : 48000.0;
    reset();
}

void ToneGenerator::reset() noexcept
{
    phase = 0.0;
    phaseInc = 0.0;
    pinkRows.fill(0.0f);
    pinkCounter = 0;
    pinkRunningSum = 0.0f;
    rngState = 0x9E3779B9u;
}

float ToneGenerator::nextWhiteSample() noexcept
{
    // xorshift32: ローコスト、十分にホワイトに見える
    std::uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    // [-1, 1) に正規化
    return static_cast<float>(static_cast<std::int32_t>(x)) * (1.0f / 2147483648.0f);
}

float ToneGenerator::nextSineSample() noexcept
{
    // 位相 [0, 1) → [0, 2π)
    const float s = std::sin(static_cast<float>(phase * 6.283185307179586));
    phase += phaseInc;
    if (phase >= 1.0) phase -= 1.0;
    return s;
}

float ToneGenerator::nextPinkSample() noexcept
{
    // 各 row はそれぞれ独自のホワイトノイズを保持している。
    //  counter の trailing zero bits が k なら、row[k] を更新（kPinkStages 未満の時のみ）。
    ++pinkCounter;
    const int k = trailingZeroBits(pinkCounter);
    if (k < kPinkStages)
    {
        const float prev = pinkRows[static_cast<size_t>(k)];
        const float next = nextWhiteSample();
        pinkRunningSum += (next - prev);
        pinkRows[static_cast<size_t>(k)] = next;
    }
    // kPinkStages 個の row + 1 個の独立ホワイトを足し合わせるとよりフラットになる
    const float extra = nextWhiteSample();
    // ピーク -1..+1 程度に収まるよう正規化（kPinkStages=7 + 1 の rms 換算で割る）
    constexpr float kPinkScale = 1.0f / static_cast<float>(kPinkStages + 1);
    return (pinkRunningSum + extra) * kPinkScale;
}

void ToneGenerator::renderBlock(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    const int numChannels = buffer.getNumChannels();
    if (numChannels <= 0 || numSamples <= 0) return;

    if (! on)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.clear(ch, startSample, numSamples);
        return;
    }

    // 周波数 → 1 サンプル当たりの位相増分。20 Hz..20 kHz をクランプ。
    const float clampedHz = std::clamp(frequencyHz, 20.0f, 20000.0f);
    phaseInc = static_cast<double>(clampedHz) / sampleRate;

    // dBFS → 線形ゲイン
    const float gainLin = std::pow(10.0f, std::clamp(levelDb, -120.0f, 0.0f) / 20.0f);

    // 各サンプルを生成して全チャンネルに同じ値を書き込む（モノラル基準のテスト信号）。
    auto writeSample = [&](int sampleIdx, float value)
    {
        const float scaled = value * gainLin;
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer(ch)[startSample + sampleIdx] = scaled;
    };

    if (type == Type::Sine)
    {
        for (int i = 0; i < numSamples; ++i)
            writeSample(i, nextSineSample());
    }
    else // PinkNoise
    {
        for (int i = 0; i < numSamples; ++i)
            writeSample(i, nextPinkSample());
    }
}

} // namespace tt::dsp
