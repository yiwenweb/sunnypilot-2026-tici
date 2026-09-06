#!/usr/bin/env python3
"""c3 stream server - MJPEG UI stream + touch input relay.

Reads frames written by the UI (STREAM mode) from shared memory
/dev/shm/openpilot_ui_frames and serves them as multipart MJPEG.
Receives touch events on POST /input and relays them to c3touchd
via unix socket /tmp/c3touch_sock.
Pure userspace, zero system modifications.
"""
import json
import mmap
import os
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SHM_PATH = "/dev/shm/openpilot_ui_frames"
TOUCH_SOCK = "/tmp/c3touch_sock"
PORT = int(os.getenv("STREAM_PORT", "8081"))
BOUNDARY = "c3frame"

_HEADER = struct.Struct("<QIIII")  # ts, width, height, size, format
HEADER_LEN = _HEADER.size  # 24
READY_OFF = 24
DATA_OFF = 31

INDEX_HTML = """<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>C3 UI</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{margin:0;background:#111;overflow:hidden}img{width:100vw;height:100vh;object-fit:contain;touch-action:none}</style>
</head><body><img id="f" src="/stream">
<script>
const img=document.getElementById('f');
function send(x,y,type){fetch('/input',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({x:x,y:y,type:type})});}
let last=null;
// Map a pointer position inside the viewport to C3 screen coords (2160x1080),
// accounting for object-fit:contain letterboxing of the <img> element.
function c3pos(e){
  const r=img.getBoundingClientRect();
  const t=(e.touches&&e.touches.length)?e.touches[0]:e;
  const px=t.clientX-r.left, py=t.clientY-r.top;
  const iw=img.naturalWidth||1280, ih=img.naturalHeight||640;
  const sc=Math.min(r.width/iw, r.height/ih);
  const dw=iw*sc, dh=ih*sc;
  const dx=(r.width-dw)/2, dy=(r.height-dh)/2;
  let fx=(px-dx)/dw, fy=(py-dy)/dh;
  fx=Math.max(0,Math.min(1,fx)); fy=Math.max(0,Math.min(1,fy));
  return [Math.round(fx*2159), Math.round(fy*1079)];
}
function down(e){e.preventDefault(); const p=c3pos(e); send(p[0],p[1],'down'); last=p;}
function move(e){e.preventDefault(); if(!last)return; const p=c3pos(e); if(p[0]!==last[0]||p[1]!==last[1]){send(p[0],p[1],'move'); last=p;}}
function up(e){e.preventDefault(); if(last){send(0,0,'up'); last=null;}}
img.addEventListener('touchstart',down,{passive:false});
img.addEventListener('touchmove',move,{passive:false});
img.addEventListener('touchend',up,{passive:false});
img.addEventListener('touchcancel',up,{passive:false});
img.addEventListener('mousedown',down);
img.addEventListener('mousemove',move);
document.addEventListener('mouseup',up);
</script></body></html>"""


def read_frame():
    """Return (ts, width, height, jpeg_bytes) or None. Blocks briefly."""
    try:
        fd = os.open(SHM_PATH, os.O_RDONLY)
    except OSError:
        return None
    try:
        mm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
        try:
            head = mm[:HEADER_LEN]
            ts, w, h, size, fmt = _HEADER.unpack(head)
            if ts == 0 or size <= 0 or size > 16 * 1024 * 1024:
                return None
            ready = mm[READY_OFF]
            if ready != 1:
                return None
            data = bytes(mm[DATA_OFF:DATA_OFF + size])
            return ts, w, h, data
        finally:
            mm.close()
    finally:
        os.close(fd)


def touch_send(x, y, typ):
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(2.0)
        s.connect(TOUCH_SOCK)
        s.sendall(f"{int(x)} {int(y)} {typ}\n".encode())
        s.close()
        return True
    except OSError:
        return False


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "C3Stream/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s\n" % (time.strftime("%H:%M:%S"), fmt % args))

    def _headers_ok(self, length=None, ctype="application/octet-stream"):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        if length is not None:
            self.send_header("Content-Length", str(length))
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        if self.path.startswith("/stream"):
            self._stream()
        elif self.path == "/stats":
            self._stats()
        else:
            body = INDEX_HTML.encode()
            self._headers_ok(len(body), "text/html; charset=utf-8")
            self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_POST(self):
        if not self.path.startswith("/input"):
            self.send_error(404)
            return
        try:
            n = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(n) or b"{}")
            x = int(body.get("x", 0))
            y = int(body.get("y", 0))
            typ = str(body.get("type", "move"))
            if typ not in ("down", "move", "up"):
                typ = "move"
            ok = touch_send(x, y, typ)
            resp = b'{"ok":1}' if ok else b'{"ok":0}'
            self._headers_ok(len(resp), "application/json")
            self.wfile.write(resp)
        except Exception as e:
            self.send_error(400, str(e))

    def _stats(self):
        info = {"port": PORT, "shm": os.path.exists(SHM_PATH)}
        body = json.dumps(info).encode()
        self._headers_ok(len(body), "application/json")
        self.wfile.write(body)

    def _stream(self):
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=%s" % BOUNDARY)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        last_ts = -1
        try:
            while True:
                fr = read_frame()
                if fr is None:
                    time.sleep(0.05)
                    continue
                ts, w, h, data = fr
                if ts == last_ts:
                    time.sleep(0.03)
                    continue
                last_ts = ts
                head = ("--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\n\r\n"
                        % (BOUNDARY, len(data))).encode()
                self.wfile.write(head)
                self.wfile.write(data)
                self.wfile.write(b"\r\n")
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass


def main():
    srv = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    srv.daemon_threads = True
    print("C3 stream server on :%d (shm=%s)" % (PORT, os.path.exists(SHM_PATH)), flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
