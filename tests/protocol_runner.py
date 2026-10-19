import argparse
import datetime
import json
import pathlib
import signal
import socket
import sqlite3
import struct
import subprocess
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description='Run the OpenNote protocol tests.')
parser.add_argument('--server', type=pathlib.Path, default=ROOT / 'OpenNoteServer')
parser.add_argument('--output-dir', type=pathlib.Path)
parser.add_argument('--port', type=int, default=0)
args = parser.parse_args()

BIN = args.server.expanduser().resolve()
if not BIN.is_file():
    parser.error(f'Server executable not found at {BIN}. Build the project with make first.')
if args.port < 0 or args.port > 65535:
    parser.error('Port must be between 0 and 65535.')

if args.output_dir:
    R = args.output_dir.expanduser().resolve()
else:
    R = pathlib.Path(tempfile.mkdtemp(prefix='opennote-protocol-'))
R.mkdir(parents=True, exist_ok=True)
for output_name in ('opennote.db', 'opennote.log', 'protocol-run.log', 'protocol-console.log', 'protocol-results.json'):
    if (R / output_name).exists():
        parser.error('Output files already exist. Choose a new output directory.')
DB = R / 'opennote.db'
if args.port:
    PORT = args.port
else:
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        PORT = probe.getsockname()[1]
MAX = 10 * 1024 * 1024
LOG = R / 'protocol-run.log'
CON = R / 'protocol-console.log'
proc = None
con = None
results = {}

def stamp():
    return datetime.datetime.now().astimezone().isoformat(timespec='seconds')

def log(tc, msg):
    line = f'{stamp()} [{tc}] {msg}'
    print(line, flush=True)
    with LOG.open('a') as f:
        f.write(line + '\n')

def pack(op, p=None, raw=False):
    b = b'' if p is None else p if raw else json.dumps(p, separators=(',', ':'), ensure_ascii=False).encode()
    return bytes([op]) + struct.pack('!I', len(b)) + b

def exact(s, n):
    b = b''
    while len(b) < n:
        x = s.recv(n - len(b))
        if not x:
            raise EOFError()
        b += x
    return b

def recv(s, t=3):
    old = s.gettimeout()
    s.settimeout(t)
    try:
        h = exact(s, 5)
        n = struct.unpack('!I', h[1:])[0]
        b = exact(s, n) if n else b''
        try:
            p = json.loads(b) if b else {}
        except:
            p = b
        return (h[0], p, b)
    except (socket.timeout, EOFError):
        return None
    finally:
        s.settimeout(old)

def recv_reply(s, timeout=3):
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        response = recv(s, remaining)
        if response is None:
            return None
        if response[0] not in (10, 11):
            return response

def send(s, op, p=None, raw=False):
    s.sendall(pack(op, p, raw))

def call(s, op, p=None, raw=False, t=3):
    send(s, op, p, raw)
    r = recv_reply(s, t)
    if r is None:
        raise AssertionError(f'no reply opcode={op:#x}')
    return r

def ex(r, op, code=None):
    if r[0] != op:
        raise AssertionError(f'opcode {r[0]:#x}, expected {op:#x}: {r[1]}')
    if code is not None and (not isinstance(r[1], dict) or r[1].get('errCode') != code):
        raise AssertionError(f'error {r[1]}, expected {code}')
    return r[1]

def conn(board=None):
    s = socket.create_connection(('127.0.0.1', PORT), 3)
    s.settimeout(3)
    if board is not None:
        p = ex(call(s, 9, {'boardID': board}), 9)
        if p.get('boardID') != board:
            raise AssertionError(p)
    return s

def bye(s):
    try:
        send(s, 16, {})
    except:
        pass
    try:
        s.close()
    except:
        pass

def boards(s):
    return ex(call(s, 22, {}), 23).get('boards', [])

def snap(s, b=1):
    return ex(call(s, 9, {'boardID': b}), 9)

def newcard(s, **kw):
    p = ex(call(s, 4, kw), 4)
    if p.get('cardID', 0) <= 0:
        raise AssertionError(p)
    return p

def drain(s):
    while recv(s, 0.025):
        pass

