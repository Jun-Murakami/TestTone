// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Jun Murakami
/**
 * juceBridge の Web 互換実装。
 * App.tsx の `juceBridge.callNative(...)` / `addEventListener(...)` をそのまま動かす薄いラッパー。
 */

import { webAudioEngine } from './WebAudioEngine';

type EventCallback = (data: unknown) => void;

class WebBridgeManager
{
  private initialized = false;
  private initCallbacks: Array<() => void> = [];
  private startPromise: Promise<void> | null = null;

  /**
   * 初回起動。**必ずユーザジェスチャ（tap/click）のハンドラから同期的に**呼ぶこと。
   * ジェスチャ同期で AudioContext を立ち上げ、その後 worklet + WASM を非同期初期化する。
   */
  public ensureStarted(): Promise<void>
  {
    if (this.startPromise) return this.startPromise;

    // ここで必ず同期フレーム内に AudioContext を作る（最初の await より前）
    const unlocked = webAudioEngine.startFromUserGesture();

    this.startPromise = unlocked
      .then(() => {
        this.initialized = true;
        this.initCallbacks.forEach((cb) => cb());
        this.initCallbacks = [];
      })
      .catch((err) => {
        console.error('[WebBridge] Initialization failed:', err);
      });

    return this.startPromise;
  }

  public isStarted(): boolean { return this.startPromise !== null; }

  public whenReady(callback: () => void): void
  {
    if (this.initialized) callback();
    else this.initCallbacks.push(callback);
  }

  public async callNative(functionName: string, ...args: unknown[]): Promise<unknown>
  {
    if (functionName === 'system_action') return null;
    if (functionName === 'window_action') return null;
    if (functionName === 'open_url')
    {
      if (typeof args[0] === 'string') window.open(args[0], '_blank');
      return true;
    }
    return null;
  }

  public addEventListener(event: string, callback: EventCallback): string
  {
    return webAudioEngine.addEventListener(event, callback);
  }

  public removeEventListener(key: string): void
  {
    webAudioEngine.removeEventListener(key);
  }

  public emitEvent(_event: string, _data: unknown): void {}
}

export const webBridge = new WebBridgeManager();
