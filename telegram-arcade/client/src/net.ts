// WebSocket client: authenticates with our session token as the first
// message, reconnects with backoff, routes JSON and binary messages.

export type JsonHandler = (msg: any) => void;
export type BinHandler = (data: Uint8Array) => void;

export class Net {
  private ws: WebSocket | null = null;
  private readonly url: string;
  private readonly token: () => string | null;
  private backoff = 500;
  private closed = false;
  private nextId = 1;
  private pending = new Map<number, { resolve: (v: any) => void; timer: number }>();
  onJson: JsonHandler = () => {};
  onBin: BinHandler = () => {};
  onStatus: (s: 'connecting' | 'open' | 'closed' | 'denied', detail?: string) => void = () => {};
  connId: string | null = null;
  rttMs = 0;
  serverOffsetMs = 0;

  constructor(url: string, token: () => string | null) {
    this.url = url;
    this.token = token;
  }

  get open(): boolean {
    return this.ws?.readyState === WebSocket.OPEN && this.connId !== null;
  }

  connect(): void {
    this.closed = false;
    this.onStatus('connecting');
    const ws = new WebSocket(this.url);
    ws.binaryType = 'arraybuffer';
    this.ws = ws;
    ws.onopen = () => {
      ws.send(JSON.stringify({ t: 'auth', token: this.token() }));
    };
    ws.onmessage = (ev) => {
      if (typeof ev.data === 'string') {
        let msg: any;
        try {
          msg = JSON.parse(ev.data);
        } catch {
          return;
        }
        if (msg.t === 'welcome') {
          this.connId = msg.connId;
          this.backoff = 500;
          this.onStatus('open');
        } else if (msg.t === 'auth_error') {
          this.closed = true;
          this.onStatus('denied', msg.error);
        } else if (msg.t === 'ack' && this.pending.has(msg.id)) {
          const p = this.pending.get(msg.id)!;
          clearTimeout(p.timer);
          this.pending.delete(msg.id);
          p.resolve(msg);
        } else if (msg.t === 'pong') {
          const now = performance.now();
          this.rttMs = now - msg.c;
          this.serverOffsetMs = msg.s - (performance.timeOrigin + now - this.rttMs / 2);
        }
        this.onJson(msg);
      } else {
        this.onBin(new Uint8Array(ev.data as ArrayBuffer));
      }
    };
    ws.onclose = (ev) => {
      this.connId = null;
      for (const p of this.pending.values()) {
        clearTimeout(p.timer);
        p.resolve({ ok: false, error: 'Connection lost.' });
      }
      this.pending.clear();
      if (this.closed || ev.code === 4003 || ev.code === 4004) {
        this.onStatus(ev.code === 4003 || ev.code === 4004 ? 'denied' : 'closed', ev.reason);
        return;
      }
      this.onStatus('closed', ev.reason);
      setTimeout(() => { if (!this.closed) this.connect(); }, this.backoff);
      this.backoff = Math.min(this.backoff * 2, 8000);
    };
  }

  send(obj: unknown): void {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(obj));
  }

  sendBin(buf: ArrayBuffer): void {
    if (this.ws?.readyState === WebSocket.OPEN && this.connId) this.ws.send(buf);
  }

  // Command with acknowledgement: resolves {ok, error?, code?}.
  cmd(op: string, args: Record<string, unknown> = {}): Promise<{ ok: boolean; error?: string; code?: string }> {
    const id = this.nextId++;
    return new Promise((resolve) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(id);
        resolve({ ok: false, error: 'No response from the server.' });
      }, 15000);
      this.pending.set(id, { resolve, timer });
      this.send({ t: 'cmd', id, op, args });
    });
  }

  request(t: string, body: Record<string, unknown> = {}): Promise<any> {
    const id = this.nextId++;
    return new Promise((resolve) => {
      const timer = window.setTimeout(() => {
        this.pending.delete(id);
        resolve({ ok: false, error: 'No response from the server.' });
      }, 20000);
      this.pending.set(id, { resolve, timer });
      this.send({ t, id, ...body });
    });
  }

  ping(): void {
    this.send({ t: 'ping', c: performance.now() });
  }

  close(): void {
    this.closed = true;
    this.ws?.close();
  }
}
