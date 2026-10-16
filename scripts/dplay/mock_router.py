#!/usr/bin/env python3
# A pretend home router, for testing the DirectPlay transport's port forwarding
# (src/Platform/Networking/DPlay/stdComm_DPlayNat.c) on one computer: it answers PCP and NAT-PMP
# (UDP 5351) or UPnP-IGD (SSDP on UDP 1900, its description and SOAP control over HTTP), and really
# forwards what it maps: with PCP and NAT-PMP each port asked for is given as port + 1000 "outside"
# (with UPnP the port asked for, if free), and what arrives there (TCP connections, UDP datagrams)
# is passed on to the player's port. So a test host told
# the outside ports reaches the player only if the player told it the right ones.
#
#   python3 mock_router.py --mode pcp|natpmp|upnp|none [--ip 127.0.0.1] [--log file]
# and run the game with OPENJKDF2_DPLAY_NAT=force OPENJKDF2_DPLAY_GATEWAY=<ip> (PCP, NAT-PMP) or
# OPENJKDF2_DPLAY_SSDP=<ip> (UPnP).
import socket, struct, select, sys, time, argparse, threading, re

log_f = None
def log(*a):
    s = time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a)
    print(s, flush=True)
    if log_f: log_f.write(s + '\n'); log_f.flush()

