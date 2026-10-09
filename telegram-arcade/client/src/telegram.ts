// Telegram Mini App integration. Every call is feature-checked against the
// client's Bot API version (isVersionAtLeast): older clients simply skip it.

export interface TgWebApp {
  initData: string;
  version: string;
  platform: string;
  isVersionAtLeast(v: string): boolean;
  ready(): void;
  expand(): void;
  close(): void;
  disableVerticalSwipes?(): void;
  enableClosingConfirmation?(): void;
  disableClosingConfirmation?(): void;
  requestFullscreen?(): void;
  exitFullscreen?(): void;
  isFullscreen?: boolean;
  lockOrientation?(): void;
  unlockOrientation?(): void;
  openLink(url: string, opts?: { try_instant_view?: boolean }): void;
  onEvent(name: string, cb: (...a: any[]) => void): void;
  offEvent(name: string, cb: (...a: any[]) => void): void;
  setHeaderColor?(c: string): void;
  setBackgroundColor?(c: string): void;
  HapticFeedback?: { impactOccurred(s: string): void; notificationOccurred(s: string): void; selectionChanged(): void };
  safeAreaInset?: { top: number; bottom: number; left: number; right: number };
  contentSafeAreaInset?: { top: number; bottom: number; left: number; right: number };
}

export function tg(): TgWebApp | null {
  const w = (window as any).Telegram?.WebApp as TgWebApp | undefined;
  return w && typeof w.initData === 'string' && w.initData.length > 0 ? w : null;
}

function at(v: string): boolean {
  const w = tg();
  return !!w && w.isVersionAtLeast(v);
}

export function initTelegram(): void {
  const w = tg();
  if (!w) return;
  try {
    w.ready();
    w.expand();
    // Bot API 7.7: stop vertical swipes from closing the panel mid-game.
    if (at('7.7')) w.disableVerticalSwipes?.();
    if (at('6.1')) {
      w.setHeaderColor?.('#14101f');
      w.setBackgroundColor?.('#14101f');
    }
  } catch { /* never block the app on cosmetic calls */ }
  const apply = () => {
    const root = document.documentElement.style;
    const s = w.safeAreaInset, c = w.contentSafeAreaInset;
    root.setProperty('--safe-top', `${(s?.top ?? 0) + (c?.top ?? 0)}px`);
    root.setProperty('--safe-bottom', `${(s?.bottom ?? 0) + (c?.bottom ?? 0)}px`);
    root.setProperty('--safe-left', `${(s?.left ?? 0) + (c?.left ?? 0)}px`);
    root.setProperty('--safe-right', `${(s?.right ?? 0) + (c?.right ?? 0)}px`);
  };
  if (at('8.0')) {
    apply();
    w.onEvent('safeAreaChanged', apply);
    w.onEvent('contentSafeAreaChanged', apply);
  }
}

export function canFullscreen(): boolean {
  return at('8.0') ? true : !!document.documentElement.requestFullscreen;
}

export function toggleFullscreen(): void {
  const w = tg();
  if (w && at('8.0')) {
    if (w.isFullscreen) w.exitFullscreen?.();
    else w.requestFullscreen?.();
    return;
  }
  if (document.fullscreenElement) void document.exitFullscreen();
  else void document.documentElement.requestFullscreen?.().catch(() => {});
}

export function setClosingConfirmation(on: boolean): void {
  const w = tg();
  if (!w || !at('6.2')) return;
  if (on) w.enableClosingConfirmation?.();
  else w.disableClosingConfirmation?.();
}

// Telegram's lockOrientation (8.0+) keeps the CURRENT orientation; the web
// screen.orientation.lock() is disabled in Telegram's mobile webviews.
export function canLockOrientation(): boolean {
  return at('8.0');
}

export function lockLandscape(on: boolean): void {
  const w = tg();
  if (w && at('8.0')) {
    if (on) w.lockOrientation?.();
    else w.unlockOrientation?.();
    return;
  }
  const o = (screen as any).orientation;
  if (on) o?.lock?.('landscape').catch(() => {});
  else o?.unlock?.();
}

export function haptic(kind: 'light' | 'success' | 'warning' = 'light'): void {
  const h = tg()?.HapticFeedback;
  if (!h || !at('6.1')) return;
  if (kind === 'light') h.impactOccurred('light');
  else h.notificationOccurred(kind);
}

// Bot API 8.0: activated/deactivated when the Mini App is minimized/restored.
export function onActiveChange(cb: (active: boolean) => void): void {
  const w = tg();
  if (w && at('8.0')) {
    w.onEvent('activated', () => cb(true));
    w.onEvent('deactivated', () => cb(false));
  }
  document.addEventListener('visibilitychange', () => cb(document.visibilityState === 'visible'));
  window.addEventListener('blur', () => cb(false));
  window.addEventListener('focus', () => cb(true));
}

export function openExternal(url: string): void {
  const w = tg();
  if (w) w.openLink(url);
  else window.open(url, '_blank', 'noopener');
}
