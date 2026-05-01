// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
/**
 * Web Audio API + WASM AudioWorklet マネージャ（TestTone 版）。
 *
 * TestTone は入力音源・トランスポート・メーターを持たない純粋なジェネレータなので、
 * このクラスは下記だけを担当する:
 *   - AudioContext のユーザジェスチャ起動と worklet ロード
 *   - WASM のロード
 *   - パラメータ変更を worklet 経由で WASM に送る
 */

type EventCallback = (data: unknown) => void;

export class WebAudioEngine
{
  private audioContext: AudioContext | null = null;
  private workletNode: AudioWorkletNode | null = null;
  private listeners = new Map<string, EventCallback>();
  private nextListenerId = 1;

  // 起動前にユーザが触ったパラメータ値を一時的に保持し、worklet 起動後にまとめて流す。
  //  これがないと、AudioContext 起動前に dragger を動かした分が WASM へ反映されない。
  private pendingParams: Array<{ param: string; value: number | boolean }> = [];

  private initialized = false;
  private startPromise: Promise<void> | null = null;
  private initResolvers: Array<() => void> = [];

  /**
   * 初回起動。**必ずユーザタップ/クリックのハンドラから同期的に**呼ぶこと。
   *
   * iOS WebKit の unlock 条件:
   *   1. `new AudioContext()` をジェスチャ同期フレーム内で実行
   *   2. 同じフレームで `resume()` の Promise を発行
   *   3. 同じフレームで無音 BufferSource を start する
   */
  startFromUserGesture(): Promise<void>
  {
    if (this.startPromise) return this.startPromise;

    const ctx = new AudioContext();
    this.audioContext = ctx;

    // ジェスチャ同期で resume を発行
    const resumed = ctx.resume();

    // iOS 用: ネイティブ sampleRate で 128 サンプルの無音 prime
    const primeFrames = 128;
    const silent = ctx.createBuffer(1, primeFrames, ctx.sampleRate);
    const src = ctx.createBufferSource();
    src.buffer = silent;
    src.connect(ctx.destination);
    src.start(0);

    this.startPromise = (async () => {
      try { await resumed; } catch { /* ignore */ }
      await this.completeInit();
    })();
    return this.startPromise;
  }

  private async completeInit(): Promise<void>
  {
    const ctx = this.audioContext;
    if (!ctx) return;
    try
    {
      await ctx.audioWorklet.addModule('/worklet/dsp-processor.js');

      this.workletNode = new AudioWorkletNode(ctx, 'dsp-processor', {
        numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
      });
      this.workletNode.connect(ctx.destination);
      this.workletNode.port.onmessage = (e) => this.handleWorkletMessage(e.data);

      const resp = await fetch('/wasm/testtone_dsp.wasm');
      if (resp.ok)
      {
        const bytes = await resp.arrayBuffer();
        this.workletNode.port.postMessage({ type: 'init-wasm', wasmBytes: bytes }, [bytes]);
        await new Promise<void>((resolve, reject) => {
          const t = setTimeout(() => reject(new Error('WASM init timeout')), 10000);
          this.initResolvers.push(() => { clearTimeout(t); resolve(); });
        });
      }
      else
      {
        console.warn('[WebAudioEngine] WASM binary not found at /wasm/testtone_dsp.wasm');
      }

      // 起動前にバッファしていたパラメータを順番に流す
      for (const p of this.pendingParams)
        this.workletNode.port.postMessage({ type: 'set-param', param: p.param, value: p.value });
      this.pendingParams = [];
    }
    catch (err)
    {
      console.warn('[WebAudioEngine] Init error:', err);
    }
    this.initialized = true;
  }

  isInitialized(): boolean { return this.initialized; }
  isStarted(): boolean { return this.startPromise !== null; }

  async ensureAudioContext(): Promise<void>
  {
    if (this.audioContext?.state === 'suspended') await this.audioContext.resume();
  }

  // ====== イベント ======

  addEventListener(event: string, callback: EventCallback): string
  {
    const id = `web_${this.nextListenerId++}`;
    this.listeners.set(`${event}:${id}`, callback);
    return `${event}:${id}`;
  }

  removeEventListener(key: string): void { this.listeners.delete(key); }

  private emit(event: string, data: unknown): void
  {
    this.listeners.forEach((cb, key) => { if (key.startsWith(`${event}:`)) cb(data); });
  }

  // ====== Worklet メッセージ ======

  private handleWorkletMessage(msg: Record<string, unknown>): void
  {
    switch (msg.type)
    {
      case 'wasm-ready':
        this.initResolvers.forEach((r) => r());
        this.initResolvers = [];
        break;

      case 'wasm-error':
        this.emit('errorNotification', { severity: 'error', message: 'WASM init failed', details: String(msg.error) });
        break;
    }
  }

  // ====== パラメータ → WASM 直送 ======

  private sendParam(param: string, value: number | boolean): void
  {
    if (this.workletNode)
    {
      this.workletNode.port.postMessage({ type: 'set-param', param, value });
    }
    else
    {
      // worklet 起動前に呼ばれた値はバッファして、初期化完了時にまとめて送る
      this.pendingParams.push({ param, value });
    }
  }

  setToneType(idx: number): void                   { this.sendParam('type',          idx); }
  setFrequencyHz(hz: number): void                 { this.sendParam('frequency_hz', hz); }
  setLevelDbfs(db: number): void                   { this.sendParam('level_dbfs',   db); }
  setOn(on: boolean): void                         { this.sendParam('on',            on); }
  setChannelEnabled(ch: number, enabled: boolean): void
  {
    this.sendParam(ch === 0 ? 'ch_l_enabled' : 'ch_r_enabled', enabled);
  }
}

export const webAudioEngine = new WebAudioEngine();
