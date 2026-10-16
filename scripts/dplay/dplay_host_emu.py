#!/usr/bin/env python3
# A minimal DirectPlay 4 host (the TCP/IP service provider's messages), for testing a join by the
# original Jedi Knight (under Wine) or by this port's DirectPlay transport
# (src/Platform/Networking/DPlay). It answers ENUMSESSIONS (UDP 47624) with a session described as
# the original game describes its own, then REQUESTPLAYERID, ADDFORWARDREQUEST (with
# SUPERENUMPLAYERSREPLY) and PING, and logs everything, the game's messages decoded as the game's
# DSS messages (u16 message id, u16 sequence number, then the message). It plays no game: a joining
# player gets as far as sending its join request. Wire format: Microsoft's [MS-DPDX] and [MS-DPSP],
# and Wine's dplayx tests.
#
#   python3 dplay_host_emu.py --episode JK1MP --map m2.jkl [--log file]
import socket, struct, select, sys, time, uuid, argparse

MAGIC = 0x79616c70  # 'play'
CMDS = {1:'ENUMSESSIONSREPLY',2:'ENUMSESSIONS',3:'ENUMPLAYERSREPLY',5:'REQUESTPLAYERID',7:'REQUESTPLAYERREPLY',
        8:'CREATEPLAYER',9:'CREATEGROUP',10:'PLAYERMESSAGE',11:'DELETEPLAYER',12:'DELETEGROUP',13:'ADDPLAYERTOGROUP',
        14:'DELETEPLAYERFROMGROUP',15:'PLAYERDATACHANGED',16:'PLAYERNAMECHANGED',17:'GROUPDATACHANGED',
        19:'ADDFORWARDREQUEST',21:'PACKET',22:'PING',23:'PINGREPLY',24:'YOUAREDEAD',
        36:'ADDFORWARDREPLY',41:'SUPERENUMPLAYERSREPLY',46:'ADDFORWARD',47:'ADDFORWARDACK',48:'PACKET2_DATA',
        49:'PACKET2_ACK',53:'IAMNAMESERVER',56:'CREATEPLAYERVERIFY'}
DSS = {1:'THINGPOS',2:'CHAT',4:'FIREPROJECTILE',5:'DEATH',6:'DAMAGE',11:'SYNCTHING',12:'THINGFULLDESC',13:'SYNCCOG',
       32:'LEAVEJOIN',33:'WELCOME',34:'JOINREQUEST',35:'DESTROYTHING',36:'JOINING',38:'PING',39:'PINGREPLY',
       40:'RESET',41:'ENUMPLAYERS',42:'QUIT'}

JK_APP_GUID = uuid.UUID('bf0613c0-de79-11d0-99c9-00a02476ad4b')
HOST_SYS_ID = 0x00051573
HOST_PLAYER_ID = 0x00001337
VERSION = 14
log_f = None

def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a)
    print(s, flush=True)
    if log_f: log_f.write(s + '\n'); log_f.flush()

def sockaddr(port, ip=0):
    return struct.pack('<H', socket.AF_INET) + struct.pack('>HI', port, ip) + b'\0' * 8

def sp_header(size, port, token=0xfab):
    return struct.pack('<I', (token << 20) | size) + sockaddr(port)

def msg_header(cmd):
    return struct.pack('<IHH', MAGIC, cmd, VERSION)

def wstr(s):
    return s.encode('utf-16le') + b'\0\0' if s is not None else b''

def sessiondesc(a):
    # DPSESSIONDESC2: dwSize, dwFlags, guidInstance, guidApplication, dwMaxPlayers, dwCurrentPlayers,
    # lpszSessionName, lpszPassword, dwReserved1, dwReserved2, dwUser1..4
    return struct.pack('<II16s16sIIIIIIIIII', 80, a.flags, a.instance.bytes_le, JK_APP_GUID.bytes_le,
                       a.maxplayers, a.curplayers, 0, 0, a.reserved1, 0,
                       a.seed, a.modeflags, (a.maxrank << 16) | (a.field_e0 & 0xffff), a.tickrate)

