// Audio output through an AudioWorklet ring buffer. The emulator produces
// int16 stereo at the core's rate; the worklet resamples to the device rate.
// Browsers start audio suspended until a user gesture; unlock() is called
// from the first tap/key/click.

export class AudioOut {
  private ctx: AudioContext | null = null;
  private node: AudioWorkletNode | null = null;
  private gain: GainNode | null = null;
  private coreRate = 48000;
  private volume = 0.8;
  private muted = false;
  private failed = false;
  ready = false;

  async init(coreRate: number): Promise<void> {
    this.coreRate = coreRate || 48000;
    if (this.ctx || this.failed) {
      this.node?.port.postMessage({ t: 'rate', coreRate: this.coreRate, outRate: this.ctx?.sampleRate });
      return;
    }
    try {
      const Ctx = (window as any).AudioContext || (window as any).webkitAudioContext;
      this.ctx = new Ctx({ latencyHint: 'interactive' }) as AudioContext;
      await this.ctx.audioWorklet.addModule('/audio-worklet.js');
      this.node = new AudioWorkletNode(this.ctx, 'arcade-out', { outputChannelCount: [2] });
      this.gain = this.ctx.createGain();
      this.gain.gain.value = this.muted ? 0 : this.volume;
      this.node.connect(this.gain).connect(this.ctx.destination);
      this.node.port.postMessage({ t: 'rate', coreRate: this.coreRate, outRate: this.ctx.sampleRate });
      this.ready = true;
    } catch {
      // No AudioWorklet (very old webviews): the game still runs, silently.
      this.failed = true;
      this.ready = false;
    }
  }

  private keepAlive: HTMLAudioElement | null = null;

  // Called from a user gesture (and again when the app becomes visible:
  // WKWebView can leave a "running" AudioContext silent after backgrounding).
  unlock(): void {
    if (this.ctx && this.ctx.state !== 'running') void this.ctx.resume().catch(() => {});
    if (this.muted) return;
    // iOS: Web Audio follows the ringer switch unless the audio session is
    // "playback" (Safari/WKWebView 16.4+). Some WKWebView builds also need a
    // playing media element, so a looping, silent <audio> is started too.
    const s = (navigator as any).audioSession;
    if (s) {
      try {
        s.type = 'playback';
      } catch { /* not supported */ }
    }
    if (!this.keepAlive && /iPhone|iPad|iPod|Macintosh/.test(navigator.userAgent) && 'ontouchend' in document) {
      const a = new Audio('/silence.wav');
      a.loop = true;
      a.volume = 0.01;
      a.setAttribute('playsinline', '');
      void a.play().catch(() => {});
      this.keepAlive = a;
    }
  }

  get state(): string {
    if (this.failed) return 'unavailable';
    return this.ctx?.state ?? 'off';
  }

  setVolume(v: number): void {
    this.volume = Math.max(0, Math.min(1, v));
    if (this.gain) this.gain.gain.value = this.muted ? 0 : this.volume;
  }

  setMuted(m: boolean): void {
    this.muted = m;
    if (this.gain) this.gain.gain.value = m ? 0 : this.volume;
  }

  push(samples: Int16Array): void {
    if (!this.node || samples.length === 0 || this.ctx?.state !== 'running') return;
    const f = new Float32Array(samples.length);
    for (let i = 0; i < samples.length; i++) f[i] = samples[i] / 32768;
    this.node.port.postMessage({ t: 'pcm', data: f }, [f.buffer]);
  }

  flush(): void {
    this.node?.port.postMessage({ t: 'flush' });
  }
}
