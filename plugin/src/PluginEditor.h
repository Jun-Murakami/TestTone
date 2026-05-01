// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include <atomic>
#include <memory>
#include <optional>

class TestToneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    // プラグインウィンドウは固定サイズ（リサイズ不可）。
    static constexpr int kFixedWidth  = 450;
    static constexpr int kFixedHeight = 265;

    explicit TestToneAudioProcessorEditor(TestToneAudioProcessor&);
    ~TestToneAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    void timerCallback() override;

    using Resource = juce::WebBrowserComponent::Resource;
    std::optional<Resource> getResource(const juce::String& url) const;

    void handleSystemAction(const juce::Array<juce::var>& args,
                            juce::WebBrowserComponent::NativeFunctionCompletion completion);

    TestToneAudioProcessor& audioProcessor;

    // WebBrowserComponent より先に宣言（attachment から参照されるため）
    juce::WebComboBoxRelay     webToneTypeRelay;
    juce::WebSliderRelay       webFrequencyRelay;
    juce::WebSliderRelay       webDbfsRelay;
    juce::WebToggleButtonRelay webOnRelay;
    juce::WebToggleButtonRelay webChLRelay;
    juce::WebToggleButtonRelay webChRRelay;

    juce::WebComboBoxParameterAttachment     toneTypeAttachment;
    juce::WebSliderParameterAttachment       frequencyAttachment;
    juce::WebSliderParameterAttachment       dbfsAttachment;
    juce::WebToggleButtonParameterAttachment onAttachment;
    juce::WebToggleButtonParameterAttachment chLAttachment;
    juce::WebToggleButtonParameterAttachment chRAttachment;

    juce::WebControlParameterIndexReceiver controlParameterIndexReceiver;

    struct WebViewLifetimeGuard : public juce::WebViewLifetimeListener
    {
        std::atomic<bool> constructed{ false };
        void webViewConstructed(juce::WebBrowserComponent*) override { constructed.store(true, std::memory_order_release); }
        void webViewDestructed(juce::WebBrowserComponent*) override  { constructed.store(false, std::memory_order_release); }
        bool isConstructed() const { return constructed.load(std::memory_order_acquire); }
    } webViewLifetimeGuard;

    juce::WebBrowserComponent webView;

    bool useLocalDevServer = false;

    std::atomic<bool> isShuttingDown{ false };

    // ホストから渡されるバスレイアウト（mono / stereo）を WebView に通知するための監視。
    //  -1 で初期化しておき、最初の有効値で必ず一度 emit する（"ready" 時に状態が確定済みでも漏らさない）。
    int lastEmittedNumOutputChannels { -1 };
    void pollAndEmitChannelLayout();

#if defined(JUCE_WINDOWS)
    double lastHwndScaleFactor { 0.0 };
    int    lastHwndDpi         { 0 };
    void   pollAndMaybeNotifyDpiChange();
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestToneAudioProcessorEditor)
};
