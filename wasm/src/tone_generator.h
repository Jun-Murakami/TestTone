// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
// TestTone DSP — Emscripten / WebAssembly 用の純 C++ 実装。
//  プラグイン側 (plugin/src/dsp/ToneGenerator.{h,cpp}) と等価な振る舞いを保つ。
//  JUCE 依存なし、std のみで完結。
#pragma once

#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace tt_wasm {

class ToneGenerator {
public:
    enum class Type : int { Sine = 0, PinkNoise = 1 };

    void prepare(double sampleRateHz)
    {
        sampleRate = (sampleRateHz > 0.0) ? sampleRateHz : 48000.0;
        reset();
    }

    void reset()
    {
        phase = 0.0;
        phaseInc = 0.0;
        pinkRows.fill(0.0f);
        pinkCounter = 0;
        pinkRunningSum = 0.0f;
        rngState = 0x9E3779B9u;
    }

    void setType(int t)               { type = (t == 1) ? Type::PinkNoise : Type::Sine; }
    void setFrequencyHz(float hz)     { frequencyHz = hz; }
    void setLevelDbfs(float db)       { levelDb = db; }
    void setOn(bool isOn)             { on = isOn; }
    void setChannelEnabled(int ch, bool enabled)
    {
        if (ch == 0) chL = enabled;
        else if (ch == 1) chR = enabled;
    }

    // outL, outR の 2 チャンネルへサンプル書き込み（L/R 同一信号）。
    //  ON=false や両 ch ミュート時は 0 で埋める。
    //  R が無効でも L が有効なら R は 0 のまま（プラグインの processBlock と挙動を揃える）。
    void processBlock(float* outL, float* outR, int numSamples)
    {
        if (numSamples <= 0) return;

        if (! on)
        {
            std::fill(outL, outL + numSamples, 0.0f);
            std::fill(outR, outR + numSamples, 0.0f);
            return;
        }

        const float clampedHz = std::clamp(frequencyHz, 20.0f, 20000.0f);
        phaseInc = static_cast<double>(clampedHz) / sampleRate;

        const float gainLin = std::pow(10.0f, std::clamp(levelDb, -120.0f, 0.0f) / 20.0f);

        if (type == Type::Sine)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float s = nextSineSample() * gainLin;
                outL[i] = chL ? s : 0.0f;
                outR[i] = chR ? s : 0.0f;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float s = nextPinkSample() * gainLin;
                outL[i] = chL ? s : 0.0f;
                outR[i] = chR ? s : 0.0f;
            }
        }
    }

private:
    static int trailingZeroBits(std::uint32_t x)
    {
       #if defined(__GNUC__) || defined(__clang__)
        return x == 0 ? 0 : __builtin_ctz(x);
       #else
        int n = 0;
        while ((x & 1u) == 0u && n < 31) { x >>= 1; ++n; }
        return n;
       #endif
    }

    float nextWhiteSample()
    {
        std::uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState = x;
        return static_cast<float>(static_cast<std::int32_t>(x)) * (1.0f / 2147483648.0f);
    }

    float nextSineSample()
    {
        const float s = std::sin(static_cast<float>(phase * 6.283185307179586));
        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;
        return s;
    }

    float nextPinkSample()
    {
        ++pinkCounter;
        const int k = trailingZeroBits(pinkCounter);
        if (k < kPinkStages)
        {
            const float prev = pinkRows[static_cast<size_t>(k)];
            const float next = nextWhiteSample();
            pinkRunningSum += (next - prev);
            pinkRows[static_cast<size_t>(k)] = next;
        }
        const float extra = nextWhiteSample();
        constexpr float kPinkScale = 1.0f / static_cast<float>(kPinkStages + 1);
        return (pinkRunningSum + extra) * kPinkScale;
    }

    double sampleRate = 48000.0;

    Type  type        = Type::Sine;
    float frequencyHz = 1000.0f;
    float levelDb     = -18.0f;
    bool  on          = false;
    bool  chL         = true;
    bool  chR         = true;

    double phase    = 0.0;
    double phaseInc = 0.0;

    static constexpr int kPinkStages = 7;
    std::array<float, kPinkStages> pinkRows{};
    std::uint32_t pinkCounter = 0;
    float pinkRunningSum = 0.0f;

    std::uint32_t rngState = 0x9E3779B9u;
};

} // namespace tt_wasm
