"""Local BlazeDemo-shaped workflow; no live site or real payment required."""
import http.server
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import parse_qs


class Fixture(http.server.BaseHTTPRequestHandler):
    def reply(self, html):
        body = html.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.reply('''<a href="/vacation">Destination of the week</a>
<form method="post" action="/reserve">
<select name="fromPort"><option>Paris</option><option>Boston</option></select>
<select name="toPort"><option>Rome</option><option>London</option></select>
<input type="submit" value="Find Flights"></form>'''
                   if self.path == '/' else '<h1>Destination: Hawaii</h1>')

    def do_POST(self):
        data = parse_qs(self.rfile.read(int(self.headers['Content-Length'])).decode())
        if self.path == '/reserve':
            assert data == {'fromPort': ['Boston'], 'toPort': ['London']}, data
            # Deliberately invalid nesting used by BlazeDemo. The HTML parser
            # keeps control/form associations even when their DOM parents differ.
            self.reply('''<table><tbody><tr>
<form id="flight" method="post" action="/purchase">
<td><input type="submit" value="Choose This Flight"></td><td>Flight 43</td>
<input type="hidden" name="flight" value="43">
<input type="hidden" name="price" value="472.56"></form></tr>
<tr><form id="flight2" method="post" action="/purchase">
<td><input type="submit" value="Choose Another Flight"></td>
<input type="hidden" name="flight" value="88"></form></tr></tbody></table>
<form id="alternate"></form><p id="contracts"></p><script>
const original=document.getElementById('flight');
const submit=document.querySelector('input[type=submit]');
const hidden=document.querySelector('input[name=flight]');
const checks=[submit.form===original,hidden.form===original,
  original.elements.length===3,new FormData(original).get('flight')==='43'];
checks.push(document.getElementById('flight2').elements.length===2,
  new FormData(document.getElementById('flight2')).get('flight')==='88');
const mouse=document.createEvent('MouseEvents');
let refused=false;try{submit.dispatchEvent(mouse)}catch(e){refused=e.name==='InvalidStateError'}
checks.push(refused);
mouse.initMouseEvent('probe',true,true,window,1,2,3,4,5,false,false,false,false,0,null);
checks.push(mouse instanceof MouseEvent,mouse.type==='probe',mouse.clientX===4,!mouse.isTrusted);
const alternate=document.getElementById('alternate');
hidden.setAttribute('form','alternate');checks.push(hidden.form===alternate);
hidden.removeAttribute('form');
checks.push(hidden.form===null);
alternate.appendChild(hidden);checks.push(hidden.form===alternate);
original.appendChild(hidden);checks.push(hidden.form===original);
document.getElementById('contracts').textContent=checks.every(Boolean)?'FORM CONTRACTS PASS':'FORM CONTRACTS FAIL';
</script>''')
        elif self.path == '/purchase':
            assert data == {'flight': ['43'], 'price': ['472.56']}, data
            self.reply('''<form method="post" action="/confirmation">
<label>Name<input name="name"></label><label>Address<input name="address"></label>
<select name="cardType"><option>Visa</option><option>American Express</option></select>
<input type="submit" value="Purchase Flight"></form>''')
        else:
            assert self.path == '/confirmation', self.path
            assert data == {'name': ['WebCLI Test'], 'address': ['Test Address'],
                            'cardType': ['American Express']}, data
            self.reply('<h1>Thank you for your purchase today!</h1>')

    def log_message(self, *_args):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory() as temporary:
        env = dict(os.environ, TEMP=temporary, TMP=temporary, TMPDIR=temporary)
        exe = str(Path(sys.argv[1]).resolve())
        worker = subprocess.Popen([exe, '--worker'], env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        commands = f'''open http://127.0.0.1:{server.server_port}/
click L1
open http://127.0.0.1:{server.server_port}/
select S3 Boston
select S4 London
click B2
click B3
fill I1 "WebCLI Test"
fill I2 "Test Address"
select S5 "American Express"
click B5
wait text "Thank you" --timeout 5000
close
quit
'''
        try:
            directory = 'webcli' if os.name == 'nt' else f'webcli-{os.getuid()}'
            log = Path(temporary) / directory / 'logs/worker.log'
            deadline = time.monotonic() + 10
            while not (log.exists() and 'browser worker ready' in log.read_text(errors='replace')):
                assert worker.poll() is None and time.monotonic() < deadline, 'fixture worker did not become ready'
                time.sleep(0.01)
            result = subprocess.run([exe, '--json'], input=commands, env=env,
                                    capture_output=True, text=True, encoding='utf8', timeout=30)
        finally:
            worker.terminate()
            worker.wait(timeout=10)
        pages = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        assert result.returncode == 0 and not any(p.get('error') for p in pages), pages
        assert 'Hawaii' in pages[1]['text'], pages[1]
        assert 'FORM CONTRACTS PASS' in pages[5]['text'], pages[5]
        assert pages[6]['url'].endswith('/purchase'), pages[6]
        assert 'Thank you' in pages[10]['text'] and 'Thank you' in pages[11]['text'], pages
        assert not any(p.get('scriptErrors') for p in pages), pages
        print('PASS BlazeDemo-shaped links, selects, table forms, fill and confirmation')
finally:
    server.shutdown()
    server.server_close()
