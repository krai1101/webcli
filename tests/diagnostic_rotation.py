"""Verify bounded diagnostic retention using an isolated temporary profile."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time


class Fixture(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b'<p>Diagnostic fixture</p><script>throw new Error("rotation fixture");</script>'
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


def main():
    executable = str(Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    with tempfile.TemporaryDirectory() as temporary:
        environment = dict(os.environ, TEMP=temporary, TMP=temporary, TMPDIR=temporary)
        root = Path(temporary) / ('webcli' if os.name == 'nt' else f'webcli-{os.getuid()}')
        logs = root / 'logs'
        logs.mkdir(parents=True)
        path = logs / 'runtime.jsonl'
        first, second = logs / 'runtime.jsonl.1', logs / 'runtime.jsonl.2'
        limit = 8 * 1024 * 1024

        def write_fixture(timestamp, size=None):
            record = (json.dumps({'timestamp_unix_ms': timestamp}, separators=(',', ':')) + '\n').encode()
            path.write_bytes(record + (b'\n' * (size - len(record)) if size else b''))

        def append():
            result = subprocess.run(
                [executable, 'open', f'http://127.0.0.1:{server.server_port}/', '--json'],
                env=environment, capture_output=True, text=True, encoding='utf-8', timeout=20,
            )
            assert result.returncode == 0 and not result.stderr, result
            page = json.loads(result.stdout)
            assert any('rotation fixture' in message for message in page['scriptErrors']), page

        try:
            write_fixture(1)
            append()
            assert json.loads(first.read_text().splitlines()[0])['timestamp_unix_ms'] == 1
            write_fixture(int(time.time() * 1000), limit)
            append()
            assert first.stat().st_size == limit and second.exists()
            write_fixture(int(time.time() * 1000), limit)
            append()
            assert first.stat().st_size == limit and second.stat().st_size == limit
            write_fixture(int(time.time() * 1000), limit + 1)
            append()
            assert all(item.stat().st_size <= limit for item in [path, first, second])
            assert len(list(logs.glob('runtime.jsonl*'))) == 3
            assert sum(item.stat().st_size for item in [path, first, second]) <= 3 * limit
            print('PASS: seven-day rotation, size rotation, two backups, oversized legacy log cleanup.')
        finally:
            subprocess.run([executable, 'close'], env=environment, capture_output=True, timeout=20)
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == '__main__':
    main()
