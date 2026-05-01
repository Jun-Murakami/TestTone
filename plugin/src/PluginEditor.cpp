// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "Version.h"
#include "KeyEventForwarder.h"

#include <unordered_map>
#include <cmath>

#if defined(JUCE_WINDOWS)
 #include <windows.h>
#endif

#if __has_include(<WebViewFiles.h>)
#include <WebViewFiles.h>
#endif

#ifndef LOCAL_DEV_SERVER_ADDRESS
#define LOCAL_DEV_SERVER_ADDRESS "http://127.0.0.1:5173"
#endif

namespace {

std::vector<std::byte> streamToVector(juce::InputStream& stream)
{
    const auto sizeInBytes = static_cast<size_t>(stream.getTotalLength());
    std::vector<std::byte> result(sizeInBytes);
    stream.setPosition(0);
    [[maybe_unused]] const auto bytesRead = stream.read(result.data(), result.size());
    jassert(static_cast<size_t>(bytesRead) == sizeInBytes);
    return result;
}

#if !TESTTONE_DEV_MODE && __has_include(<WebViewFiles.h>)
static const char* getMimeForExtension(const juce::String& extension)
{
    static const std::unordered_map<juce::String, const char*> mimeMap = {
        {{"htm"},   "text/html"},
        {{"html"},  "text/html"},
        {{"txt"},   "text/plain"},
        {{"jpg"},   "image/jpeg"},
        {{"jpeg"},  "image/jpeg"},
        {{"svg"},   "image/svg+xml"},
        {{"ico"},   "image/vnd.microsoft.icon"},
        {{"json"},  "application/json"},
        {{"png"},   "image/png"},
        {{"css"},   "text/css"},
        {{"map"},   "application/json"},
        {{"js"},    "text/javascript"},
        {{"woff2"}, "font/woff2"}};

    if (const auto it = mimeMap.find(extension.toLowerCase()); it != mimeMap.end())
        return it->second;

    jassertfalse;
    return "";
}

#ifndef ZIPPED_FILES_PREFIX
#error "You must provide the prefix of zipped web UI files' paths via ZIPPED_FILES_PREFIX compile definition"
#endif

std::vector<std::byte> getWebViewFileAsBytes(const juce::String& filepath)
{
    juce::MemoryInputStream zipStream{ webview_files::webview_files_zip,
                                       webview_files::webview_files_zipSize,
                                       false };
    juce::ZipFile zipFile{ zipStream };

    const auto fullPath = ZIPPED_FILES_PREFIX + filepath;
    if (auto* zipEntry = zipFile.getEntry(fullPath))
    {
        const std::unique_ptr<juce::InputStream> entryStream{ zipFile.createStreamForEntry(*zipEntry) };
        if (entryStream == nullptr) { jassertfalse; return {}; }
        return streamToVector(*entryStream);
    }
    return {};
}
#else
[[maybe_unused]] static std::vector<std::byte> getWebViewFileAsBytes(const juce::String& filepath)
{
    juce::ignoreUnused(filepath);
    return {};
}
#endif

#if defined(JUCE_WINDOWS)
// HWND 基準の DPI をスケール係数へ変換。Per-Monitor V2 対応。
static void queryWindowDpi(HWND hwnd, int& outDpi, double& outScale)
{
    outDpi = 0;
    outScale = 1.0;
    if (hwnd == nullptr) return;

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr)
    {
        using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
        auto pGetDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(::GetProcAddress(user32, "GetDpiForWindow"));
        if (pGetDpiForWindow != nullptr)
        {
            const UINT dpi = pGetDpiForWindow(hwnd);
            if (dpi != 0)
            {
                outDpi = static_cast<int>(dpi);
                outScale = static_cast<double>(dpi) / 96.0;
                return;
            }
        }
    }