class Host:
    def __init__(s, a):
        s.a = a
        s.udp_enum = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.udp_enum.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.udp_enum.bind(('0.0.0.0', 47624))
        s.tcp_listen = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.tcp_listen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.tcp_listen.bind(('0.0.0.0', a.tcpport)); s.tcp_listen.listen(8)
        s.udp_game = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.udp_game.bind(('0.0.0.0', a.udpport))
        s.inconns = {}   # sock -> bytearray
        s.out = {}       # (ip, port) -> sock
        s.next_id = 0x00010001
        s.clients = {}   # player id -> dict(ip, tcp, udp)
        s.name = '%s:%s:%s' % (a.name, a.episode, a.map)
        log('host: session %r tcp %d udp %d' % (s.name, a.tcpport, a.udpport))

    def send_tcp(s, ip, port, payload):
        key = (ip, port)
        sk = s.out.get(key)
        if sk is None:
            sk = socket.create_connection(key, timeout=3)
            s.out[key] = sk
            log('  -> connected out to %s:%d' % key)
        sk.sendall(payload)

    def enum_reply(s, ip, port):
        body = msg_header(1) + sessiondesc(s.a) + struct.pack('<I', 8 + 80 + 4) + wstr(s.name)
        s.send_tcp(ip, port, sp_header(20 + len(body), s.a.tcpport) + body)
        log('  -> ENUMSESSIONSREPLY to %s:%d' % (ip, port))

    def superenum(s, ip, port, client_sys, cl):
        def spdata(tcp, udp): return sockaddr(tcp) + sockaddr(udp)
        players = b''
        # client's system player
        players += struct.pack('<IIIII', 16, 0x5, client_sys, 0x4, VERSION) + bytes([32]) + spdata(cl['tcp'], cl['udp'])
        # host system player (name server)
        players += struct.pack('<IIIII', 16, 0xf, HOST_SYS_ID, 0x4, VERSION) + bytes([32]) + spdata(s.a.tcpport, s.a.udpport)
        # host app player
        players += (struct.pack('<IIIII', 16, 0x8, HOST_PLAYER_ID, 0x7, HOST_SYS_ID) + wstr(s.a.player)
                    + wstr(s.a.player) + bytes([32]) + spdata(s.a.tcpport, s.a.udpport))
        nplayers = 3
        name = wstr(s.name)
        hdr_len = 8 + 7 * 4
        body = (msg_header(41) + struct.pack('<IIIIIII', nplayers, 0, hdr_len + 80 + len(name), 0, hdr_len,
                                             hdr_len + 80, 0) + sessiondesc(s.a) + name + players)
        s.send_tcp(ip, port, sp_header(20 + len(body), s.a.tcpport) + body)
        log('  -> SUPERENUMPLAYERSREPLY (%d players) to %s:%d' % (nplayers, ip, port))

    def game(s, src, data, via):
        frm, to = struct.unpack_from('<II', data, 0)
        p = data[8:]
        if len(p) >= 4:
            cid, mid = struct.unpack_from('<HH', p, 0)
            log('  GAME %s from %#x to %#x: DSS %d %s msgId %d len %d: %s' % (via, frm, to, cid, DSS.get(cid, '?'),
                mid, len(p) - 4, p[4:4 + 96].hex() + (' ... tail ' + p[-8:].hex() if len(p) > 100 else '')))
        else:
            log('  GAME %s from %#x to %#x: %s' % (via, frm, to, p.hex()))

    def handle(s, ip, msg):
        mixed, = struct.unpack_from('<I', msg, 0)
        fam, port = struct.unpack_from('<H', msg, 4)[0], struct.unpack_from('>H', msg, 6)[0]
        body = msg[20:]
        if len(body) >= 8 and struct.unpack_from('<I', body, 0)[0] == MAGIC:
            cmd, ver = struct.unpack_from('<HH', body, 4)
            log('<- %s (%d) v%d from %s port %d, %d bytes: %s' % (CMDS.get(cmd, '?'), cmd, ver, ip, port, len(msg),
                                                                body[8:8 + 64].hex()))
            if cmd == 5:   # REQUESTPLAYERID
                flags, = struct.unpack_from('<I', body, 8)
                pid = s.next_id; s.next_id += 1
                reply = msg_header(7) + struct.pack('<I', pid) + b'\0' * 24 + struct.pack('<III', 0, 0, 0) + b'\0' * 64
                s.send_tcp(ip, port, sp_header(20 + len(reply), s.a.tcpport) + reply)
                log('  -> REQUESTPLAYERREPLY id %#x (flags %#x)' % (pid, flags))
            elif cmd == 19:  # ADDFORWARDREQUEST
                toId, pid, gid, co, po = struct.unpack_from('<IIIII', body, 8)
                pp = body[28:28 + 48]
                spd = body[28 + 48:28 + 48 + 32]
                tcp = struct.unpack_from('>H', spd, 2)[0]; udp = struct.unpack_from('>H', spd, 18)[0]
                cl = {'ip': ip, 'tcp': tcp, 'udp': udp}
                s.clients[pid] = cl
                log('  client sys player %#x tcp %d udp %d' % (pid, tcp, udp))
                s.superenum(ip, port, pid, cl)
            elif cmd == 8:  # CREATEPLAYER
                toId, pid, gid, co, po = struct.unpack_from('<IIIII', body, 8)
                size, flags, id_, sl, ll, sps, pds = struct.unpack_from('<7I', body, 28)
                off = 28 + 48
                sn = body[off:off + sl].decode('utf-16le', 'replace').rstrip('\0'); off += sl
                log('  client app player %#x flags %#x name %r' % (pid, flags, sn))
                for c in s.clients.values():
                    if c['ip'] == ip: c['app'] = pid
            elif cmd == 22:  # PING
                frm, tick = struct.unpack_from('<II', body, 8)
                reply = msg_header(23) + struct.pack('<II', HOST_SYS_ID, tick)
                s.send_tcp(ip, port, sp_header(20 + len(reply), s.a.tcpport) + reply)
        else:
            s.game((ip, port), body, 'tcp')

    def run(s, secs):
        end = time.time() + secs
        while time.time() < end:
            rl = [s.udp_enum, s.tcp_listen, s.udp_game] + list(s.inconns)
            r, _, _ = select.select(rl, [], [], 0.5)
            for sk in r:
                if sk is s.udp_enum:
                    d, (ip, _) = sk.recvfrom(4096)
                    port = struct.unpack_from('>H', d, 6)[0]
                    body = d[20:]
                    cmd, = struct.unpack_from('<H', body, 4)
                    guid = uuid.UUID(bytes_le=bytes(body[8:24]))
                    pwo, flags = struct.unpack_from('<II', body, 24)
                    log('<- UDP ENUMSESSIONS from %s port %d app %s flags %#x pwoff %d' % (ip, port, guid, flags, pwo))
                    if guid == JK_APP_GUID or guid.int == 0:
                        s.enum_reply(ip, port)
                elif sk is s.tcp_listen:
                    c, addr = sk.accept()
                    s.inconns[c] = bytearray()
                    log('accepted in-conn from %s:%d' % addr)
                elif sk is s.udp_game:
                    d, addr = sk.recvfrom(4096)
                    s.game(addr, d, 'udp')
                else:
                    d = sk.recv(65536)
                    if not d:
                        log('in-conn closed'); del s.inconns[sk]; continue
                    buf = s.inconns[sk]; buf += d
                    while len(buf) >= 4:
                        size = struct.unpack_from('<I', buf, 0)[0] & 0xfffff
                        if len(buf) < size: break
                        msg = bytes(buf[:size]); del buf[:size]
                        s.handle(sk.getpeername()[0], msg)

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--name', default='EmuHost'); ap.add_argument('--episode', default='JK1MP.GOB')
    ap.add_argument('--map', default='m2.jkl'); ap.add_argument('--player', default='EmuPlayer')
    ap.add_argument('--tcpport', type=int, default=2349); ap.add_argument('--udpport', type=int, default=2399)
    ap.add_argument('--maxplayers', type=int, default=4); ap.add_argument('--curplayers', type=int, default=1)
    ap.add_argument('--flags', type=lambda x: int(x, 0), default=0xc0)
    ap.add_argument('--seed', type=lambda x: int(x, 0), default=0x12345678)
    ap.add_argument('--modeflags', type=lambda x: int(x, 0), default=0x10)
    ap.add_argument('--maxrank', type=int, default=4); ap.add_argument('--field_e0', type=int, default=10)
    ap.add_argument('--tickrate', type=int, default=180); ap.add_argument('--reserved1', type=lambda x: int(x, 0), default=0xaabbccdd)
    ap.add_argument('--secs', type=int, default=600); ap.add_argument('--log')
    a = ap.parse_args(); a.instance = uuid.uuid4()
    if a.log: log_f = open(a.log, 'w')
    Host(a).run(a.secs)
