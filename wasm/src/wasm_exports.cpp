// JS 側（AudioWorklet）が呼ぶ C ABI。
// エンジン本体は tone_generator.h。
#include "tone_generator.h"
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

static tt_wasm::ToneGenerator* g_engine = nullptr;

extern "C" {

// ---------- 初期化 / 解放 ----------

WASM_EXPORT void dsp_init(double sampleRate)
{
    if (g_engine) delete g_engine;
    g_engine = new tt_wasm::ToneGenerator();
    g_engine->prepare(sampleRate);
}

WASM_EXPORT void dsp_destroy()
{
    delete g_engine;
    g_engine = nullptr;
}

// ---------- メモリ（WASM heap 上にスクラッチを確保するため JS から呼ぶ）----------

WASM_EXPORT float* dsp_alloc_buffer(int numSamples)
{
    return static_cast<float*>(std::malloc(sizeof(float) * static_cast<size_t>(numSamples)));
}

WASM_EXPORT void dsp_free_buffer(float* p)
{
    std::free(p);
}

// ---------- パラメータ ----------

WASM_EXPORT void dsp_set_type(int t)              { if (g_engine) g_engine->setType(t); }
WASM_EXPORT void dsp_set_frequency_hz(float hz)   { if (g_engine) g_engine->setFrequencyHz(hz); }
WASM_EXPORT void dsp_set_level_dbfs(float db)     { if (g_engine) g_engine->setLevelDbfs(db); }
WASM_EXPORT void dsp_set_on(int on)               { if (g_engine) g_engine->setOn(on != 0); }
WASM_EXPORT void dsp_set_channel_enabled(int ch, int enabled)
{
    if (g_engine) g_engine->setChannelEnabled(ch, enabled != 0);
}

// ---------- 処理 ----------

WASM_EXPORT void dsp_process_block(float* outL, float* outR, int numSamples)
{
    if (g_engine) g_engine->processBlock(outL, outR, numSamples);
}

WASM_EXPORT void dsp_reset()
{
    if (g_engine) g_engine->reset();
}

} // extern "C"
