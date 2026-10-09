// Small leveled logger. Lines are capped in length, secrets that look like
// Telegram bot tokens or bearer tokens are redacted, and nothing here ever
// logs ROM contents or message text.

type Level = 'debug' | 'info' | 'warn' | 'error';
const ORDER: Record<Level, number> = { debug: 10, info: 20, warn: 30, error: 40 };

let threshold: number = ORDER[(process.env.LOG_LEVEL as Level) ?? 'info'] ?? ORDER.info;
const MAX_LINE = 600;

const REDACT: RegExp[] = [
  /\b\d{6,12}:[A-Za-z0-9_-]{30,}\b/g,           // bot tokens
  /(token|secret|hash|initData|authorization)=([^&\s"]+)/gi,
  /\b(Bearer)\s+[A-Za-z0-9._~+/=-]+/g,
];

export function redact(s: string): string {
  let out = s;
  for (const r of REDACT) out = out.replace(r, (m, a) => (typeof a === 'string' && m.includes('=') ? `${a}=[redacted]` : '[redacted]'));
  return out;
}

function write(level: Level, scope: string, msg: string, extra?: Record<string, unknown>): void {
  if (ORDER[level] < threshold) return;
  let line = `${new Date().toISOString()} ${level.toUpperCase()} [${scope}] ${msg}`;
  if (extra) {
    try {
      line += ' ' + JSON.stringify(extra);
    } catch {
      /* ignore unserializable extras */
    }
  }
  line = redact(line);
  if (line.length > MAX_LINE) line = line.slice(0, MAX_LINE) + '…';
  (level === 'error' || level === 'warn' ? process.stderr : process.stdout).write(line + '\n');
}

export const log = {
  setLevel(l: Level) { threshold = ORDER[l]; },
  debug: (scope: string, msg: string, extra?: Record<string, unknown>) => write('debug', scope, msg, extra),
  info: (scope: string, msg: string, extra?: Record<string, unknown>) => write('info', scope, msg, extra),
  warn: (scope: string, msg: string, extra?: Record<string, unknown>) => write('warn', scope, msg, extra),
  error: (scope: string, msg: string, extra?: Record<string, unknown>) => write('error', scope, msg, extra),
};