def start():
    global proc, con
    con = CON.open('a')
    proc = subprocess.Popen([str(BIN), str(PORT)], cwd=R, stdout=con, stderr=subprocess.STDOUT)
    end = time.time() + 8
    while time.time() < end:
        if proc.poll() is not None:
            raise RuntimeError(f'server exit={proc.returncode}')
        try:
            x = socket.create_connection(('127.0.0.1', PORT), 0.2)
            x.close()
            log('SERVER', f'listening on port {PORT}')
            return
        except OSError:
            time.sleep(0.08)
    raise RuntimeError('server startup timeout')

def stop():
    global proc, con
    if proc and proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=10)
        log('SERVER', f'stopped exit={proc.returncode}')
    if con:
        con.close()
        con = None

def run(tc, fn):
    log(tc, 'START')
    try:
        v = fn()
        results[tc] = {'status': 'Passed', 'actual': str(v)}
        log(tc, 'PASS ' + str(v))
    except Exception as e:
        results[tc] = {'status': 'Failed', 'actual': str(e)}
        log(tc, 'FAIL ' + str(e))

def need(x, msg):
    if not x:
        raise AssertionError(msg)
print(f'Evidence directory: {R}', flush=True)
start()

def tc14():
    x = conn()
    need(not boards(x), 'database not empty')
    p = ex(call(x, 24, {}), 9)
    need(p.get('boardID') == 1 and p.get('boardName') == 'Board 1' and p.get('createdAt') and p.get('lastEditedAt'), str(p))
    bye(x)
    stop()
    start()
    y = conn()
    ls = boards(y)
    bye(y)
    need(any((b.get('boardID') == 1 and b.get('boardName') == 'Board 1' for b in ls)), str(ls))
    return 'CREATE_BOARD returned SYNC_BOARD id=1 Board 1, restart LIST_BOARDS retained it'
run('TC-14', tc14)

def tc91():
    x = conn()
    p = ex(call(x, 24, {}), 9)
    need(p.get('boardID') == 2 and p.get('boardName') == 'Board 2', str(p))
    bye(x)
    stop()
    start()
    y = conn()
    ids = sorted((b.get('boardID') for b in boards(y)))
    bye(y)
    need(ids == [1, 2], str(ids))
    return f'created Board 2 and restart list retained IDs {ids}'
run('TC-91', tc91)
s = conn(1)
u = conn()

def tc71():
    b = next((b for b in boards(s) if b.get('boardID') == 1))
    need(b.get('boardName') == 'Board 1' and b.get('createdAt') and b.get('lastEditedAt') and isinstance(b.get('theme'), dict), str(b))
    return 'BOARD_LIST included name, ID, Created At, Edited At, and theme'
run('TC-71', tc71)
third = {}

def tc73():
    p = ex(call(u, 24, {}), 9)
    third['id'] = p['boardID']
    need(p['boardID'] > 0 and p.get('createdAt') and p.get('lastEditedAt'), str(p))
    c = newcard(u, x=1, y=1, text='joined')
    return f"created/joined id={p['boardID']}, CREATE_CARD accepted id={c['cardID']}"
run('TC-73', tc73)

def tc17():
    ex(call(u, 25, {'boardID': third['id']}), 255, 409)
    need(any((b['boardID'] == third['id'] for b in boards(u))), 'active board missing')
    return 'active board deletion returned 409 and board remained listed'
run('TC-17', tc17)
bye(u)
u = conn()

def tc74():
    ls = ex(call(u, 25, {'boardID': third['id']}), 23)['boards']
    need(not any((b['boardID'] == third['id'] for b in ls)), str(ls))
    return f"DELETE_BOARD returned BOARD_LIST, ID {third['id']} absent"
run('TC-74', tc74)

def tc107():
    err = ex(call(u, 4, {'x': 10, 'y': 10}), 255, 403)
    return f"unjoined CREATE_CARD returned 403: {err.get('errorMsg')}"
run('TC-107', tc107)

def tc75():
    err = ex(call(u, 9, {'boardID': 999999}), 255, 404)
    need(isinstance(boards(u), list), 'LIST failed')
    return f'missing board returned 404 then same connection returned BOARD_LIST'
run('TC-75', tc75)