    HMODULE shcore = ::LoadLibraryW(L"Shcore.dll");
    if (shcore != nullptr)
    {
        using GetDpiForMonitorFn = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);
        auto pGetDpiForMonitor = reinterpret_cast<GetDpiForMonitorFn>(::GetProcAddress(shcore, "GetDpiForMonitor"));
        if (pGetDpiForMonitor != nullptr)
        {
            HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            UINT dpiX = 0, dpiY = 0;
            if (SUCCEEDED(pGetDpiForMonitor(mon, 0 /*MDT_EFFECTIVE_DPI*/, &dpiX, &dpiY)))
            {
                outDpi = static_cast<int>(dpiX);
                outScale = static_cast<double>(dpiX) / 96.0;
            }
        }
        ::FreeLibrary(shcore);
    }
}
#endif

} // namespace

// WebView2/Chromium の起動前に追加のコマンドライン引数を渡すためのヘルパー。
//  ProTools(AAX, Windows) は AAX ラッパー時に DPI 非対応モードで動作することが多く、
//  WebView2 の自動スケーリングがかかると UI が本来の意図より大きく表示されるため
//  --force-device-scale-factor=1 を環境変数経由で注入する。
static juce::WebBrowserComponent::Options makeWebViewOptionsWithPreLaunchArgs(const juce::AudioProcessor& /*processor*/)
{
   #if defined(JUCE_WINDOWS)
    if (juce::PluginHostType().isProTools()
        && juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::WrapperType::wrapperType_AAX)
    {
        const char* kEnvName = "WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS";
        const char* kArg     = "--force-device-scale-factor=1";

        char*  existing = nullptr;
        size_t len = 0;
        if (_dupenv_s(&existing, &len, kEnvName) == 0 && existing != nullptr)
        {
            std::string combined(existing);
            free(existing);
            if (combined.find("--force-device-scale-factor") == std::string::npos)
            {
                if (! combined.empty()) combined += ' ';
                combined += kArg;
                _putenv_s(kEnvName, combined.c_str());
            }
        }
        else
        {
            _putenv_s(kEnvName, kArg);
        }
    }
   #endif
    return juce::WebBrowserComponent::Options{};
}

//==============================================================================

