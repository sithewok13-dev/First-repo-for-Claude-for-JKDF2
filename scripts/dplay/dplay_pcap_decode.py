#!/usr/bin/env python3
# Summarizes a DirectPlay (TCP/IP) capture: each system message by command, and the game's own
# messages by DSS message id, in each direction. Reads a pcap file (tcpdump -w, Ethernet, Linux
# "any" or loopback), IPv4 only; TCP streams are reassembled naively (in capture order).
#
#   tcpdump -i any -w dp.pcap 'portrange 2300-2400 or port 47624'
#   python3 dplay_pcap_decode.py dp.pcap [-v]
import struct, sys, collections

CMDS = {1: 'ENUMSESSIONSREPLY', 2: 'ENUMSESSIONS', 5: 'REQUESTPLAYERID', 7: 'REQUESTPLAYERREPLY',
        8: 'CREATEPLAYER', 11: 'DELETEPLAYER', 19: 'ADDFORWARDREQUEST', 22: 'PING', 23: 'PINGREPLY',
        36: 'ADDFORWARDREPLY', 41: 'SUPERENUMPLAYERSREPLY', 46: 'ADDFORWARD', 47: 'ADDFORWARDACK'}
MAGIC = 0x79616c70

def packets(path):
    d = open(path, 'rb').read()
    magic, = struct.unpack_from('<I', d, 0)
    e = '<' if magic == 0xa1b2c3d4 else '>'
    link, = struct.unpack_from(e + 'I', d, 20)
    off = 24
    while off + 16 <= len(d):
        _, _, cl, _ = struct.unpack_from(e + 'IIII', d, off)
        pkt = d[off + 16: off + 16 + cl]
        off += 16 + cl
        hdr = {1: 14, 113: 16, 276: 20, 0: 4}.get(link, 14)
        ip = pkt[hdr:]
        if len(ip) < 20 or ip[0] >> 4 != 4: continue
        ihl = (ip[0] & 15) * 4
        src, dst = ip[12:16], ip[16:20]
        proto = ip[9]
        if proto == 17:
            sp, dp = struct.unpack_from('>HH', ip, ihl)
            yield 'udp', (src, sp), (dst, dp), ip[ihl + 8:]
        elif proto == 6:
            sp, dp = struct.unpack_from('>HH', ip, ihl)
            doff = (ip[ihl + 12] >> 4) * 4
            yield 'tcp', (src, sp), (dst, dp), ip[ihl + doff:]

def name(a):
    return '%d.%d.%d.%d:%d' % (*a[0], a[1])

def main():
    verbose = '-v' in sys.argv
    counts = collections.Counter()
    streams = collections.defaultdict(bytearray)
    def system(direction, m, via):
        cmd, ver = struct.unpack_from('<HH', m, 4)
        counts[(direction, '%s %s v%d' % (via, CMDS.get(cmd, 'cmd %d' % cmd), ver))] += 1
    def game(direction, m, via):
        if len(m) < 12: return
        frm, to, dss = struct.unpack_from('<IIH', m, 0)
        counts[(direction, '%s game DSS %d' % (via, dss))] += 1
        if verbose: print(direction, via, 'from %#x to %#x DSS %d: %s' % (frm, to, dss, m[8:40].hex()))
    for kind, s, t, pl in packets(sys.argv[1]):
        direction = '%s -> %s' % (name(s), name(t))
        if not pl: continue
        if kind == 'udp':
            if len(pl) >= 28 and struct.unpack_from('<I', pl, 20)[0] == MAGIC: system(direction, pl[20:], 'udp')
            elif len(pl) >= 8 and struct.unpack_from('<I', pl, 0)[0] == MAGIC: system(direction, pl, 'udp')
            else: game(direction, pl, 'udp')
            continue
        buf = streams[(s, t)]
        buf += pl
        while len(buf) >= 20:
            size = struct.unpack_from('<I', buf, 0)[0] & 0xFFFFF
            if size < 20 or len(buf) < size:
                if size < 20: buf.clear()
                break
            m = bytes(buf[:size]); del buf[:size]
            if len(m) >= 28 and struct.unpack_from('<I', m, 20)[0] == MAGIC: system(direction, m[20:], 'tcp')
            else: game(direction, m[20:], 'tcp')
    for (direction, what), n in sorted(counts.items()):
        print('%6d  %-45s %s' % (n, direction, what))

if __name__ == '__main__':
    main()
