"""A minimal WebSocket client (RFC 6455) for the tests: stdlib only."""
import base64, os, socket, struct

PORT = 6771

class WS:
    def __init__(self, path, timeout=5):
        self.s = socket.create_connection(('127.0.0.1', PORT), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(('GET %s HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
                        'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n' % (path, key)).encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            c = self.s.recv(4096)
            if not c: raise ConnectionError('closed during handshake')
            buf += c
        head, _, self.buf = buf.partition(b'\r\n\r\n')
        self.status = head.split(b'\r\n')[0].decode()
        if ' 101 ' not in self.status: raise ConnectionError(self.status)

    def send_text(self, text):
        data = text.encode(); mask = os.urandom(4)
        n = len(data)
        head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack('>H', n))
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _need(self, n):
        while len(self.buf) < n:
            c = self.s.recv(1 << 20)
            if not c: raise ConnectionError('closed')
            self.buf += c

    def recv(self):
        """(opcode, payload) of the next message."""
        self._need(2); b0, b1 = self.buf[0], self.buf[1]; n = b1 & 127; off = 2
        if n == 126: self._need(4); n = struct.unpack('>H', self.buf[2:4])[0]; off = 4
        elif n == 127: self._need(10); n = struct.unpack('>Q', self.buf[2:10])[0]; off = 10
        self._need(off + n); payload = self.buf[off:off + n]; self.buf = self.buf[off + n:]
        return b0 & 15, payload

    def close(self):
        try: self.s.close()
        except OSError: pass

def http_get(path, timeout=5):
    s = socket.create_connection(('127.0.0.1', PORT), timeout=timeout)
    s.sendall(('GET %s HTTP/1.1\r\nHost: x\r\n\r\n' % path).encode())
    data = b''
    while True:
        try: c = s.recv(65536)
        except socket.timeout: break
        if not c: break
        data += c
    s.close()
    head, _, body = data.partition(b'\r\n\r\n')
    return head.decode(errors='replace'), body

def unpack(buf):
    """An independent Python port of the page's 'GBA2' decoder."""
    assert buf[:4] == b'GBA2', buf[:4]
    w, h = struct.unpack('<HH', buf[4:8]); n = w * h
    ln = struct.unpack('<I', buf[8:12])[0]; assert len(buf) == 12 + ln, (len(buf), ln)
    i = 12; end = 12 + ln; px = []
    while i < end and len(px) < n:
        c = buf[i]; i += 1
        if c < 128:
            k = c + 1; px += struct.unpack('<%dH' % k, buf[i:i + 2 * k]); i += 2 * k
        else:
            k = (c & 127) + 3; v = struct.unpack('<H', buf[i:i + 2])[0]; i += 2; px += [v] * k
    assert len(px) == n and i == end, (len(px), n, i, end)
    for j in range(w, n): px[j] ^= px[j - w]
    return w, h, px
