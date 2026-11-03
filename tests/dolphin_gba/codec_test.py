import os, socket, struct, subprocess, threading, time, sys
PORT = 6771
def start():
    p = subprocess.Popen([os.environ.get('GBAHARNESS') or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build', 'gbaharness'), '1'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
    out = []; threading.Thread(target=lambda: [out.append(l.strip()) for l in p.stdout], daemon=True).start()
    while 'READY' not in out: time.sleep(0.05)
    return p
from wsclient import WS, unpack
def rgb(r, g, b): return (r & 31) | (g & 31) << 5 | (b & 31) << 10
def expected(m):
    """The harness's patterns (harness.cpp), in the GBA's 15-bit colour."""
    out = []
    for i in range(240 * 160):
        x, y = i % 240, i // 240
        if m == 2: c = rgb(x % 32, y % 32, 0)
        elif m == 3:
            r = ((i * 2654435761) & 0xFFFFFFFF) ^ 0x9E3779B9; r ^= r >> 15; r = (r * 2246822519) & 0xFFFFFFFF; r ^= r >> 13
            c = rgb(r, r >> 5, r >> 10)
        elif m == 4:
            c = rgb(3, 5, 12)
            if y < 16: c = rgb(24, 24, 6)
            elif 20 <= x < 220 and 40 <= y < 140: c = rgb(31, 31, 31) if (x == 20 or x == 219 or y == 40 or y == 139) else rgb(8, 8, 10)
            if 60 <= y < 100 and 40 <= x < 200 and (x * 7 + y * 13) % 11 < 2: c = rgb(31, 31, 31)
            if 100 <= x < 132 and 100 <= y < 132:
                r = ((x * 31 + y * 17) * 2654435761) & 0xFFFFFFFF; c = rgb(r >> 3, r >> 8, r >> 13)
        elif m == 5: c = rgb(x * 31 // 239, 31 - x * 31 // 239, 16)
        elif m == 6: c = rgb(31, 31, 31) if (x + y) & 1 else rgb(0, 0, 0)
        else: c = rgb((x + y) * 31 // 398, x * 31 // 239, y * 31 // 159)
        out.append(c)
    return out
def picture():
    """One packed picture of the current pattern, through /video."""
    # The first may be the picture kept from before the pattern changed (nobody watched since):
    # the newest within half a second is the current one.
    v = WS('/video?p=2'); v.send_text('fps 60'); v.s.settimeout(0.5); last = None
    try:
        while True:
            op, data = v.recv()
            if op == 2: last = data; v.send_text('ack')
    except (TimeoutError, OSError):
        pass
    v.close(); return last
p = start(); bad = 0
print('%-28s %9s %9s %7s  %s' % ('picture', 'raw', 'packed', 'ratio', 'lossless?'))
for mode, name in ((2, 'harness pattern'), (3, 'noise (worst case)'), (4, 'flat UI + sprite'), (5, 'horizontal gradient'), (6, '1-px checkerboard'), (7, 'diagonal gradient')):
    p.stdin.write('pat%d\n' % mode); p.stdin.flush(); time.sleep(0.5)
    z = picture(); w, hh, b = unpack(z)
    a = expected(mode)
    raw = b'x' * (8 + 2 * w * hh)
    ok = (w, hh) == (240, 160) and a == b
    bad += not ok
    print('%-28s %7d B %7d B %6.1fx  %s' % (name, len(raw), len(z), len(raw) / len(z), 'YES' if ok else 'NO  <<<<<'))
p.stdin.write('quit\n'); p.stdin.flush(); p.wait(timeout=10)
print('\nALL LOSSLESS' if not bad else '\nMISMATCH')
sys.exit(1 if bad else 0)
