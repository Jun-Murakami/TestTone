#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tt::id {
    // TestTone — シンプルなテスト信号ジェネレータ
    //  - TONE_TYPE: choice [Sine, Pink Noise]、既定 Sine
    //  - FREQUENCY: float, 20..20000 Hz, log skew, 既定 1000 Hz（Pink Noise 時は無効）
    //  - DBFS:      float, -90..0 dB, 既定 -18 dB
    //  - ON:        bool, 既定 OFF（出力ミュート＝完全な無音）
    //  - CH_L/CH_R: bool, 既定 ON（チャンネル個別ミュート。両方 ON で通常出力）
    const juce::ParameterID TONE_TYPE { "TONE_TYPE", 1 };
    const juce::ParameterID FREQUENCY { "FREQUENCY", 1 };
    const juce::ParameterID DBFS      { "DBFS",      1 };
    const juce::ParameterID ON        { "ON",        1 };
    const juce::ParameterID CH_L      { "CH_L",      1 };
    const juce::ParameterID CH_R      { "CH_R",      1 };
}  // namespace tt::id