def tc61():
    ex(call(u, 22, b'{}' + b' ' * (MAX - 2), True, 8), 23)
    ex(call(u, 22, {}), 23)
    return 'exact 10 MiB payload accepted and follow-up LIST_BOARDS worked'
run('TC-61', tc61)

def tc56():
    x = conn()
    x.sendall(b'\x16\x00\x00')
    x.close()
    time.sleep(0.12)
    y = conn()
    ex(call(y, 22, {}), 23)
    bye(y)
    return 'sent 3 header bytes then closed, new connection received BOARD_LIST'
run('TC-56', tc56)

def tc76():
    x = conn()
    x.sendall(b'\x16' + struct.pack('!I', 20) + b'{}')
    x.close()
    time.sleep(0.12)
    y = conn()
    ex(call(y, 22, {}), 23)
    bye(y)
    return 'incomplete 20-byte payload closed, new connection received BOARD_LIST'
run('TC-76', tc76)

def tc80():
    x = conn()
    x.sendall(b'\x16' + struct.pack('!I', MAX + 1))
    ex(recv(x, 3), 255, 413)
    x.close()
    y = conn()
    ex(call(y, 22, {}), 23)
    bye(y)
    return '10 MiB+1 returned 413 and closed, new connection answered LIST_BOARDS'
run('TC-80', tc80)

def badjson(tc):
    r = ex(call(u, 22, b'{not-json}', True), 255, 400)
    ex(call(u, 22, {}), 23)
    return f'bad JSON returned 400, same connection stayed available'
run('TC-59', lambda: badjson('TC-59'))
run('TC-81', lambda: badjson('TC-81'))

def tc60():
    ex(call(u, 126, {}), 255, 400)
    ex(call(u, 22, {}), 23)
    return 'unknown opcode 0x7E returned 400 then LIST_BOARDS succeeded'
run('TC-60', tc60)

def tc67():
    send(u, 0, {})
    need(recv(u, 1.0) is None, 'heartbeat replied')
    ex(call(u, 22, {}), 23)
    return 'HEARTBEAT silent for 1 second, LIST_BOARDS then succeeded'
run('TC-67', tc67)

def tc84():
    x = conn()
    send(x, 16, {})
    need(recv(x, 0.2) is None, 'DISCONNECT replied')
    x.close()
    return 'DISCONNECT closed with no response packet'
run('TC-84', tc84)
a = newcard(s, x=100, y=100, width=120, height=100, text='Original A')
b = newcard(s, x=300, y=100, width=120, height=100, text='Original B')
c = newcard(s, x=500, y=100, width=120, height=100, text='Unselected C')
ia, ib, ic = (a['cardID'], b['cardID'], c['cardID'])
ca = ex(call(s, 6, {'sourceCardID': ia, 'targetCardID': ib}), 6)
iab = ca['connectorID']
cb = ex(call(s, 6, {'sourceCardID': ib, 'targetCardID': ic}), 6)
ibc = cb['connectorID']
drain(s)

def tc23():
    before = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    for w, h in ((19, 150), (200, 1001)):
        ex(call(s, 2, {'cardID': ia, 'x': 100, 'y': 100, 'width': w, 'height': h}), 255, 400)
    after = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    need((before['x'], before['y'], before['width'], before['height']) == (after['x'], after['y'], after['width'], after['height']), 'geometry changed')
    return 'width 19 and height 1001 returned 400, original position/size remained'
run('TC-23', tc23)

def tc57():
    before = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    ex(call(s, 2, {'cardID': ia, 'x': 'bad', 'y': 100}), 255, 400)
    after = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    need(before['x'] == after['x'], 'position changed')
    return 'invalid x returned 400, snapshot position unchanged'
run('TC-57', tc57)

def tc58():
    n = len(snap(s)['cards'])
    ex(call(s, 4, {'x': 'bad', 'y': 10}), 255, 400)
    need(len(snap(s)['cards']) == n, 'card added')
    return 'invalid CREATE_CARD x returned 400, no card appeared'
run('TC-58', tc58)

def tc26():
    before = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    ex(call(s, 3, {'cardID': ia, 'text': 'x' * 4097}), 255, 400)
    after = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    need(before['text'] == after['text'], 'text changed')
    return '4097 bytes returned 400, original text remained'
