// Thin wrapper around a WebAssembly core built by native/build-cores.sh.
// Used unchanged by the server's emulation worker (Node) and by browsers.

export interface CoreModule {
  FS: {
    mkdir(path: string): void;
    writeFile(path: string, data: Uint8Array): void;
    unlink(path: string): void;
    analyzePath(path: string): { exists: boolean };
  };
  HEAPU8: Uint8Array;
  HEAP16: Int16Array;
  HEAPU32: Uint32Array;
  UTF8ToString(ptr: number): string;
  stringToNewUTF8(s: string): number;
  _malloc(n: number): number;
  _free(ptr: number): void;
  [fn: string]: any;
}

export type CoreFactory = (opts: Record<string, unknown>) => Promise<CoreModule>;

export interface InputDescriptor {
  port: number;
  device: number;
  index: number;
  id: number;
  text: string;
}

export interface FrameImage {
  width: number;
  height: number;
  rgba: Uint8ClampedArray; // view into wasm memory; copy before the next frame
}

export const RETRO_MEMORY_SYSTEM_RAM = 2;
export const RETRO_SAVESTATE_CONTEXT_NORMAL = 0;
export const RETRO_SAVESTATE_CONTEXT_ROLLBACK_NETPLAY = 3;

// RetroPad bit indexes (RETRO_DEVICE_ID_JOYPAD_*).
export const PAD = {
  B: 0, Y: 1, SELECT: 2, START: 3, UP: 4, DOWN: 5, LEFT: 6, RIGHT: 7,
  A: 8, X: 9, L: 10, R: 11, L2: 12, R2: 13, L3: 14, R3: 15,
} as const;

export class Core {
  readonly m: CoreModule;
  private loaded = false;
  private logLines: string[] = [];

  private constructor(m: CoreModule) {
    this.m = m;
  }

  // wasmBinary is passed explicitly so hosts control fetching/caching.
  static async create(factory: CoreFactory, wasmBinary: ArrayBuffer | Uint8Array, opts: { quiet?: boolean } = {}): Promise<Core> {
    const lines: string[] = [];
    const sink = (s: string) => {
      if (lines.length < 200) lines.push(s);
    };
    const m = await factory({
      wasmBinary,
      print: opts.quiet ? sink : (s: string) => { sink(s); console.log(s); },
      printErr: opts.quiet ? sink : (s: string) => { sink(s); console.warn(s); },
    });
    const core = new Core(m);
    core.logLines = lines;
    for (const dir of ['/system', '/save', '/roms']) {
      if (!m.FS.analyzePath(dir).exists) m.FS.mkdir(dir);
    }
    m._fe_init();
    return core;
  }

  get log(): readonly string[] {
    return this.logLines;
  }

  private str(ptr: number): string {
    return ptr ? this.m.UTF8ToString(ptr) : '';
  }

  private withString<T>(s: string, fn: (ptr: number) => T): T {
    const p = this.m.stringToNewUTF8(s);
    try {
      return fn(p);
    } finally {
      this.m._free(p);
    }
  }

  get name(): string { return this.str(this.m._fe_core_name()); }
  get version(): string { return this.str(this.m._fe_core_version()); }
  get extensions(): string { return this.str(this.m._fe_core_extensions()); }

  writeFile(path: string, data: Uint8Array): void {
    this.m.FS.writeFile(path, data);
  }

  setOption(key: string, value: string): void {
    this.withString(key, (k) => this.withString(value, (v) => this.m._fe_set_option(k, v)));
  }

  getOption(key: string): string | null {
    const p = this.withString(key, (k) => this.m._fe_get_option(k));
    return p ? this.str(p) : null;
  }

  options(): Record<string, string> {
    const out: Record<string, string> = {};
    const n = this.m._fe_option_count();
    for (let i = 0; i < n; i++) {
      const key = this.str(this.m._fe_option_key(i));
      out[key] = this.getOption(key) ?? '';
    }
    return out;
  }

