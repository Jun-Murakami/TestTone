// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ParameterIDs.h"
#include "Version.h"

// CMake 未構成時（IntelliSense/分岐切替直後など、生成済み Version.h が include パスに無い状態）でも
//  コンパイル・解析が通るようフォールバックを定義する。実ビルドでは Version.h の値が優先される。
#ifndef TESTTONE_VERSION_STRING
 #define TESTTONE_VERSION_STRING "0.0.0-dev"
#endif
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

[[maybe_unused]] std::vector<std::byte> streamToVector(juce::InputStream& stream)
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
                  [this](const juce::Array<juce::var>& args,
                         juce::WebBrowserComponent::NativeFunctionCompletion completion)
                  {
                      // 固定サイズプラグインなのでリサイズ要求(resizeTo 等)は無視するが、apply_layout
                      //  だけは処理する。WebView が報告する真のディスプレイ倍率 devicePixelRatio を確定し、
                      //  applyDisplayScale で Linux 埋め込み時のウィンドウ物理サイズを補正する。
                      if (args.size() >= 3 && args[0].toString() == "apply_layout")
                      {
                          lastWebViewDpr = (args.size() >= 4) ? static_cast<double>(args[3]) : -1.0;
                          applyDisplayScale();
                          completion(juce::var{ true });
                          return;
                      }
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

    // WebView を明示的に teardown してから破棄する。これをしないと Linux + NVIDIA で
    //  Standalone 終了時に WebKit/EGL のクリーンアップ順序が崩れ、libEGL_nvidia の atexit で
    //  SEGV する（JUCE 8.0.13 の外部サブプロセス化とあわせて確実にする。MixCompare と同じ手順）。
    if (webViewLifetimeGuard.isConstructed())
    {
        webView.goToURL("about:blank");
        webView.stop();
        webView.setVisible(false);
    }
    removeChildComponent(&webView);
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

void TestToneAudioProcessorEditor::applyDisplayScale()
{
#if JUCE_LINUX || JUCE_BSD
    // ※ この transform 補正は Linux 専用。macOS(WKWebView)/Windows(WebView2) は Retina・高DPI を native に
    //   処理するため transform 不要。macOS では getPlatformScaleFactor() が Retina でも 1.0 を返す一方
    //   devicePixelRatio は 2.0 のため、無条件適用すると s=2.0 で窓が倍に膨らむ。Windows は両者一致で偶然
    //   s=1.0 に収束するだけ。将来の DPI 不一致事故も含め Linux/BSD 以外では一切走らせない。
    //
    // 補正の唯一の目的は「WebView の CSS ビューポートを設計値(kFixedWidth/Height)へ一致させる」こと。
    //  WebView 物理px = 設計CSS × T × peerScale、CSS ビューポート = 物理px / webViewDpr。これを設計値へ
    //  一致させる解は wrapperType に依らず T = webViewDpr / peerScale。
    //
    //  peerScale = ComponentPeer::getPlatformScaleFactor() は「JUCE/OS が既にウィンドウを何倍に物理拡大
    //  したか」を表す権威ある実スケール（Linux では LinuxComponentPeer が display->scale / globalScale で算出）。
    //  これを判定軸にするので 1 本の式で両ケースが成立し、二重拡大も自動回避される:
    //    - OS が拡大済み (peerScale==webViewDpr)        → T=1.0（恒等。何も足さない）
    //    - OS が未拡大   (peerScale=1.0, webViewDpr=2.0) → T=2.0（正しく拡大）
    //
    //  かつては Standalone を一律 setTransform({}) で除外していたが、これは「Standalone は OS が必ず正しく
    //  拡大する」前提に依存しており KDE/Wayland(XWayland) で破綻する: JUCE の display->scale は GNOME 互換
    //  gsettings(scaling-factor=1) を拾って 1.0 になる一方、WebKitGTK は GDK スケール 2 で webViewDpr=2.0。
    //  結果 peerScale=1.0 のまま transform 無しだとウィンドウは設計px のまま小さく、CSS ビューポートは
    //  450/2=225px へ潰れてレイアウトが崩れる。よって Standalone も同じ T=webViewDpr/peerScale を適用する。
    //  （StandaloneFilterWindow は getSizeToContainEditor が editor->getTransform() を見て窓サイズを追従
    //   させるため、transform を掛けるだけで OS ウィンドウも正しいサイズに広がる。）
    double peerScale = 1.0;
    if (auto* p = getPeer())
    {
        const double ps = p->getPlatformScaleFactor();
        if (ps > 0.0)
            peerScale = ps;
    }
    const float s = (lastWebViewDpr > 0.0) ? (float) (lastWebViewDpr / peerScale) : 1.0f;
    setTransform(juce::AffineTransform::scale(s));

    // 固定サイズなので settle 機構が無い。transform 適用後に WebView ネイティブ子窓を新 transform 下へ
    //  再配置させるため、1px ジグルで resized()→webView.setBounds を再発火させる。同期連続 setBounds は
    //  WebKitGTK の描画を固めるため 2-tick に分割する（step1: 1px 縮め / step2: 次 tick で戻す）。
    setSize(kFixedWidth, kFixedHeight - 1);
    resyncStep2Pending = true;
#endif
}

void TestToneAudioProcessorEditor::setScaleFactor(float /*newHostScale*/)
{
    // ホスト(VST3 setContentScaleFactor / CLAP guiSetScale)が宣言する scale は誤判定するため使わず、
    //  applyDisplayScale が webViewDpr/peerScale で正しい transform を（再）適用する。
    applyDisplayScale();
}

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

#if JUCE_LINUX || JUCE_BSD
    // applyDisplayScale が張った 1px ジグルの 2-tick 目: 縮めた分を固定サイズへ戻し、新 transform 下で
    //  WebView 子窓を最終配置に収束させる（同期連続 setBounds による WebKitGTK 描画固着を避けるため分割）。
    if (resyncStep2Pending)
    {
        resyncStep2Pending = false;
        setSize(kFixedWidth, kFixedHeight);
    }
#endif

   #if defined(JUCE_WINDOWS)
    pollAndMaybeNotifyDpiChange();
   #endif

    pollAndEmitChannelLayout();
}
