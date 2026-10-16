#!/usr/bin/env python3
# A minimal DirectPlay 4 host (the TCP/IP service provider's messages), for testing a join by the
# original Jedi Knight (under Wine) or by this port's DirectPlay transport
# (src/Platform/Networking/DPlay). It plays no game: a player who joins gets as far as sending the
# game's join request; what the game sends is logged, decoded as the game's DSS messages (u16
# message id, u16 sequence number, then the message). Wire format: Microsoft's [MS-DPDX] and
# [MS-DPSP], and Wine's dplayx tests.
#
# It can stand in for what a real game night has:
#   --session NAME:EPISODE:MAP[@IP]   (again for more) several sessions answering one ENUMSESSIONS,
#                                     as a computer hosting several games does; one with @IP
#                                     answers from that address (127.0.0.2 ... on Linux)
#   --fake N                          N other players already in the (first) session; each sends
#                                     the game's DSS_PING to whoever joins, every 3 s, from its own
#                                     UDP port (so what is sent back to it is seen)
#   --join-at S / --leave-at S        S s after a player joins: another one joins (ADDFORWARD,
#                                     CREATEPLAYER), the first fake one leaves (DELETEPLAYER)
#   --die-at S                        S s after a player joins: no answer to anything any more (the
#                                     connections kept open), as a host whose computer froze
#
#   python3 dplay_host_emu.py --session "Game A:JK1MP:m2.jkl" --fake 2 --log emu.log
import socket, struct, select, sys, time, uuid, argparse

MAGIC = 0x79616c70  # 'play'
CMDS = {1: 'ENUMSESSIONSREPLY', 2: 'ENUMSESSIONS', 5: 'REQUESTPLAYERID', 7: 'REQUESTPLAYERREPLY',
        8: 'CREATEPLAYER', 11: 'DELETEPLAYER', 19: 'ADDFORWARDREQUEST', 22: 'PING', 23: 'PINGREPLY',
        36: 'ADDFORWARDREPLY', 41: 'SUPERENUMPLAYERSREPLY', 46: 'ADDFORWARD', 47: 'ADDFORWARDACK'}
DSS = {1: 'THINGPOS', 2: 'CHAT', 11: 'SYNCTHING', 32: 'LEAVEJOIN', 33: 'WELCOME', 34: 'JOINREQUEST', 36: 'JOINING',
       38: 'PING', 39: 'PINGREPLY', 40: 'RESET', 41: 'ENUMPLAYERS', 42: 'QUIT'}
JK_APP_GUID = uuid.UUID('bf0613c0-de79-11d0-99c9-00a02476ad4b')
VERSION = 14
log_f = None
T0 = time.time()

def log(*a):
    s = '%7.2f ' % (time.time() - T0) + ' '.join(str(x) for x in a)
    print(s, flush=True)
    if log_f: log_f.write(s + '\n'); log_f.flush()

def sockaddr(port, ip='0.0.0.0'):
    return struct.pack('<H', socket.AF_INET) + struct.pack('>H', port) + socket.inet_aton(ip) + b'\0' * 8

def sp_header(size, port):
    return struct.pack('<I', (0xfab << 20) | size) + sockaddr(port)

def msg_header(cmd):
    return struct.pack('<IHH', MAGIC, cmd, VERSION)

def wstr(s):
    return s.encode('utf-16le') + b'\0\0'

def packed_player(flags, pid, sysid, name, tcp, udp, ip):
    sn = wstr(name) if name else b''
    return (struct.pack('<12I', 48 + len(sn) + 32, flags, pid, len(sn), 0, 32, 0, 0, sysid, 48, VERSION, 0)
            + sn + sockaddr(tcp, ip) + sockaddr(udp, ip))

class Fake:
    def __init__(f, ses, k, name):
        f.sys = ses.host.next_id(); f.app = ses.host.next_id(); f.name = name
        f.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        f.udp.bind((ses.ip, 0)); f.udpport = f.udp.getsockname()[1]
        f.ses = ses; f.last = 0

