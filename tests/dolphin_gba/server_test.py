"""The page server (GBAWeb.cpp) against a fake GBA (harness.cpp): pages, /ports, the WebSockets
(/video pictures with acks and an fps cap, /input buttons and the page's actions), and its
robustness. The harness's GBA is port 2 (device 1)."""
import json, os, socket, subprocess, sys, threading, time
from wsclient import WS, http_get, unpack

HARNESS = [os.environ.get('GBAHARNESS') or os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build', 'gbaharness'), '1']
fails = []
def check(name, cond, detail=''):
    print(('PASS ' if cond else 'FAIL ') + name + (('  [' + str(detail) + ']') if detail != '' else ''))
    if not cond: fails.append(name)

def start():
    p = subprocess.Popen(HARNESS, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    out, err = [], []
    threading.Thread(target=lambda: [out.append(l.strip()) for l in p.stdout], daemon=True).start()
    threading.Thread(target=lambda: [err.append(l.strip()) for l in p.stderr], daemon=True).start()
    t = time.time()
    while 'READY' not in out and time.time() - t < 10:
        if 'HOST NULL' in out: break
        time.sleep(0.05)
    time.sleep(0.2)
    return p, out, err

def cmd(p, line):
    p.stdin.write(line + '\n'); p.stdin.flush()

def frames(ws, n, ack=True, timeout=8):
    """n pictures (acknowledged as the page does); returns them and the time they took."""
    got = []; t0 = time.time(); ws.s.settimeout(timeout)
    while len(got) < n:
        op, data = ws.recv()
        if op == 2:
            got.append(data)
            if ack: ws.send_text('ack')
    return got, time.time() - t0

def wait_for(lines, text, timeout=3):
    t = time.time()
    while time.time() - t < timeout:
        if any(text in l for l in lines): return True
        time.sleep(0.02)
    return False

p, out, err = start()
check('harness started', 'READY' in out, out[:3])
check('server announced (Info)', wait_for(err, 'I: GBA web: serving on port 6771'), err[:3])

h, b = http_get('/ports')
check('/ports lists port 2', h.startswith('HTTP/1.1 200') and json.loads(b) == [2], b)
h, b = http_get('/')
check('/ serves the one-GBA page', h.startswith('HTTP/1.1 200') and b'<canvas' in b and b'<title>GBA</title>' in b, h.split('\r\n')[0])
h, b = http_get('/multi')
check('/multi serves the several-GBAs page', h.startswith('HTTP/1.1 200') and b'<title>GBAs</title>' in b, h.split('\r\n')[0])
h, _ = http_get('/nope')
check('an unknown path is 404', h.startswith('HTTP/1.1 404'), h.split('\r\n')[0])
h, _ = http_get('/video')
check('/video without an upgrade is 400', h.startswith('HTTP/1.1 400'), h.split('\r\n')[0])

# Pictures: the animated pattern at the source's 60 a second.
cmd(p, 'regular')
v = WS('/video?p=2'); v.send_text('fps 60')
got, dt = frames(v, 90)
fps = (len(got) - 1) / dt
check('pictures at about the source rate (60 fps) with acks', 50 <= fps <= 66, round(fps, 1))
w, hh, px = unpack(got[-1])
check('a picture is 240x160', (w, hh) == (240, 160), (w, hh))
c = px[7 * 240 + 5]
check('pixel (5,7) decodes losslessly: red 5, green 7', (c & 31, c >> 5 & 31) == (5, 7), hex(c))
blues = {unpack(g)[2][0] >> 10 & 31 for g in got}
check('pictures advance (blue carries the frame count)', len(blues) > 20, len(blues))

v.close()
v = WS('/video?p=2'); v.send_text('fps 30')
frames(v, 5)
got, dt = frames(v, 45)
fps30 = (len(got) - 1) / dt
check('"fps 30" spaces them to about 30', 25 <= fps30 <= 34, round(fps30, 1))
v.close()

# Without acks the server stops at eight ahead.
v = WS('/video?p=2'); v.send_text('fps 60')
v.s.settimeout(1.0); n = 0
try:
    while True:
        op, _ = v.recv()
        if op == 2: n += 1
except (socket.timeout, TimeoutError):
    pass
check('no acks: at most eight pictures ahead, then it waits', n == 8, n)
v.close()

# A still screen: one picture, then nothing until it changes; a late viewer gets it at once.
cmd(p, 'still'); time.sleep(0.3)
v = WS('/video?p=2'); v.send_text('fps 60')
# (The picture kept from before may come first, then the current one.)
got, _ = frames(v, 1, timeout=3)
v.s.settimeout(0.5)
try:
    while True:
        op, _ = v.recv()
        if op == 2: v.send_text('ack')
except (socket.timeout, TimeoutError):
    pass
v.s.settimeout(1.0); extra = 0
try:
    while True:
        op, _ = v.recv()
        if op == 2: extra += 1; v.send_text('ack')
except (socket.timeout, TimeoutError):
    pass
check('a still screen sends one picture and then nothing', extra == 0, extra)
late = WS('/video?p=2'); late.send_text('fps 60')
t0 = time.time(); got2, _ = frames(late, 1, timeout=3)
check('a late viewer of a still screen gets the picture at once', time.time() - t0 < 0.5, round(time.time() - t0, 2))
cmd(p, 'regular')
t0 = time.time(); frames(v, 1, timeout=3)
check('the moment it changes, the next picture comes', time.time() - t0 < 0.3, round(time.time() - t0, 2))
v.close(); late.close()

# Buttons.
i = WS('/input?p=2')
i.send_text('k 5')
check('"k 5" reaches the GBA (ExternalKeys 005)', wait_for(out, 'KEYS 1 005'))
i.send_text('k 4095')
check('a mask is clamped to the ten keys (3ff)', wait_for(out, 'KEYS 1 3ff'))
check('held keys nobody refreshes are let go after about two seconds', wait_for(out[-1:] and out, 'KEYS 1 000', 3.5))
i.send_text('k 64')
wait_for(out, 'KEYS 1 040'); i.close()
time.sleep(0.5)
check('closing the page lets go of its keys', out[-1] == 'KEYS 1 000' or wait_for(out, 'KEYS 1 000'), out[-2:])
i = WS('/input?p=2')
i.send_text('reset')
check('Reset GBA from the page asks the GBA to start over', wait_for(out, 'RESET 1'))
i.send_text('disconnect'); i.send_text('disconnect')
check('Disconnect GBA toggles the link cable: out, then in', wait_for(out, 'REQUEST 1 1') and wait_for(out, 'REQUEST 1 0'), [l for l in out if l.startswith('REQUEST')])
check('the link cable from the page is logged at Info', wait_for(err, 'I: GBA 2: link cable out from the page') and wait_for(err, 'I: GBA 2: link cable in from the page'))
check('the page\'s buttons are logged at Info', wait_for(err, 'I: GBA 2: Reset from the page'))
i.close()

# A viewer that stops reading must not slow the GBA.
cmd(p, 'stats'); time.sleep(0.1)
stalled = WS('/video?p=2'); stalled.send_text('fps 60')
time.sleep(2.5)
cmd(p, 'stats'); time.sleep(0.2)
stats = [l for l in out if l.startswith('STATS')]
mx = int(stats[-1].split('max_framended_us=')[1]) if stats else 10**9
check('FrameEnded never stalled while a viewer stopped reading (max < 20 ms)', mx < 20000, stats[-1:] if stats else 'none')
stalled.close()

for _ in range(25):
    try:
        s = WS('/video?p=2'); s.send_text('fps 60'); s.close()
    except OSError:
        pass
v = WS('/video?p=2'); v.send_text('fps 60')
got, _ = frames(v, 10)
check('healthy after 25 abrupt disconnects', len(got) == 10)

s = socket.create_connection(('127.0.0.1', 6771), timeout=3)
s.sendall(b'POST / HTTP/1.1\r\n\r\n'); r = s.recv(100); s.close()
check('a request that is not GET gets 405', r.startswith(b'HTTP/1.1 405'), r[:30])

# Shutdown with live viewers, and a restart at once.
v2 = WS('/video?p=2'); v2.send_text('fps 60'); frames(v2, 3)
cmd(p, 'quit'); p.wait(timeout=10)
sm = [l for l in out if l.startswith('SHUTDOWN_MS')]
check('shutdown with two live viewers is quick (< 1.5 s)', sm and int(sm[0].split()[1]) < 1500, sm)
check('the server says it stopped', wait_for(err, 'I: GBA web: stopped'))
v.close(); v2.close()
p, out, err = start()
check('the server restarts at once (SO_REUSEADDR)', wait_for(err, 'serving on port 6771'), err[:2])
cmd(p, 'quit'); p.wait(timeout=10)

print('\nALL PASSED' if not fails else '\nFAILED: %d' % len(fails))
sys.exit(1 if fails else 0)