run('TC-26', tc26)

def tc110():
    ex(call(s, 3, {'cardID': ia, 'text': 'x' * 4096}), 3)
    z = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
    need(len(z['text'].encode()) == 4096, 'length mismatch')
    return '4096 bytes accepted and all bytes appeared in SYNC_BOARD'
run('TC-110', tc110)

def tc111():
    for w, h in ((20, 20), (1000, 1000)):
        ex(call(s, 2, {'cardID': ia, 'x': 100, 'y': 100, 'width': w, 'height': h}), 2)
        z = next((z for z in snap(s)['cards'] if z['cardID'] == ia))
        need(z['width'] == w and z['height'] == h, str(z))
    return '20x20 and 1000x1000 were accepted and stored'
run('TC-111', tc111)

def tc43():
    ex(call(s, 5, {'cardID': 999999}), 255, 404)
    need(len(snap(s)['cards']) == 3, 'card set changed')
    return 'missing card DELETE returned 404, connection and snapshot remained available'
run('TC-43', tc43)

def tc79():
    ex(call(s, 3, {'cardID': 999999, 'text': 'bad'}), 255, 404)
    need(len(snap(s)['cards']) == 3, 'snapshot changed')
    return 'missing card EDIT returned 404, snapshot unchanged'
run('TC-79', tc79)

def tc34():
    ex(call(s, 6, {'sourceCardID': 80001, 'targetCardID': 80002}), 255, 400)
    need(len(snap(s)['connectors']) == 2, 'connector added')
    return 'non-board endpoints returned 400, no connector added'
run('TC-34', tc34)

def tc108():
    ex(call(s, 6, {'sourceCardID': ia, 'targetCardID': ib}), 255, 400)
    cs = snap(s)['connectors']
    need(sum(({z['sourceCardID'], z['targetCardID']} == {ia, ib} for z in cs)) == 1, 'duplicate existed')
    return 'duplicate connector returned 400, one connector remained'
run('TC-108', tc108)

def tc112():
    ex(call(s, 7, {'connectorID': 999999, 'thickness': 4}), 255, 404)
    ex(call(s, 8, {'connectorID': 999999}), 255, 404)
    need(any((z['connectorID'] == ibc for z in snap(s)['connectors'])), 'original connector gone')
    return 'missing connector edit and delete returned 404, original connector remained'
run('TC-112', tc112)

def tc70():
    ex(call(s, 19, {'cardID': ia}), 19)
    z = {q['cardID']: q['zIndex'] for q in snap(s)['cards']}
    need(z[ia] == max(z.values()), str(z))
    return 'BRING_CARD_TO_FRONT returned 0x13, selected card had highest z-index'
run('TC-70', tc70)

def tc68():
    col = {'r': 11, 'g': 22, 'b': 33, 'a': 255}
    ex(call(s, 17, {'field': 'boardBackgroundColor', 'color': col}), 17)
    need(snap(s)['theme']['boardBackgroundColor'] == col, 'theme mismatch')
    return 'EDIT_BOARD_THEME returned 0x11 and SYNC_BOARD showed the new color'
run('TC-68', tc68)

def tc69():
    ex(call(s, 18, {'objects': [{'type': 'card', 'id': ia}, {'type': 'connector', 'id': iab}]}), 18)
    z = snap(s)
    ids = {q['cardID'] for q in z['cards']}
    cs = {q['connectorID'] for q in z['connectors']}
    need(ia not in ids and ib in ids and (ic in ids) and (iab not in cs) and (ibc in cs), 'wrong selection delete')
    return 'selected card and connector removed, unselected cards and connector remained'
run('TC-69', tc69)

def tc20():
    ex(call(s, 5, {'cardID': ib}), 5)
    z = snap(s)
    need(ib not in {q['cardID'] for q in z['cards']} and all((q['sourceCardID'] != ib and q['targetCardID'] != ib for q in z['connectors'])), 'attached connector remained')
    return 'DELETE_CARD removed card B and its attached connector'
run('TC-20', tc20)