class Router:
    def __init__(r, a):
        r.a = a; r.maps = {}  # (proto, ext) -> (fwd socket, internal ip, internal port)
        r.socks = []
        if a.mode in ('pcp', 'natpmp'):
            r.pmp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); r.pmp.bind((a.ip, 5351)); r.socks.append(r.pmp)
        if a.mode == 'upnp':
            r.ssdp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); r.ssdp.bind((a.ip, 1900)); r.socks.append(r.ssdp)
            r.http = socket.socket(socket.AF_INET, socket.SOCK_STREAM); r.http.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            r.http.bind((a.ip, 5000)); r.http.listen(4); r.socks.append(r.http)
        r.pipes = {}

    def add(r, proto, internal_ip, port, ext):
        key = (proto, ext)
        if key in r.maps: return ext
        if proto == 'TCP':
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(('0.0.0.0', ext)); s.listen(4)
        else:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('0.0.0.0', ext))
        r.maps[key] = (s, internal_ip, port)
        log('mapped %s outside %d -> %s:%d' % (proto, ext, internal_ip, port))
        return ext

    def delete(r, proto, ext):
        m = r.maps.pop((proto, ext), None)
        if m: m[0].close(); log('unmapped %s outside %d' % (proto, ext))

    def pcp(r, d, addr):
        if d[0] == 2 and r.a.mode == 'pcp' and len(d) >= 60 and d[1] == 1:
            life = struct.unpack_from('>I', d, 4)[0]
            nonce = d[24:36]; proto = d[36]; port = struct.unpack_from('>H', d, 40)[0]
            internal = socket.inet_ntoa(d[20:24]); pname = 'TCP' if proto == 6 else 'UDP'
            ext = port + 1000
            if life: r.add(pname, internal, port, ext)
            else: r.delete(pname, ext)
            resp = bytes([2, 0x81, 0, 0]) + struct.pack('>II', life, int(time.time())) + b'\0' * 12
            resp += nonce + bytes([proto, 0, 0, 0]) + struct.pack('>HH', port, ext) + b'\0' * 10 + b'\xff\xff' + socket.inet_aton('198.51.100.7')
            r.pmp.sendto(resp, addr)
        elif d[0] == 2:
            r.pmp.sendto(bytes([0, 0x81, 0, 1]) + b'\0' * 12, addr)  # (NAT-PMP only: "unsupported version")
        elif d[0] == 0 and r.a.mode == 'natpmp':
            op = d[1]
            if op == 0:
                r.pmp.sendto(bytes([0, 128, 0, 0]) + struct.pack('>I', int(time.time())) + socket.inet_aton('198.51.100.7'), addr)
            elif op in (1, 2) and len(d) >= 12:
                port, want, life = struct.unpack_from('>HHI', d, 4)
                pname = 'UDP' if op == 1 else 'TCP'
                ext = port + 1000
                if life: r.add(pname, addr[0], port, ext)
                else: r.delete(pname, ext)
                r.pmp.sendto(bytes([0, 128 + op, 0, 0]) + struct.pack('>IHHI', int(time.time()), port, ext if life else 0, life), addr)

    def http_req(r, c):
        c.settimeout(2)
        data = b''
        while b'\r\n\r\n' not in data:
            k = c.recv(4096)
            if not k: return
            data += k
        head, _, body = data.partition(b'\r\n\r\n')
        m = re.search(rb'Content-Length: *(\d+)', head, re.I)
        n = int(m.group(1)) if m else 0
        while len(body) < n:
            k = c.recv(4096)
            if not k: break
            body += k
        line = head.split(b'\r\n')[0].decode()
        if line.startswith('GET'):
            desc = ('<?xml version="1.0"?><root><device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>'
                    '<deviceList><device><deviceList><device><serviceList><service>'
                    '<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>/ctl/IPConn</controlURL>'
                    '</service></serviceList></device></deviceList></device></deviceList></device></root>')
            c.sendall(('HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(desc)).encode() + desc.encode())
            log('UPnP: description given')
            return
        b = body.decode('utf-8', 'replace')
        def tag(t):
            m = re.search('<%s>(.*?)</%s>' % (t, t), b)
            return m.group(1) if m else ''
        act = re.search(r'#(\w+)"', head.decode()).group(1)
        out = ''
        if act == 'AddPortMapping':
            # (UPnP: outside, the port asked for; one that is in use -- on this one computer, the
            # player's own -- is a conflict, and the player asks for the next)
            log('UPnP: AddPortMapping %s %s lease %s' % (tag('NewProtocol'), tag('NewExternalPort'), tag('NewLeaseDuration')))
            try:
                r.add(tag('NewProtocol'), tag('NewInternalClient'), int(tag('NewInternalPort')), int(tag('NewExternalPort')))
            except OSError:
                log('UPnP: outside %s taken: conflict' % tag('NewExternalPort'))
                err = '<s:Envelope><s:Body><s:Fault><detail><UPnPError><errorCode>718</errorCode></UPnPError></detail></s:Fault></s:Body></s:Envelope>'
                c.sendall(('HTTP/1.1 500 Internal Server Error\r\nContent-Length: %d\r\n\r\n' % len(err)).encode() + err.encode()); return
        elif act == 'DeletePortMapping':
            r.delete(tag('NewProtocol'), int(tag('NewExternalPort')))
        elif act == 'GetExternalIPAddress':
            out = '<NewExternalIPAddress>198.51.100.7</NewExternalIPAddress>'
        resp = '<?xml version="1.0"?><s:Envelope><s:Body><u:%sResponse>%s</u:%sResponse></s:Body></s:Envelope>' % (act, out, act)
        c.sendall(('HTTP/1.1 200 OK\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(resp)).encode() + resp.encode())

    def run(r, secs):
        end = time.time() + secs
        while time.time() < end:
            fwd = [m[0] for m in r.maps.values()]
            rl = r.socks + fwd + list(r.pipes)
            rd, _, _ = select.select(rl, [], [], 0.2)
            for s in rd:
                if s in r.socks:
                    if s is getattr(r, 'pmp', None):
                        d, addr = s.recvfrom(2048); r.pcp(d, addr)
                    elif s is getattr(r, 'ssdp', None):
                        d, addr = s.recvfrom(2048)
                        if b'M-SEARCH' in d:
                            s.sendto(('HTTP/1.1 200 OK\r\nST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n'
                                      'LOCATION: http://%s:5000/rootDesc.xml\r\n\r\n' % r.a.ip).encode(), addr)
                            log('SSDP: answered %s' % addr[0])
                    else:
                        c, _ = s.accept()
                        try: r.http_req(c)
                        finally: c.close()
                    continue
                if s in r.pipes:
                    other = r.pipes[s]
                    try: d = s.recv(65536)
                    except OSError: d = b''
                    if not d:
                        r.pipes.pop(s, None); r.pipes.pop(other, None); s.close(); other.close()
                    else: other.sendall(d)
                    continue
                key = next(k for k, m in r.maps.items() if m[0] is s)
                _, ip, port = r.maps[key]
                if key[0] == 'TCP':
                    c, addr = s.accept()
                    i = socket.create_connection((ip, port))
                    r.pipes[c] = i; r.pipes[i] = c
                    log('forwarded a TCP connection from %s outside %d -> %d' % (addr[0], key[1], port))
                else:
                    d, addr = s.recvfrom(65536)
                    if not hasattr(r, 'relay'): r.relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    r.relay.sendto(d, (ip, port))

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', default='pcp'); ap.add_argument('--ip', default='127.0.0.1')
    ap.add_argument('--secs', type=int, default=600); ap.add_argument('--log')
    a = ap.parse_args()
    if a.log: log_f = open(a.log, 'w')
    Router(a).run(a.secs)
