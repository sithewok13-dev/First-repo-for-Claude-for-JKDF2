// Room text chat (players and spectators). Plain text only: messages are
// rendered as text nodes, never as HTML. Typing here never reaches the game.

import { useEffect, useRef, useState } from 'preact/hooks';
import { cmd, input, net } from '../controller.ts';
import { store, useStore } from '../store.ts';

export function Chat() {
  const st = useStore();
  const [text, setText] = useState('');
  const list = useRef<HTMLDivElement>(null);
  const open = st.chatOpen;
  useEffect(() => {
    if (list.current) list.current.scrollTop = list.current.scrollHeight;
  }, [st.chat.length, open]);
  useEffect(() => {
    if (open && st.unread) store.set({ unread: 0 });
  }, [open, st.unread]);
  const roleOf = (userId: number) => {
    const m = st.room?.members?.find((x: any) => x.userId === userId);
    if (!m) return '';
    if (m.host) return 'host';
    if (m.deputy) return 'deputy';
    return m.role;
  };
  const send = async () => {
    const t = text.trim();
    if (!t) return;
    const r = await net.request('chat', { text: t });
    if (r.ok) setText('');
    else store.set({ notice: r.error ?? 'Message not sent.' });
  };
  const canMod = st.you?.roles?.canModerate;
  return (
    <aside class={`chat ${open ? 'open' : 'closed'}`}>
      <button class="chat-toggle" onClick={() => store.set({ chatOpen: !open })}>
        💬 Chat{!open && st.unread > 0 ? <span class="unread">{st.unread}</span> : null}
      </button>
      {open && (
        <>
          <div class="chat-list" ref={list}>
            {st.chat.map((m) => (
              <div key={m.id} class="chat-msg">
                <span class={`who role-${roleOf(m.userId)}`}>{m.name}</span>
                <span class="text">{m.text}</span>
                {canMod && m.userId !== st.me?.id && (
                  <button class="tiny" title="Delete" onClick={() => void cmd('mod.chat_delete', { id: m.id, reason: 'moderation' })}>×</button>
                )}
              </div>
            ))}
            {st.chat.length === 0 && <div class="muted">No messages yet. Say hi!</div>}
          </div>
          <form class="chat-form" onSubmit={(e) => { e.preventDefault(); void send(); }}>
            <input value={text} maxLength={500} placeholder={st.you?.muted ? 'You are muted' : 'Message the room'}
              disabled={st.you?.muted}
              onFocus={() => { input.setEnabled(false); net.send({ t: 'blur' }); }}
              onBlur={() => input.setEnabled(true)}
              onInput={(e) => setText((e.target as HTMLInputElement).value)} />
            <button class="btn" type="submit">Send</button>
          </form>
        </>
      )}
    </aside>
  );
}
