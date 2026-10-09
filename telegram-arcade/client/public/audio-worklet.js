// AudioWorklet: a stereo ring buffer with linear resampling from the
// emulator's sample rate to the device rate. Keeps latency bounded: when more
// than ~120 ms is queued the oldest audio is dropped; on underrun it plays
// silence instead of stalling the game.
class ArcadeOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.size = 48000;            // frames (1 s at 48 kHz)
    this.l = new Float32Array(this.size);
    this.r = new Float32Array(this.size);
    this.read = 0;                // fractional read position
    this.write = 0;
    this.count = 0;               // frames queued
    this.step = 1;                // coreRate / outRate
    this.maxQueued = 0.12;        // seconds
    this.coreRate = 48000;
    this.port.onmessage = (e) => {
      const m = e.data;
      if (m.t === 'rate') {
        this.coreRate = m.coreRate;
        this.step = m.coreRate / (m.outRate || sampleRate);
      } else if (m.t === 'flush') {
        this.count = 0;
        this.read = this.write;
      } else if (m.t === 'pcm') {
        const d = m.data;
        const n = d.length >> 1;
        for (let i = 0; i < n; i++) {
          this.l[this.write] = d[2 * i];
          this.r[this.write] = d[2 * i + 1];
          this.write = (this.write + 1) % this.size;
        }
        this.count = Math.min(this.size - 1, this.count + n);
        const limit = Math.floor(this.coreRate * this.maxQueued);
        if (this.count > limit) {
          const drop = this.count - Math.floor(limit / 2);
          this.read = (this.read + drop) % this.size;
          this.count -= drop;
        }
      }
    };
  }

  process(_inputs, outputs) {
    const out = outputs[0];
    const L = out[0], R = out[1] || out[0];
    for (let i = 0; i < L.length; i++) {
      if (this.count < 2) {
        L[i] = 0;
        R[i] = 0;
        continue;
      }
      const i0 = Math.floor(this.read) % this.size;
      const i1 = (i0 + 1) % this.size;
      const t = this.read - Math.floor(this.read);
      L[i] = this.l[i0] + (this.l[i1] - this.l[i0]) * t;
      R[i] = this.r[i0] + (this.r[i1] - this.r[i0]) * t;
      const before = Math.floor(this.read);
      this.read += this.step;
      const advanced = Math.floor(this.read) - before;
      if (this.read >= this.size) this.read -= this.size;
      this.count -= advanced;
    }
    return true;
  }
}
registerProcessor('arcade-out', ArcadeOut);
