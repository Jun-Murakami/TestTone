/**
 * TestTone WASM AudioWorkletProcessor.
 *
 * - 入力ソース無しの純粋なジェネレータ
 * - 全パラメータ (TONE_TYPE / FREQUENCY / DBFS / ON / CH_L / CH_R) を C++ WASM へ転送
 * - 毎フレーム dsp_process_block() で出力を埋める
 *
 * メインスレッド↔ worklet のメッセージ:
 *   in:  init-wasm, set-param
 *   out: wasm-ready, wasm-error
 */

const INITIAL_RENDER_FRAMES = 2048;

class DspProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.wasm = null;
    this.wasmReady = false;
    this.wasmMemory = null;

    this.outLPtr = 0;
    this.outRPtr = 0;
    this.renderBufferFrames = 0;
    this.heapF32 = null;

    this.port.onmessage = (e) => this.handleMessage(e.data);
  }

  handleMessage(msg) {
    switch (msg.type) {
      case 'init-wasm':
        this.initWasm(msg.wasmBytes);
        break;

      case 'set-param': {
        if (!this.wasm) break;
        const p = msg.param;
        const v = msg.value;
        if (p === 'type')               this.wasm.dsp_set_type(v | 0);
        else if (p === 'frequency_hz')  this.wasm.dsp_set_frequency_hz(v);
        else if (p === 'level_dbfs')    this.wasm.dsp_set_level_dbfs(v);
        else if (p === 'on')            this.wasm.dsp_set_on(v ? 1 : 0);
        else if (p === 'ch_l_enabled')  this.wasm.dsp_set_channel_enabled(0, v ? 1 : 0);
        else if (p === 'ch_r_enabled')  this.wasm.dsp_set_channel_enabled(1, v ? 1 : 0);
        break;
      }
    }
  }

  async initWasm(wasmBytes) {
    try {
      const module = await WebAssembly.compile(wasmBytes);
      const importObject = {
        env: { emscripten_notify_memory_growth: () => {} },
      };
      const instance = await WebAssembly.instantiate(module, importObject);
      if (instance.exports._initialize) instance.exports._initialize();

      this.wasm = instance.exports;
      this.wasmMemory = instance.exports.memory;

      // sampleRate は AudioWorkletGlobalScope のグローバル定数
      this.wasm.dsp_init(sampleRate);

      if (!this.ensureRenderBufferCapacity(INITIAL_RENDER_FRAMES)) {
        throw new Error('WASM audio buffer allocation failed');
      }

      this.refreshHeapView();

      this.wasmReady = true;
      this.port.postMessage({ type: 'wasm-ready' });
    } catch (err) {
      this.port.postMessage({ type: 'wasm-error', error: String(err) });
    }
  }

  refreshHeapView() {
    if (!this.wasmMemory) return false;
    if (!this.heapF32 || this.heapF32.buffer !== this.wasmMemory.buffer) {
      this.heapF32 = new Float32Array(this.wasmMemory.buffer);
    }
    return true;
  }

  ensureRenderBufferCapacity(frameCount) {
    if (!this.wasm || frameCount <= 0) return false;
    if (frameCount <= this.renderBufferFrames && this.refreshHeapView()) return true;

    const nextFrames = Math.max(frameCount, INITIAL_RENDER_FRAMES);
    const nextL = this.wasm.dsp_alloc_buffer(nextFrames);
    const nextR = this.wasm.dsp_alloc_buffer(nextFrames);
    if (!nextL || !nextR) {
      if (nextL) this.wasm.dsp_free_buffer(nextL);
      if (nextR) this.wasm.dsp_free_buffer(nextR);
      return false;
    }

    if (this.outLPtr) this.wasm.dsp_free_buffer(this.outLPtr);
    if (this.outRPtr) this.wasm.dsp_free_buffer(this.outRPtr);
    this.outLPtr = nextL;
    this.outRPtr = nextR;
    this.renderBufferFrames = nextFrames;
    return this.refreshHeapView();
  }

  process(_inputs, outputs) {
    if (!this.wasmReady) return true;

    const output = outputs[0];
    if (!output || output.length < 2) return true;
    const outL = output[0];
    const outR = output[1];
    const n = outL.length;
    if (!this.ensureRenderBufferCapacity(n)) {
      outL.fill(0);
      outR.fill(0);
      return true;
    }

    this.wasm.dsp_process_block(this.outLPtr, this.outRPtr, n);

    this.refreshHeapView();
    const heap = this.heapF32;
    const lBase = this.outLPtr / 4;
    const rBase = this.outRPtr / 4;
    for (let i = 0; i < n; ++i) {
      outL[i] = heap[lBase + i];
      outR[i] = heap[rBase + i];
    }

    return true;
  }
}

registerProcessor('dsp-processor', DspProcessor);