  setEpoch(epochSeconds: number): void { this.m._fe_set_epoch(epochSeconds); }
  setLogLevel(level: number): void { this.m._fe_set_log_level(level); }
  setPortDevice(port: number, device: number): void { this.m._fe_set_port_device(port, device); }

  loadGame(path: string): boolean {
    if (this.loaded) throw new Error('a game is already loaded');
    const ok = this.withString(path, (p) => this.m._fe_load_game(p)) === 1;
    this.loaded = ok;
    return ok;
  }

  get fps(): number { return this.m._fe_fps(); }
  get sampleRate(): number { return this.m._fe_sample_rate(); }
  get baseWidth(): number { return this.m._fe_base_width(); }
  get baseHeight(): number { return this.m._fe_base_height(); }
  get aspect(): number { return this.m._fe_aspect(); }
  get rotation(): number { return this.m._fe_rotation(); }
  get frame(): number { return this.m._fe_frame(); }
  set frame(f: number) { this.m._fe_set_frame(f); }

  setInput(port: number, mask: number): void { this.m._fe_set_input(port, mask & 0xffff); }

  // Runs one frame. Returns true if the core produced a new image.
  runFrame(render: boolean, audio: boolean): boolean {
    return this.m._fe_run_frame(render ? 1 : 0, audio ? 1 : 0) === 1;
  }

  image(): FrameImage | null {
    const ptr = this.m._fe_video_ptr();
    const w = this.m._fe_video_width();
    const h = this.m._fe_video_height();
    if (!ptr || !w || !h) return null;
    return { width: w, height: h, rgba: new Uint8ClampedArray(this.m.HEAPU8.buffer, ptr, w * h * 4) };
  }

  // Interleaved stereo int16 samples produced by the last frame (view; copy if kept).
  audio(): Int16Array {
    const frames = this.m._fe_audio_frames();
    const ptr = this.m._fe_audio_ptr();
    return new Int16Array(this.m.HEAP16.buffer, ptr, frames * 2);
  }

  setSavestateContext(ctx: number): void { this.m._fe_set_savestate_context(ctx); }

  serialize(): Uint8Array {
    const n = this.m._fe_serialize();
    if (!n) throw new Error('serialize failed');
    const ptr = this.m._fe_state_ptr();
    return this.m.HEAPU8.slice(ptr, ptr + n);
  }

  unserialize(state: Uint8Array): void {
    const ptr = this.m._fe_state_reserve(state.length);
    if (!ptr) throw new Error('out of memory for state');
    this.m.HEAPU8.set(state, ptr);
    if (this.m._fe_unserialize_buffer(state.length) !== 1) throw new Error('unserialize failed');
  }

  stateHash(): string {
    const ptr = this.m._fe_state_hash();
    if (!ptr) throw new Error('hash failed');
    let s = '';
    for (let i = 7; i >= 0; i--) s += this.m.HEAPU8[ptr + i].toString(16).padStart(2, '0');
    return s;
  }

  memory(id = RETRO_MEMORY_SYSTEM_RAM): Uint8Array | null {
    const ptr = this.m._fe_memory_ptr(id);
    const size = this.m._fe_memory_size(id);
    if (!ptr || !size) return null;
    return new Uint8Array(this.m.HEAPU8.buffer, ptr, size);
  }

  descriptors(): InputDescriptor[] {
    const out: InputDescriptor[] = [];
    const n = this.m._fe_descriptor_count();
    for (let i = 0; i < n; i++) {
      const p = this.m._fe_descriptor_get(i) >> 2;
      const u = this.m.HEAPU32;
      out.push({ port: u[p], device: u[p + 1], index: u[p + 2], id: u[p + 3], text: this.str(this.m._fe_descriptor_text(i)) });
    }
    return out;
  }

  driverInfo(): any {
    return JSON.parse(this.str(this.m._fe_driver_info()) || '{}');
  }

  catalog(): any[] {
    return JSON.parse(this.str(this.m._fe_catalog()) || '[]');
  }

  reset(): void { this.m._fe_reset(); }

  unload(): void {
    if (this.loaded) this.m._fe_unload();
    this.loaded = false;
  }
}
