"""Direct CLI checks for parser, scheduling, forms and navigation repair.
Usage: python tests/engine_cycle.py /path/to/webcli
Local HTTP fixtures only; no third-party packages.
"""
import http.server, threading, json, sys, time, os, subprocess
from pathlib import Path
from terminal_driver import Terminal
if len(sys.argv) != 2:
    raise SystemExit('Usage: python tests/engine_cycle.py /path/to/webcli')
exe = str(Path(sys.argv[1]).resolve())
html = r'''<!doctype html><html><head><style>.hidden{display:none!important}</style><script>
var checks=[];checks.push(['later absent',document.getElementById('later')===null]);
try{document.readyState='complete'}catch(e){}checks.push(['readiness protected',document.readyState==='loading']);
document.write('<div id="written">written</div>');checks.push(['write synchronous',!!document.getElementById('written')]);
document.write('<script>checks.push(["nested write",true])<\/script>');
document.write('<p id=afterNested>after nested</p>');checks.push(['currentScript restored',!!document.getElementById('afterNested')]);
customElements.define('cycle-element',class extends HTMLElement{connectedCallback(){this.textContent='COMPONENT_READY'}});
</script></head><body><cycle-element></cycle-element><div id="later">later</div><div id="hidden" class="hidden" style="display:block">HIDDEN</div><div id="size" style="width:40px;height:20px">size</div><script>
checks.push(['parser custom element',document.querySelector('cycle-element').textContent==='COMPONENT_READY']);checks.push(['later present',!!document.getElementById('later')]);checks.push(['stylesheet important',getComputedStyle(document.getElementById('hidden')).display==='none']);
const sheet=new CSSStyleSheet();sheet.replaceSync('#later{visibility:hidden}');document.adoptedStyleSheets=[sheet];checks.push(['adopted stylesheet',getComputedStyle(document.getElementById('later')).visibility==='hidden']);sheet.replaceSync('#later{visibility:visible}');checks.push(['adopted stylesheet mutation',getComputedStyle(document.getElementById('later')).visibility==='visible']);
document.body.append('CYCLE_DONE '+JSON.stringify(checks));
</script></body></html>'''
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        if self.path == '/fail':
            self.send_response(503);self.send_header('Content-Length','0');self.end_headers();return
        scripts={'/module.js':"import {value} from './dep.js';await signalPromise;document.getElementById('module').textContent='MODULE_DONE='+value;",'/dep.js':'export const value=42;', '/release.js':'signal();'}
        if self.path in scripts:
            self.send_response(200);self.send_header('Content-Type','text/javascript');self.end_headers();self.wfile.write(scripts[self.path].encode());return
        if self.path == '/slow': time.sleep(3)
        if self.path.startswith('/echo'):
            body = '<html><body>'+self.path+'</body></html>'
        elif self.path == '/modules':
            body='''<html><head><script>var signal;var signalPromise=new Promise(resolve=>signal=resolve);</script></head><body><p id=module>START</p><script type=module src=/module.js></script><script defer src=/release.js></script></body></html>'''
        elif self.path == '/rollback':
            body = '''<html><body><p id=result>START</p><button onclick="fetch('/slow').then(()=>document.getElementById('result').textContent='SURVIVED')">Start</button></body></html>'''
        elif self.path == '/forms':
            body = '<html><body>'+''.join('<form action="/echo"><input type="hidden" name="tail" value="ok"><button>Submit '+str(i)+'</button></form>' for i in range(121))+'</body></html>'
        else: body = html
        self.send_response(200); self.send_header('Content-Type', 'text/html'); self.end_headers(); self.wfile.write(body.encode())
server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = 'http://127.0.0.1:'+str(server.server_port)
a = Terminal(exe, os.environ.copy()); b = Terminal(exe, os.environ.copy())
def run(terminal, *args):
    r = terminal.run([*args, '--json']); page = json.loads(r.stdout)
    assert r.returncode == 0, page
    return page
try:
    page = run(a, 'open', url+'/parser'); page = run(a, 'wait', 'text', 'CYCLE_DONE', '--timeout', '2000')
    checks = json.loads(page['text'].split('CYCLE_DONE ', 1)[1])
    assert all(value for _, value in checks), checks
    assert not page['scriptErrors'], page['scriptErrors']
    assert 'HIDDEN' not in page['text'], page
    print('PASS parser/write/style:', checks, flush=True)
    modules=run(a,'open',url+'/modules');assert 'MODULE_DONE=42' in modules['text'] and not modules['scriptErrors'],modules
    print('PASS module dependency and nonserial top-level await',flush=True)
    forms = run(a, 'open', url+'/forms'); page = run(a, 'click', forms['buttons'][-1]['ref'])
    assert 'tail=ok' in page['text'], page
    print('PASS form beyond observation form limit', flush=True)
    slow = []; thread = threading.Thread(target=lambda: slow.append(run(a, 'open', url+'/slow')))
    thread.start(); time.sleep(.15); start = time.monotonic(); run(b, 'open', url+'/forms')
    duration = time.monotonic()-start; thread.join(); assert duration < 2, duration
    print('PASS parallel instances fast request seconds:', round(duration, 3), flush=True)
    run(a,'open',url+'/rollback');run(a,'click','B1')
    failed = a.run(['open',url+'/fail','--json'])
    assert failed.returncode != 0,failed
    run(a,'wait','text','SURVIVED','--timeout','5000')
    print('PASS failed navigation preserves previous fetch',flush=True)
    subprocess.run([exe,'open',url+'/forms','--json'],capture_output=True,check=True)
    pending = subprocess.Popen([exe,'wait','text','ABSENT','--timeout','10000','--json'],stdout=subprocess.PIPE,text=True,encoding='utf-8')
    time.sleep(.5);start=time.monotonic()
    subprocess.run([exe,'close','--json'],capture_output=True,check=True)
    result=pending.communicate(timeout=2)[0];assert time.monotonic()-start<2
    assert json.loads(result)['error']=='WAIT_CANCELLED',result
    print('PASS close cancels resumable wait',flush=True)
finally:
    a.close(); b.close(); server.shutdown()