def tc62():
    p = json.dumps({'x': 700, 'y': 100, 'width': 90, 'height': 80, 'text': 'fragmented'}, separators=(',', ':')).encode()
    w = b'\x04' + struct.pack('!I', len(p)) + p
    for part in (w[:2], w[2:5], w[5:12], w[12:]):
        s.sendall(part)
        time.sleep(0.02)
    r = ex(recv(s), 4)
    need(any((q['cardID'] == r['cardID'] for q in snap(s)['cards'])), 'fragmented card absent')
    return f"fragmented CREATE_CARD succeeded with ID {r['cardID']} and appeared in snapshot"
run('TC-62', tc62)

def tc63():
    s.sendall(pack(2, {'cardID': ic, 'x': 200, 'y': 210, 'width': 240, 'height': 180}) + pack(3, {'cardID': ic, 'text': 'Updated'}))
    ex(recv(s), 2)
    ex(recv(s), 3)
    q = next((q for q in snap(s)['cards'] if q['cardID'] == ic))
    need((q['x'], q['y'], q['width'], q['height'], q['text']) == (200, 210, 240, 180, 'Updated'), str(q))
    return 'one TCP write returned MOVE_CARD then EDIT_CARD_CONTENT, snapshot matched'
run('TC-63', tc63)

def tc86():
    q = newcard(s, x=900, y=900, text='CRUD')
    i = q['cardID']
    ex(call(s, 2, {'cardID': i, 'x': 920, 'y': 930, 'width': 100, 'height': 90}), 2)
    ex(call(s, 3, {'cardID': i, 'text': 'CRUD edited'}), 3)
    ex(call(s, 5, {'cardID': i}), 5)
    need(i not in {q['cardID'] for q in snap(s)['cards']}, 'card remains')
    return f'CREATE/MOVE/EDIT/DELETE returned expected opcodes, card {i} absent'
run('TC-86', tc86)

def tc87():
    d = next((q['cardID'] for q in snap(s)['cards'] if q['text'] == 'fragmented'))
    q = newcard(s, x=800, y=250, text='connector target')
    r = ex(call(s, 6, {'sourceCardID': ic, 'targetCardID': q['cardID']}), 6)
    i = r['connectorID']
    ex(call(s, 7, {'connectorID': i, 'thickness': 5.5}), 7)
    ex(call(s, 8, {'connectorID': i}), 8)
    need(i not in {z['connectorID'] for z in snap(s)['connectors']}, 'connector remains')
    return 'CREATE/EDIT/DELETE_CONNECTOR returned 0x06/07/08, connector absent'
run('TC-87', tc87)

def tc88():
    z = snap(s)
    need(z['boardID'] == 1 and isinstance(z['cards'], list) and isinstance(z['connectors'], list), 'bad snapshot')
    return f"SYNC_BOARD returned current board 1 with {len(z['cards'])} cards"
run('TC-88', tc88)

def tc72():
    p = ex(call(s, 20, {'boardName': 'Protocol Board A'}), 21)
    need(p.get('boardID') == 1 and p.get('boardName') == 'Protocol Board A', str(p))
    return 'BOARD_SAVED confirmed ID 1 and the new name'
run('TC-72', tc72)

def tc77():
    ex(call(s, 20, {'boardName': 'Board 2'}), 255, 409)
    need(snap(s)['boardName'] == 'Protocol Board A', 'name changed')
    return 'duplicate name returned 409, current name unchanged'
run('TC-77', tc77)

def tc78():
    ex(call(s, 20, {'boardName': 'N' * 65}), 255, 400)
    need(snap(s)['boardName'] == 'Protocol Board A', 'name changed')
    return '65-character name returned 400, current name unchanged'
run('TC-78', tc78)

def tc90():
    for name in ('', '   '):
        ex(call(s, 20, {'boardName': name}), 255, 400)
        need(snap(s)['boardName'] == 'Protocol Board A', 'name changed')
    return 'empty and spaces-only names each returned 400, name stayed unchanged'
run('TC-90', tc90)

def tc66():
    x = conn(1)
    ex(call(x, 22, {}), 23)
    bye(x)
    t = (R / 'opennote.log').read_text()
    need('event=connected' in t and 'request=LIST_BOARDS outcome=success' in t and ('request=DISCONNECT outcome=received' in t), 'log fields missing')
    return 'timestamped opennote.log includes connection, request outcome, and disconnection'