class Session:
    def __init__(s, host, spec, k):
        s.host = host
        spec, _, ip = spec.partition('@')
        s.ip = ip or '127.0.0.1'
        parts = spec.split(':')
        s.name = '%s:%s:%s' % (parts[0], parts[1] if len(parts) > 1 else 'JK1MP', parts[2] if len(parts) > 2 else 'm2.jkl')
        s.guid = uuid.uuid4()
        s.tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.tcp.bind((s.ip, 2349 - k)); s.tcp.listen(8); s.tcpport = 2349 - k
        s.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.udp.bind((s.ip, 2399 - k)); s.udpport = 2399 - k
        s.sys = host.next_id(); s.app = host.next_id()
        s.fakes = []
        s.out = {}
        s.client = None   # (ip, tcp, udp, sys, app)
        s.joined_at = None
        log('session %r at %s tcp %d udp %d' % (s.name, s.ip, s.tcpport, s.udpport))

    def dpsd(s):
        a = s.host.a
        n = 1 + len(s.fakes) + (1 if s.client and s.client.get('app') else 0)
        return struct.pack('<II16s16sIIIIIIIIII', 80, 0xc0, s.guid.bytes_le, JK_APP_GUID.bytes_le, a.maxplayers, n, 0, 0,
                           0xaabbccdd, 0, a.seed, a.modeflags, (4 << 16) | 10, 180)

    def send_tcp(s, ip, port, body):
        sk = s.out.get((ip, port))
        if sk is None:
            sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sk.bind((s.ip, 0)); sk.settimeout(3); sk.connect((ip, port))
            s.out[(ip, port)] = sk
            log('  [%s] connected out to %s:%d' % (s.ip, ip, port))
        try: sk.sendall(sp_header(20 + len(body), s.tcpport) + body)
        except OSError:
            s.out.pop((ip, port), None); sk.close()
            log('  [%s] connection to %s:%d lost: again' % (s.ip, ip, port))
            try:
                sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM); sk.bind((s.ip, 0)); sk.settimeout(3); sk.connect((ip, port))
                s.out[(ip, port)] = sk; sk.sendall(sp_header(20 + len(body), s.tcpport) + body)
            except OSError: s.out.pop((ip, port), None); log('  (not reached)')

    def enum_reply(s, ip, port):
        body = msg_header(1) + s.dpsd() + struct.pack('<I', 8 + 80 + 4) + wstr(s.name)
        s.send_tcp(ip, port, body)
        log('  -> ENUMSESSIONSREPLY %r to %s:%d' % (s.name, ip, port))

    def superenum(s, ip, port):
        c = s.client
        def sp(tcp, udp, ipx='0.0.0.0'): return bytes([32]) + sockaddr(tcp, ipx) + sockaddr(udp, ipx)
        pl = struct.pack('<IIIII', 16, 0x5, c['sys'], 0x4, VERSION) + sp(c['tcp'], c['udp'])
        pl += struct.pack('<IIIII', 16, 0xf, s.sys, 0x4, VERSION) + sp(s.tcpport, s.udpport)
        pl += struct.pack('<IIIII', 16, 0x8, s.app, 0x7, s.sys) + wstr('EmuHost') + wstr('EmuHost') + sp(s.tcpport, s.udpport)
        n = 3
        for f in s.fakes:
            pl += struct.pack('<IIIII', 16, 0x5, f.sys, 0x4, VERSION) + sp(s.tcpport, f.udpport, s.ip)
            pl += struct.pack('<IIIII', 16, 0x0, f.app, 0x7, f.sys) + wstr(f.name) + wstr(f.name) + sp(s.tcpport, f.udpport, s.ip)
            n += 2
        name = wstr(s.name)
        fixed = 8 + 28
        body = msg_header(41) + struct.pack('<7I', n, 0, fixed + 80 + len(name), 0, fixed, fixed + 80, 0) + s.dpsd() + name + pl
        s.send_tcp(ip, port, body)
        log('  -> SUPERENUMPLAYERSREPLY (%d players, %d of them others) to %s:%d' % (n, len(s.fakes), ip, port))

    def fake_join(s):
        f = Fake(s, len(s.fakes), 'Latecomer')
        s.fakes.append(f)
        c = s.client
        body = msg_header(46) + struct.pack('<5I', 0, f.sys, 0, 28, 0) + packed_player(0x5, f.sys, f.sys, None, s.tcpport, f.udpport, s.ip)
        s.send_tcp(c['ip'], c['tcp'], body)
        body = msg_header(8) + struct.pack('<5I', 0, f.app, 0, 28, 0) + packed_player(0x0, f.app, f.sys, f.name, s.tcpport, f.udpport, s.ip) + b'\0' * 6
        s.send_tcp(c['ip'], c['tcp'], body)
        log('  -> another player joined: sys %#x player %#x (UDP %d)' % (f.sys, f.app, f.udpport))

    def fake_leave(s):
        if not s.fakes: return
        f = s.fakes.pop(0)
        c = s.client
        for pid in (f.app, f.sys):
            s.send_tcp(c['ip'], c['tcp'], msg_header(11) + struct.pack('<5I', 0, pid, 0, 0, 0))
        log('  -> player %#x (%s) left' % (f.app, f.name))

