"""Native fetch isolation and corrected engine lifetimes; local fixtures only."""
import http.server
import json
from pathlib import Path
import subprocess
import sys
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    writes = 0

    def log_message(self, *args):
        pass

    def reply(self, body, kind="text/plain", headers=None, status=200):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_OPTIONS(self):
        if self.path == "/allowed":
            self.reply(b"", headers={"Access-Control-Allow-Origin": self.headers["Origin"],
                                     "Access-Control-Allow-Credentials": "true",
                                     "Access-Control-Allow-Methods": "PUT",
                                     "Access-Control-Allow-Headers": "x-test"})
        else:
            self.reply(b"")

    def do_PUT(self):
        Handler.writes += 1
        self.reply(b"written", headers={"Access-Control-Allow-Origin": self.headers["Origin"],
                                       "Access-Control-Allow-Credentials": "true"})

    def do_GET(self):
        if self.path in ("/psl-public", "/psl-private", "/psl-valid"):
            domain = {"/psl-public": "co.uk", "/psl-private": "github.io", "/psl-valid": "foo.co.uk"}[self.path]
            name = "psl_safe" if self.path == "/psl-valid" else "psl_unsafe"
            self.reply(b"", headers={"Set-Cookie": f"{name}=1; Domain={domain}; Path=/"})
        elif self.path == "/page":
            script = r"""
const checks=[];const check=(name,value)=>checks.push([name,!!value]);
(async()=>{
const native=await __webcliFetch('/secret');
check('native cookie header hidden',!('set-cookie' in native.headers));
check('HttpOnly cookie hidden',!document.cookie.includes('private'));
try{await __webcliFetch('/echo',{headers:{'x-test':'before\u0000after'}});check('native header controls denied',false)}catch(e){check('native header controls denied',true)}
try{await __webcliFetch(remote+'/denied');check('native cors denial',false)}catch(e){check('native cors denial',true)}
const allowed=await fetch(remote+'/allowed');check('allowed cors body',await allowed.text()==='public');
check('cors header exposure',allowed.headers.get('x-hidden')===null && allowed.headers.get('x-visible')==='visible');
try{await fetch(remote+'/wildcard',{credentials:'include'});check('credential wildcard denied',false)}catch(e){check('credential wildcard denied',true)}
const opaque=await __webcliFetch(remote+'/denied',{mode:'no-cors'});check('native opaque',opaque.opaque && !opaque.bytes && !opaque.headers && !opaque.url);
const publicOpaque=await fetch(remote+'/denied',{mode:'no-cors'});check('public opaque',publicOpaque.status===0 && publicOpaque.type==='opaque');
try{await fetch(remote+'/denied',{method:'PUT',headers:{'x-test':'yes'}});check('preflight denied',false)}catch(e){check('preflight denied',true)}
const write=await fetch(remote+'/allowed',{method:'PUT',headers:{'x-test':'yes'},credentials:'include'});check('preflight allowed',await write.text()==='written');
const forged=await __webcliFetch(remote+'/echo',{headers:{Origin:'https://forged.invalid',Cookie:'fake=1'},credentials:'omit'});
check('forbidden request headers',JSON.parse(new TextDecoder().decode(forged.bytes)).origin===location.origin && !JSON.parse(new TextDecoder().decode(forged.bytes)).cookie);
window.__webcliCurrentURL=remote+'/foreign';
document.body.append('SECURITY_DONE '+JSON.stringify(checks));
})().catch(e=>document.body.append('SECURITY_FAILURE '+e+' '+e.stack));
"""
            self.reply(("<html><body><script>const remote=" + json.dumps(remote) + ";" + script +
                        "</script></body></html>").encode(), "text/html")
        elif self.path == "/secret":
            self.reply(b"ok", headers={"Set-Cookie": "private=hidden; HttpOnly; Path=/"})
        elif self.path == "/echo":
            self.reply(json.dumps({"origin": self.headers.get("Origin"), "cookie": self.headers.get("Cookie")}).encode(),
                       headers={"Access-Control-Allow-Origin": "*"})
        elif self.path in ("/allowed", "/wildcard"):
            self.reply(b"public", headers={"Access-Control-Allow-Origin": "*",
                                           "Access-Control-Expose-Headers": "x-visible",
                                           "X-Visible": "visible", "X-Hidden": "hidden"})
        elif self.path == "/lifetimes":
            self.reply(b'''<html><body><script>window.moduleRuns=0;window.imageReady=false;window.loadReady=false;
const img=new Image();img.onload=()=>{window.imageReady=true};img.src='/image';window.onload=()=>{window.loadReady=window.imageReady};
</script><script type="module" src="/module.js"></script><script type="module" src="/module.js"></script>
<script>setTimeout(()=>document.body.append('LIFETIME_DONE '+JSON.stringify([moduleRuns,loadReady,imageReady])),600);</script></body></html>''', "text/html")
        elif self.path == "/module.js":
            self.reply(b"window.moduleRuns++;export const value=1;", "text/javascript")
        elif self.path == "/image":
            time.sleep(0.2)
            self.reply(b"image", "image/png")
        else:
            self.reply(b"private")


exe = str(Path(sys.argv[1]).resolve())
servers = [http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) for _ in range(2)]
for server in servers:
    threading.Thread(target=server.serve_forever, daemon=True).start()
origin = f"http://127.0.0.1:{servers[0].server_port}"
remote = f"http://127.0.0.1:{servers[1].server_port}"
try:
    suffix_exe = Path(exe).with_name("webcli_suffix_tests" + Path(exe).suffix)
    suffix_result = subprocess.run([str(suffix_exe), str(servers[0].server_port)], capture_output=True, text=True, timeout=15)
    assert suffix_result.returncode == 0, (suffix_result.stdout, suffix_result.stderr)
    print("PASS HTTP cookies reject public/private suffixes and accept a valid domain")
    result = subprocess.run([exe, "--json"], input=f"open {origin}/page\nwait text SECURITY_DONE --timeout 10000\nobserve\nopen {origin}/lifetimes\nwait text LIFETIME_DONE --timeout 5000\nquit\n",
                            text=True, encoding="utf-8", capture_output=True, timeout=35)
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stderr)
    outputs = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(outputs) == 5, outputs
    state = outputs[1]
    checks = json.loads(state["text"].split("SECURITY_DONE ", 1)[1])
    assert all(value for _, value in checks), checks
    assert Handler.writes == 1, Handler.writes
    assert outputs[2]["url"].startswith(origin), outputs[2]
    assert not state["scriptErrors"], state
    life = outputs[-1]
    assert "LIFETIME_DONE [1,true,true]" in life["text"] and not life["scriptErrors"], life
    print(f"PASS {len(checks)} network security assertions, denied preflight never dispatched, native origin guard, module identity and image load ordering")
finally:
    for server in servers:
        server.shutdown()