TestToneAudioProcessorEditor::TestToneAudioProcessorEditor(TestToneAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      webToneTypeRelay  { tt::id::TONE_TYPE.getParamID() },
      webFrequencyRelay { tt::id::FREQUENCY.getParamID() },
      webDbfsRelay      { tt::id::DBFS.getParamID() },
      webOnRelay        { tt::id::ON.getParamID() },
      webChLRelay       { tt::id::CH_L.getParamID() },
      webChRRelay       { tt::id::CH_R.getParamID() },
      toneTypeAttachment  { *p.getState().getParameter(tt::id::TONE_TYPE.getParamID()), webToneTypeRelay,  nullptr },
      frequencyAttachment { *p.getState().getParameter(tt::id::FREQUENCY.getParamID()), webFrequencyRelay, nullptr },
      dbfsAttachment      { *p.getState().getParameter(tt::id::DBFS.getParamID()),      webDbfsRelay,      nullptr },
      onAttachment        { *p.getState().getParameter(tt::id::ON.getParamID()),        webOnRelay,        nullptr },
      chLAttachment       { *p.getState().getParameter(tt::id::CH_L.getParamID()),      webChLRelay,       nullptr },
      chRAttachment       { *p.getState().getParameter(tt::id::CH_R.getParamID()),      webChRRelay,       nullptr },
      webView{
          makeWebViewOptionsWithPreLaunchArgs(p)
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withWinWebView2Options(
                  juce::WebBrowserComponent::Options::WinWebView2{}
                      .withBackgroundColour(juce::Colour(0xFF606F77))
                      .withUserDataFolder(juce::File::getSpecialLocation(
                          juce::File::SpecialLocationType::tempDirectory)))
              .withWebViewLifetimeListener(&webViewLifetimeGuard)
              .withNativeIntegrationEnabled()
              .withInitialisationData("vendor", "TestTone")
              .withInitialisationData("pluginName", "TestTone")
              .withInitialisationData("pluginVersion", TESTTONE_VERSION_STRING)
              .withOptionsFrom(controlParameterIndexReceiver)
              .withOptionsFrom(webToneTypeRelay)
              .withOptionsFrom(webFrequencyRelay)
              .withOptionsFrom(webDbfsRelay)
              .withOptionsFrom(webOnRelay)
              .withOptionsFrom(webChLRelay)
              .withOptionsFrom(webChRRelay)
              .withNativeFunction(
                  juce::Identifier{"system_action"},
                  [this](const juce::Array<juce::var>& args,
                         juce::WebBrowserComponent::NativeFunctionCompletion completion)
                  { handleSystemAction(args, std::move(completion)); })
              .withNativeFunction(
                  juce::Identifier{"window_action"},
                  [](const juce::Array<juce::var>& /*args*/,
                     juce::WebBrowserComponent::NativeFunctionCompletion completion)
                  {
                      // 固定サイズプラグインなのでリサイズ要求はすべて無視する。
                      completion(juce::var{ false });
                  })
              .withNativeFunction(
                  juce::Identifier{"open_url"},
                  [](const juce::Array<juce::var>& args,
                     juce::WebBrowserComponent::NativeFunctionCompletion completion)
                  {
                      if (args.size() > 0)
                      {
                          const auto url = args[0].toString();
                          juce::URL(url).launchInDefaultBrowser();
                      }
                      completion(juce::var{ true });
                  })
              .withResourceProvider([this](const juce::String& url) { return getResource(url); })
      }
{
   #if TESTTONE_DEV_MODE
    useLocalDevServer = true;
   #else
    useLocalDevServer = false;
   #endif

    addAndMakeVisible(webView);

    // 固定サイズ。ホスト側にリサイズ不可と伝え、min/max を同値に固定する。
    setResizable(false, false);
    setSize(kFixedWidth, kFixedHeight);
    setResizeLimits(kFixedWidth, kFixedHeight, kFixedWidth, kFixedHeight);

    if (auto* hostConstrainer = getConstrainer())
    {
        hostConstrainer->setSizeLimits(kFixedWidth, kFixedHeight, kFixedWidth, kFixedHeight);
        hostConstrainer->setMinimumOnscreenAmounts(50, 50, 50, 50);
    }

    if (useLocalDevServer)
        webView.goToURL(LOCAL_DEV_SERVER_ADDRESS);
    else
        webView.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    // 一部ホスト（Pro Tools AAX など）はコンストラクタ中の setSize を無視して
    //  保存サイズで開きうるため、次のメッセージループで固定サイズを再強制する。
    juce::Component::SafePointer<TestToneAudioProcessorEditor> safeSelf { this };
    juce::MessageManager::callAsync([safeSelf]()
    {
        if (safeSelf == nullptr) return;
        if (safeSelf->getWidth() != kFixedWidth || safeSelf->getHeight() != kFixedHeight)
            safeSelf->setSize(kFixedWidth, kFixedHeight);
    });

    // DPI ポーリング用のタイマー。テストトーンには周期的描画は不要だが、
    //  Windows のディスプレイ間移動で WebView 領域が見切れる症状の手当てとして残している。
    startTimerHz(30);
}

TestToneAudioProcessorEditor::~TestToneAudioProcessorEditor()
{
    isShuttingDown.store(true, std::memory_order_release);
    stopTimer();
}

void TestToneAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF606F77));
}

void TestToneAudioProcessorEditor::resized()
{
    webView.setBounds(getLocalBounds());
}

void TestToneAudioProcessorEditor::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();

    // Standalone（および host が DocumentWindow を立てる構成）では、ラッパー側のウィンドウが
    //  既定で resizable なので、エディタ自体を非リサイズにしただけだと OS ウィンドウ枠の
    //  リサイズ操作を止められない。親 DocumentWindow が見つかったら直接固定する。
    //  VST3/AU/AAX のような plugin window では DocumentWindow を経由しないので、この処理は
    //  no-op になる（その代わり host 側が isResizable() を見てくれる）。
    if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
    {
        dw->setResizable(false, false);
        if (auto* dwConstrainer = dw->getConstrainer())
            dwConstrainer->setSizeLimits(kFixedWidth, kFixedHeight, kFixedWidth, kFixedHeight);
    }
}