class Host:
    def __init__(h, a):
        h.a = a; h.id = 0x00050000; h.dead = False
        h.enum = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        h.enum.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        h.enum.bind(('0.0.0.0', 47624))
        h.sessions = [Session(h, spec, k) for k, spec in enumerate(a.session or ['EmuHost:JK1MP:m2.jkl'])]
        for i in range(a.fake):
            h.sessions[0].fakes.append(Fake(h.sessions[0], i, 'Fake%d' % (i + 1)))
        h.inconns = {}

    def next_id(h):
        h.id += 0x101
        return h.id

    def game(h, where, data, via):
        frm, to = struct.unpack_from('<II', data, 0)
        p = data[8:]
        cid = struct.unpack_from('<H', p, 0)[0] if len(p) >= 2 else -1
        log('  GAME %s to %s: from %#x to %#x: DSS %d %s, %d bytes' % (via, where, frm, to, cid, DSS.get(cid, '?'), len(p)))

    def handle(h, ses, ip, msg):
        port = struct.unpack_from('>H', msg, 6)[0]
        body = msg[20:]
        if len(body) < 8 or struct.unpack_from('<I', body, 0)[0] != MAGIC:
            h.game('host %s' % ses.ip, body, 'tcp'); return
        cmd, ver = struct.unpack_from('<HH', body, 4)
        log('<- %s (%d) v%d from %s port %d' % (CMDS.get(cmd, '?'), cmd, ver, ip, port))
        if h.dead: return
        if cmd == 5:
            pid = h.next_id()
            ses.send_tcp(ip, port, msg_header(7) + struct.pack('<I', pid) + b'\0' * 24 + b'\0' * 12 + b'\0' * 64)
            log('  -> REQUESTPLAYERREPLY id %#x' % pid)
        elif cmd == 19:
            pid = struct.unpack_from('<I', body, 12)[0]
            spd = body[28 + 48:28 + 48 + 32]
            ses.client = {'ip': ip, 'tcp': struct.unpack_from('>H', spd, 2)[0], 'udp': struct.unpack_from('>H', spd, 18)[0], 'sys': pid}
            log('  joining: sys %#x, its ports TCP %d UDP %d (as it says)' % (pid, ses.client['tcp'], ses.client['udp']))
            ses.superenum(ip, port)
        elif cmd == 8:
            pid = struct.unpack_from('<I', body, 12)[0]
            if ses.client: ses.client['app'] = pid
            ses.joined_at = time.time()
            log('  joined: player %#x' % pid)
        elif cmd == 22:
            frm, tick = struct.unpack_from('<II', body, 8)
            ses.send_tcp(ip, port, msg_header(23) + struct.pack('<II', ses.sys, tick))

    def run(h, secs):
        end = time.time() + secs
        lastping = 0
        while time.time() < end:
            socks = [h.enum] + [x for s in h.sessions for x in [s.tcp, s.udp] + [f.udp for f in s.fakes]] + list(h.inconns)
            r, _, _ = select.select(socks, [], [], 0.2)
            for sk in r:
                if sk is h.enum:
                    d, (ip, _) = sk.recvfrom(4096)
                    port = struct.unpack_from('>H', d, 6)[0]
                    guid = uuid.UUID(bytes_le=bytes(d[28:44]))
                    log('<- ENUMSESSIONS from %s port %d' % (ip, port))
                    if not h.dead and guid in (JK_APP_GUID, uuid.UUID(int=0)):
                        for s in h.sessions: s.enum_reply(ip, port)
                    continue
                ses = next((s for s in h.sessions if sk is s.tcp), None)
                if ses:
                    c, addr = sk.accept(); h.inconns[c] = (ses, bytearray())
                    log('[%s] accepted from %s:%d' % (ses.ip, addr[0], addr[1])); continue
                ses = next((s for s in h.sessions if sk is s.udp), None)
                if ses:
                    d, addr = sk.recvfrom(4096); h.game('host %s' % ses.ip, d, 'udp'); continue
                fk = next((f for s in h.sessions for f in s.fakes if sk is f.udp), None)
                if fk:
                    d, addr = sk.recvfrom(4096); h.game('other player %s' % fk.name, d, 'udp'); continue
                ses, buf = h.inconns[sk]
                d = sk.recv(65536)
                if not d:
                    log('[%s] in-connection closed' % ses.ip); del h.inconns[sk]; continue
                buf += d
                while len(buf) >= 4:
                    size = struct.unpack_from('<I', buf, 0)[0] & 0xfffff
                    if len(buf) < size: break
                    m = bytes(buf[:size]); del buf[:size]
                    h.handle(ses, sk.getpeername()[0], m)
            now = time.time()
            for s in h.sessions:
                if not s.joined_at or h.dead: continue
                t = now - s.joined_at
                if h.a.die_at and t > h.a.die_at:
                    h.dead = True; log('-- the host stops answering'); break
                if h.a.join_at and t > h.a.join_at and not getattr(s, 'did_join', 0):
                    s.did_join = 1; s.fake_join()
                if h.a.leave_at and t > h.a.leave_at and not getattr(s, 'did_leave', 0):
                    s.did_leave = 1; s.fake_leave()
                c = s.client
                for f in s.fakes:
                    if now - f.last > 3 and c and c.get('app'):
                        f.last = now
                        f.udp.sendto(struct.pack('<IIHH', f.app, c['app'], 38, 0) + struct.pack('<I', int(now * 1000) & 0xffffffff), (c['ip'], c['udp']))
                if now - lastping > 5 and c:
                    lastping = now
                    s.send_tcp(c['ip'], c['tcp'], msg_header(22) + struct.pack('<II', s.sys, int(now * 1000) & 0xffffffff))

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--session', action='append')
    ap.add_argument('--fake', type=int, default=0)
    ap.add_argument('--join-at', type=float, default=0); ap.add_argument('--leave-at', type=float, default=0)
    ap.add_argument('--die-at', type=float, default=0)
    ap.add_argument('--maxplayers', type=int, default=8)
    ap.add_argument('--seed', type=lambda x: int(x, 0), default=0x12345678)
    ap.add_argument('--modeflags', type=lambda x: int(x, 0), default=0x10)
    ap.add_argument('--secs', type=int, default=600); ap.add_argument('--log')
    a = ap.parse_args()
    if a.log: log_f = open(a.log, 'w')
    Host(a).run(a.secs)
