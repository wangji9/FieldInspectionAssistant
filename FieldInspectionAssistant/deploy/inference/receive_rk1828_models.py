"""Temporary LAN receiver restricted to one PC, token and exact model file list."""
import hmac
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote
import shutil

CFG = json.loads(Path('/tmp/rk1828-transfer.json').read_text())
ROOT = Path('/opt/rk1828-models')


class Handler(BaseHTTPRequestHandler):
    def destination(self):
        if self.client_address[0] != CFG['client'] or not hmac.compare_digest(
                self.headers.get('Authorization', ''), 'Bearer '+CFG['token']):
            self.send_error(403)
            return None
        rel = unquote(self.path.removeprefix('/'))
        if rel not in CFG['files']:
            self.send_error(403)
            return None
        return rel, ROOT/rel

    def do_HEAD(self):
        entry=self.destination()
        if not entry:return
        rel,dest=entry
        offset=int(self.headers.get('X-Offset','0'))
        chunk=Path(str(dest)+f'.partial.part-{offset}')
        self.send_response(200)
        self.send_header('Content-Length',str(chunk.stat().st_size if chunk.exists() else 0))
        self.end_headers()

    def do_PUT(self):
        entry=self.destination()
        if not entry:return
        rel,dest=entry
        try:
            offset = int(self.headers.get('X-Offset', '0'))
            length = int(self.headers['Content-Length'])
        except (KeyError, ValueError):
            self.send_error(400)
            return
        if offset<0 or length<=0 or length>64*1024*1024 or offset+length>CFG['files'][rel]:
            self.send_error(409)
            return
        chunk=Path(str(dest)+f'.partial.part-{offset}')
        with chunk.open('wb') as f:
            while length:
                b = self.rfile.read(min(length, 1024*1024))
                if not b:
                    return
                f.write(b)
                length -= len(b)
        self.send_response(200)
        self.send_header('Content-Length', '2')
        self.end_headers()
        self.wfile.write(b'OK')

    def do_POST(self):
        entry=self.destination()
        if not entry:return
        rel,dest=entry
        partial=Path(str(dest)+'.partial')
        offset=partial.stat().st_size if partial.exists() else 0
        # Validate all segments before modifying the contiguous partial file.
        chunks=[]
        for start in range(offset,CFG['files'][rel],64*1024*1024):
            chunk=Path(str(dest)+f'.partial.part-{start}')
            size=min(64*1024*1024,CFG['files'][rel]-start)
            if not chunk.is_file() or chunk.stat().st_size!=size:
                self.send_error(409);return
            chunks.append(chunk)
        with partial.open('ab') as f:
            for chunk in chunks:
                with chunk.open('rb') as source:shutil.copyfileobj(source,f,1024*1024)
        partial.replace(dest)
        for chunk in chunks:chunk.unlink()
        self.send_response(200);self.send_header('Content-Length','2');self.end_headers();self.wfile.write(b'OK')

    def log_message(self, *args):
        pass


ThreadingHTTPServer((CFG['server'], 18765), Handler).serve_forever()