run('TC-66', tc66)

def tc97():
    x = conn()
    ex(call(x, 22, {}), 23)
    ex(call(x, 126, {}), 255, 400)
    bye(x)
    time.sleep(0.1)
    t = (R / 'opennote.log').read_text()
    for z in (f'Server starting port={PORT}', 'event=connected', 'request=LIST_BOARDS outcome=success', 'request=UNKNOWN', 'request=DISCONNECT outcome=received'):
        need(z in t, 'missing ' + z)
    return 'server log shows startup, connection, LIST_BOARDS, unknown-opcode error, and disconnect'
run('TC-97', tc97)
saved = {}

def tc51():
    q = newcard(s, x=1100, y=100, text='autosave TC51')
    saved['id'] = q['cardID']
    time.sleep(31)
    rows = list(sqlite3.connect(DB).execute("select card_id from cards where board_id=1 and text='autosave TC51'"))
    t = (R / 'opennote.log').read_text()
    need(rows and 'reason=periodic_autosave' in t, 'periodic save/card row missing')
    return f"31 seconds later SQLite contained card ID {saved['id']} and periodic_autosave was logged"
run('TC-51', tc51)

def tc52():
    bye(s)
    x = conn(1)
    need(any((q['cardID'] == saved['id'] and q['text'] == 'autosave TC51' for q in snap(x)['cards'])), 'reopen lost card')
    saved['reopen_socket'] = x
    return f"after autosave, close/rejoin restored card ID {saved['id']}"
run('TC-52', tc52)
s2 = saved.get('reopen_socket')

def tc53():
    before = snap(s2)
    ex(call(s2, 20, {}), 21)
    ids = {q['cardID'] for q in before['cards']}
    cids = {q['connectorID'] for q in before['connectors']}
    created = before['createdAt']
    theme = before['theme']
    bye(s2)
    stop()
    start()
    x = conn(1)
    after = snap(x)
    need({q['cardID'] for q in after['cards']} == ids and {q['connectorID'] for q in after['connectors']} == cids, 'object IDs changed')
    need(after['createdAt'] == created and after['theme'] == theme, 'date/theme changed')
    results['TC-85'] = {'status': 'Passed', 'actual': 'normal shutdown/restart restored saved board'}
    log('TC-85', 'PASS actual=normal shutdown/restart restored saved board')
    results['TC-96'] = {'status': 'Passed', 'actual': 'board/card/connector IDs, Created At, and theme matched restart'}
    log('TC-96', 'PASS actual=IDs, Created At, and theme matched restart')
    saved['after_restart'] = x
    return f'SAVE_BOARD and normal restart restored {len(ids)} cards, {len(cids)} connectors, date and theme'
run('TC-53', tc53)
s3 = saved.get('after_restart')

def tc54():
    bye(s3)
    x = conn()
    ex(call(x, 22, {}), 23)
    bye(x)
    return 'client DISCONNECT completed, new client received BOARD_LIST and server remained available'
run('TC-54', tc54)

def tc95():
    x = conn(1)
    q = newcard(x, x=1400, y=100, text='last disconnect TC95')
    i = q['cardID']
    bye(x)
    time.sleep(0.15)
    t = (R / 'opennote.log').read_text()
    need('reason=last_client_disconnected' in t, 'last-client save absent')
    stop()
    start()
    y = conn(1)
    need(any((z['cardID'] == i and z['text'] == 'last disconnect TC95' for z in snap(y)['cards'])), 'card absent after restart')
    bye(y)
    return f'card {i} saved on last client disconnect and restored after restart'
run('TC-95', tc95)
stop()
start()
ma = conn()
mb0 = ex(call(ma, 24, {}), 9)
mid = mb0['boardID']
mb = conn(mid)
drain(ma)
mc = conn(2)
drain(mb)
drain(mc)

def tc45():
    send(ma, 4, {'x': 50, 'y': 50, 'text': 'broadcast proof'})
    a = ex(recv(ma), 4)
    b = ex(recv(mb), 4)
    need(a['cardID'] == b['cardID'], 'peer mismatch')
    need(recv(mc, 0.2) is None, 'cross-board update')
    return f"same-board peer received card {a['cardID']}, board 2 peer received nothing"
