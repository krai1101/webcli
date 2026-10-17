"""Check diagnostics for failures handled by page JavaScript, without credentials."""
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
        body = b'''<input type="password" aria-label="Password">
<button onclick="fetch('/denied?token=QUERY_SECRET',{method:'POST',body:'BODY_SECRET'}).catch(()=>{})">Request</button>
<button onclick="fetch('/abort?token=QUERY_SECRET',{method:'POST'}).catch(()=>{})">Abort</button>
<script>console.warn('Formatted %s %d', 'diagnostic', 7)</script>'''
        self.send_response(200)
        self.send_header('Content-Type', 'text/html')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length', 0)))
        if self.path.startswith('/abort'):
            self.close_connection = True
            return
        self.send_response(403)
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, *_args):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory() as temporary:
        environment = dict(os.environ, TEMP=temporary, TMP=temporary, TMPDIR=temporary)
        commands = f'''open http://127.0.0.1:{server.server_port}/
fill I1 FIELD_SECRET
click B1
wait idle --timeout 5000
click B2
wait idle --timeout 5000
observe --debug
observe --debug
close
quit
'''
        executable = str(Path(sys.argv[1]).resolve())
        # Own the fixture's resident worker, so its files are closed before cleanup.
        worker = subprocess.Popen([executable, '--worker'], env=environment,
                                  stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
        try:
            # Do not let the CLI auto-start a second, unowned worker while the
            # fixture's worker is still starting (that process can keep logs open).
            directory = 'webcli' if os.name == 'nt' else f'webcli-{os.getuid()}'
            worker_log = Path(temporary) / directory / 'logs/worker.log'
            deadline = time.monotonic() + 10
            while not (worker_log.exists() and
                       'browser worker ready' in worker_log.read_text(errors='replace')):
                assert worker.poll() is None, 'Fixture worker exited before becoming ready'
                assert time.monotonic() < deadline, 'Fixture worker did not become ready'
                time.sleep(.01)
            result = subprocess.run([executable, '--json'], input=commands,
                                    text=True, encoding='utf8', capture_output=True,
                                    env=environment, timeout=30)
        finally:
            worker.terminate()
            worker.wait(timeout=10)
        pages = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        assert result.returncode == 0 and not any(p.get('error') for p in pages), pages
        observed = next(p for p in reversed(pages) if p.get('diagnostics'))
        assert observed['scriptErrors'] == ['console.warn: Formatted diagnostic 7'], observed['scriptErrors']
        diagnostics = observed['diagnostics']
        messages = [entry['message'] for entry in diagnostics['trace']]
        assert any('click node=' in m and 'handlers=1' in m for m in messages), messages
        assert any('HTTP 403' in m for m in messages), messages
        assert any(m.startswith('failed #') for m in messages), messages
        assert any('click B1 returned;' in m and 'DOM=unchanged' in m for m in messages), messages
        assert diagnostics['pending']['requests'] == 0, diagnostics
        log = Path(diagnostics['logPath']).read_text(encoding='utf8')
        encoded = json.dumps(diagnostics['trace']) + log
        assert not any(secret in encoded for secret in ('QUERY_SECRET', 'BODY_SECRET', 'FIELD_SECRET')), encoded
        records = [json.loads(line) for line in log.splitlines()]
        trace_records = [record for record in records if 'elapsedMs' in record]
        assert len(trace_records) == len(diagnostics['trace']), 'Repeated debug duplicated trace entries'
        assert len(records) == len(trace_records) + 1, 'Warning log entry missing or duplicated'
        print('PASS runtime diagnostics: handled HTTP/network failures, event delivery, unchanged DOM, redaction, log deduplication')
finally:
    server.shutdown()
