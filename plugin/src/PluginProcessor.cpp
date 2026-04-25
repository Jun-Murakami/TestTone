#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <memory>
#include <vector>

namespace {
    // 対数スキュー（等比マッピング）付き NormalisableRange を組み立てる。
    juce::NormalisableRange<float> makeLogRange(float start, float end, float interval = 0.0f)
    {
        return juce::NormalisableRange<float>(
            start, end,
            [](float a, float b, float t)  { return a * std::pow(b / a, t); },
            [](float a, float b, float v)  { return std::log(v / a) / std::log(b / a); },
            [interval](float a, float b, float v)
            {
                v = juce::jlimit(a, b, v);
                if (interval > 0.0f)
                    v = a * std::pow(b / a, std::round(std::log(v / a) / std::log(b / a) / interval) * interval);
                return v;
            });
    }
}

TestToneAudioProcessor::TestToneAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("TestTone"), createParameterLayout())
{
}

TestToneAudioProcessor::~TestToneAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout TestToneAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // TONE_TYPE: 0=Sine / 1=Pink Noise（既定 Sine）
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        tt::id::TONE_TYPE,
        "Tone Type",
        juce::StringArray{ "Sine", "Pink Noise" },
        0));

    // FREQUENCY: 20..20000 Hz（log skew、既定 1000 Hz）
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        tt::id::FREQUENCY,
        "Frequency",
        makeLogRange(20.0f, 20000.0f),
        1000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    // DBFS: -90..0 dB（リニア、既定 -18 dB）
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        tt::id::DBFS,
        "Level",
        juce::NormalisableRange<float>(-90.0f, 0.0f, 0.1f),
        -18.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // ON: 既定 OFF（プラグイン挿入時に意図せず音を出さない）
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        tt::id::ON,
        "On",
        false));

    // CH_L / CH_R: チャンネル個別ミュート。既定 ON。両方 ON で通常出力。
    //  片方 OFF にすると、そのチャンネルだけサイレンス（mono バスでは CH_R は無視される）。
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        tt::id::CH_L,
        "Channel L",
        true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        tt::id::CH_R,
        "Channel R",
        true));

    return { params.begin(), params.end() };
}

const juce::String TestToneAudioProcessor::getName() const { return JucePlugin_Name; }
bool TestToneAudioProcessor::acceptsMidi() const           { return false; }
bool TestToneAudioProcessor::producesMidi() const          { return false; }
bool TestToneAudioProcessor::isMidiEffect() const          { return false; }
double TestToneAudioProcessor::getTailLengthSeconds() const{ return 0.0; }

int TestToneAudioProcessor::getNumPrograms() { return 1; }
int TestToneAudioProcessor::getCurrentProgram() { return 0; }
void TestToneAudioProcessor::setCurrentProgram(int) {}
const juce::String TestToneAudioProcessor::getProgramName(int) { return {}; }
void TestToneAudioProcessor::changeProgramName(int, const juce::String&) {}

void TestToneAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    generator.prepare(sampleRate);
}

void TestToneAudioProcessor::releaseResources()
{
    generator.reset();
}

bool TestToneAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut.isDisabled()) return false;
    // mono / stereo どちらも許可。入力は無視するが、ホスト互換のため main I/O は同じ構成を要求する。
    if (mainOut != juce::AudioChannelSet::mono()
     && mainOut != juce::AudioChannelSet::stereo()) return false;
    const auto& mainIn = layouts.getMainInputChannelSet();
    return mainIn.isDisabled() || mainIn == mainOut;
}

void TestToneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // 入力は完全に無視して、テスト信号で上書きする（テストツールとして自然）。
    const float freqHz = parameters.getRawParameterValue(tt::id::FREQUENCY.getParamID())->load();
    const float levelDb = parameters.getRawParameterValue(tt::id::DBFS.getParamID())->load();
    const bool  isOn    = parameters.getRawParameterValue(tt::id::ON.getParamID())->load() > 0.5f;
    const int   typeIdx = static_cast<int>(parameters.getRawParameterValue(tt::id::TONE_TYPE.getParamID())->load() + 0.5f);

    generator.setType(typeIdx == 1 ? tt::dsp::ToneGenerator::Type::PinkNoise
                                   : tt::dsp::ToneGenerator::Type::Sine);
    generator.setFrequencyHz(freqHz);
    generator.setLevelDbfs(levelDb);
    generator.setOn(isOn);

    generator.renderBlock(buffer, 0, numSamples);

    // チャンネル個別ミュート: ON=true の時にだけ意味を持つ（OFF 時は generator が既に全 ch を 0 にする）。
    //  L=ch0, R=ch1 という JUCE のステレオレイアウト前提。mono バスでは ch1 が無いので CH_R は no-op。
    if (isOn)
    {
        const bool chL = parameters.getRawParameterValue(tt::id::CH_L.getParamID())->load() > 0.5f;
        const bool chR = parameters.getRawParameterValue(tt::id::CH_R.getParamID())->load() > 0.5f;
        const int numChannels = buffer.getNumChannels();
        if (! chL && numChannels > 0) buffer.clear(0, 0, numSamples);
        if (! chR && numChannels > 1) buffer.clear(1, 0, numSamples);
    }
}

bool TestToneAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* TestToneAudioProcessor::createEditor()
{
    return new TestToneAudioProcessorEditor(*this);
}

void TestToneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void TestToneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestToneAudioProcessor();
}
