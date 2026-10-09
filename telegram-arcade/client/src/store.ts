// Tiny observable app state (no framework dependency beyond Preact hooks).

import { useEffect, useState } from 'preact/hooks';
import type { RunnerStats, SessionInfo } from './emu/runner.ts';

export interface ChatMsg {
  id: number;
  userId: number;
  name: string;
  text: string;
  at: number;
  frame: number | null;
}

export interface RoomEvent {
  kind: string;
  text: string;
  frame: number | null;
  at: number;
}

export interface AppState {
  conn: 'connecting' | 'open' | 'closed' | 'denied';
  connDetail: string;
  me: { id: number; name: string } | null;
  group: { id: number; title: string } | null;
  tgRole: string;
  room: any | null;
  you: any | null;
  session: SessionInfo | null;
  stats: RunnerStats | null;
  chat: ChatMsg[];
  unread: number;
  events: RoomEvent[];
  notice: string | null;
  tab: 'cabinet' | 'shelf' | 'vote' | 'records' | 'settings' | 'host';
  chatOpen: boolean;
  optIn: { gameId: number; title: string; closesAt: number } | null;
  error: string | null;
}

type Listener = () => void;

class Store {
  state: AppState = {
    conn: 'connecting', connDetail: '', me: null, group: null, tgRole: 'member', room: null, you: null, session: null, stats: null,
    chat: [], unread: 0, events: [], notice: null, tab: 'cabinet', chatOpen: false, optIn: null, error: null,
  };
  private listeners = new Set<Listener>();

  set(patch: Partial<AppState>): void {
    this.state = { ...this.state, ...patch };
    for (const l of this.listeners) l();
  }

  subscribe(l: Listener): () => void {
    this.listeners.add(l);
    return () => this.listeners.delete(l);
  }
}

export const store = new Store();

export function useStore(): AppState {
  const [, force] = useState(0);
  useEffect(() => store.subscribe(() => force((n) => n + 1)), []);
  return store.state;
}