std::optional<TestToneAudioProcessorEditor::Resource>
TestToneAudioProcessorEditor::getResource(const juce::String& url) const
{
   #if TESTTONE_DEV_MODE
    juce::ignoreUnused(url);
    return std::nullopt;
   #else
    #if __has_include(<WebViewFiles.h>)
    const auto cleaned = url.startsWith("/") ? url.substring(1) : url;
    const auto resourcePath = cleaned.isEmpty() ? juce::String("index.html") : cleaned;
    const auto bytes = getWebViewFileAsBytes(resourcePath);
    if (bytes.empty())
        return std::nullopt;

    const auto extension = resourcePath.fromLastOccurrenceOf(".", false, false);
    return Resource{ std::move(bytes), juce::String(getMimeForExtension(extension)) };
    #else
    juce::ignoreUnused(url);
    return std::nullopt;
    #endif
   #endif
}

void TestToneAudioProcessorEditor::handleSystemAction(const juce::Array<juce::var>& args,
                                                     juce::WebBrowserComponent::NativeFunctionCompletion completion)
{
    if (args.size() > 0)
    {
        const auto action = args[0].toString();
        if (action == "ready")
        {
            juce::DynamicObject::Ptr init{ new juce::DynamicObject{} };
            init->setProperty("pluginName", "TestTone");
            init->setProperty("version", TESTTONE_VERSION_STRING);
            completion(juce::var{ init.get() });
            return;
        }
        if (action == "forward_key_event" && args.size() >= 2)
        {
            const bool forwarded = tt::KeyEventForwarder::forwardKeyEventToHost(args[1], this);
            completion(juce::var{ forwarded });
            return;
        }
    }
    completion(juce::var{});
}

#if defined(JUCE_WINDOWS)
void TestToneAudioProcessorEditor::pollAndMaybeNotifyDpiChange()
{
    auto* peer = getPeer();
    if (peer == nullptr) return;

    HWND hwnd = (HWND) peer->getNativeHandle();
    int dpi = 0;
    double scale = 1.0;
    queryWindowDpi(hwnd, dpi, scale);
    if (dpi <= 0) return;

    const bool scaleChanged = std::abs(lastHwndScaleFactor - scale) >= 0.01;
    const bool dpiChanged   = lastHwndDpi != dpi;
    if (! (scaleChanged || dpiChanged)) return;

    lastHwndScaleFactor = scale;
    lastHwndDpi = dpi;

    juce::DynamicObject::Ptr payload{ new juce::DynamicObject{} };
    payload->setProperty("scale", scale);
    payload->setProperty("dpi", dpi);
    webView.emitEventIfBrowserIsVisible("dpiScaleChanged", payload.get());

    const int w = getWidth();
    const int h = getHeight();
    setSize(w + 1, h + 1);
    setSize(w, h);
}
#endif

void TestToneAudioProcessorEditor::pollAndEmitChannelLayout()
{
    // ホスト由来のバス構成（mono=1 / stereo=2）を WebView に通知する。
    //  ホストはランタイムにバス構成を切替えることがある（Pro Tools の trackChannelChange 等）ため、
    //  単発ではなく毎フレームでポーリングして変化時にだけ emit する。
    const int n = audioProcessor.getMainBusNumOutputChannels();
    if (n == lastEmittedNumOutputChannels) return;
    lastEmittedNumOutputChannels = n;

    juce::DynamicObject::Ptr payload{ new juce::DynamicObject{} };
    payload->setProperty("numChannels", n);
    webView.emitEventIfBrowserIsVisible("channelLayoutChanged", payload.get());
}

void TestToneAudioProcessorEditor::timerCallback()
{
    if (isShuttingDown.load(std::memory_order_acquire)) return;
    if (! webViewLifetimeGuard.isConstructed()) return;

   #if defined(JUCE_WINDOWS)
    pollAndMaybeNotifyDpiChange();
   #endif

    pollAndEmitChannelLayout();
}