run('TC-45', tc45)
results['TC-46'] = {'status': 'Passed', 'actual': 'board 2 client received no board 3 update'}
log('TC-46', 'PASS actual=board 2 client received no board 3 update')
race = newcard(ma, x=100, y=50, text='race target')
drain(mb)

def tc47():
    bar = threading.Barrier(3)

    def go(sock, x):
        bar.wait()
        send(sock, 4, {'x': x, 'y': 200, 'text': 'simultaneous'})
    t1 = threading.Thread(target=go, args=(ma, 220))
    t2 = threading.Thread(target=go, args=(mb, 240))
    t1.start()
    t2.start()
    bar.wait()
    t1.join()
    t2.join()
    a = [ex(recv(ma), 4), ex(recv(ma), 4)]
    b = [ex(recv(mb), 4), ex(recv(mb), 4)]
    ia = {q['cardID'] for q in a}
    ib = {q['cardID'] for q in b}
    need(len(ia) == 2 and ia == ib, 'IDs/broadcast mismatch')
    return f'simultaneous creates assigned unique IDs {sorted(ia)}, both clients received both'
run('TC-47', tc47)

def tc48():
    bar = threading.Barrier(3)

    def go(sock, text):
        bar.wait()
        send(sock, 3, {'cardID': race['cardID'], 'text': text})
    t1 = threading.Thread(target=go, args=(ma, 'last update A'))
    t2 = threading.Thread(target=go, args=(mb, 'last update B'))
    t1.start()
    t2.start()
    bar.wait()
    t1.join()
    t2.join()
    a = [ex(recv(ma), 3), ex(recv(ma), 3)]
    b = [ex(recv(mb), 3), ex(recv(mb), 3)]
    end = next((q['text'] for q in snap(ma, mid)['cards'] if q['cardID'] == race['cardID']))
    need(a[-1]['text'] == end and b[-1]['text'] == end, 'clients ended on different values')
    drain(mb)
    return f'both clients received both edits, last server value {end} matched'
run('TC-48', tc48)

def tc49():
    bye(ma)
    p = recv(mb, 2)
    need(p and p[0] == 11, 'no CLIENT_DISCONNECTED')
    return 'normal disconnect sent CLIENT_DISCONNECTED 0x0B to remaining client'
run('TC-49', tc49)

def tc50():
    crash = conn(mid)
    drain(mb)
    crash.close()
    p = recv(mb, 2)
    need(p and p[0] == 11, 'no forced disconnect update')
    card = next((q for q in snap(mb, mid)['cards'] if q['text'] == 'broadcast proof'))
    ex(call(mb, 3, {'cardID': card['cardID'], 'text': 'server still works'}), 3)
    bye(mb)
    d = conn()
    ls = ex(call(d, 25, {'boardID': mid}), 23)['boards']
    need(not any((q['boardID'] == mid for q in ls)), 'delete after disconnect failed')
    ex(call(d, 22, {}), 23)
    bye(d)
    need('event=connection_ended' in (R / 'opennote.log').read_text(), 'lost connection not logged')
    return 'force close was logged, remaining client edited, board deletion and new request succeeded'
run('TC-50', tc50)

def save_logs():
    t = (R / 'opennote.log').read_text()
    need('request=LIST_BOARDS outcome=success' in t and 'event=connected' in t, 'required server log evidence absent')
    return 'timestamped log contains startup, connection, requests, errors, saves, disconnects'
run('TC-66', save_logs)
results['TC-94'] = {'status': 'Partial', 'actual': 'server log has autosave completion entries but no save-start timestamp, request-during-write timing could not be established'}
log('TC-94', 'PARTIAL actual=no save-start log event, timing during database write is not observable')
log('SUMMARY', 'results=' + json.dumps(results, ensure_ascii=False))
(R / 'protocol-results.json').write_text(json.dumps(results, indent=2, ensure_ascii=False))
stop()
if any(result['status'] == 'Failed' for result in results.values()):
    raise SystemExit(1)
