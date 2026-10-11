"""Focused buffered API, document lifecycle and observation contract checks.

Usage: python tests/runtime_contracts.py /path/to/webcli
No external websites or third-party Python dependencies are required.
"""

import http.server, threading, json, subprocess, sys, time
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit('Usage: python tests/runtime_contracts.py /path/to/webcli')
exe = str(Path(sys.argv[1]).resolve())
script = r"""
(async()=>{
const passed=[],failed=[];
const check=(name,value)=>{(value?passed:failed).push(name)};
const test=async(name,action)=>{try{await action()}catch(e){failed.push(name+': '+e+' '+e.stack)}};
await test('body',async()=>{
const bytes=new Uint8Array([0,255,65,128]);const response=new Response(bytes);const copy=response.clone();
check('binary response',JSON.stringify([...new Uint8Array(await response.arrayBuffer())])==='[0,255,65,128]');
check('body used',response.bodyUsed);try{await response.text();check('consumption rejection',false)}catch(e){check('consumption rejection',e instanceof TypeError)}
check('clone independent',JSON.stringify([...await copy.bytes()])==='[0,255,65,128]');
check('error clone',Response.error().clone().status===0);
const request=new Request('/echo',{method:'POST',body:bytes});const clone=request.clone();
check('request clone',!request.bodyUsed && !clone.bodyUsed);
const result=await fetch(request);check('request consumed',request.bodyUsed);check('binary transport',JSON.stringify([...await result.bytes()])==='[0,255,65,128]');
const redirect=await fetch('/redirect',{redirect:'manual'});check('manual redirect',redirect.status===0 && redirect.type==='opaqueredirect');
});
await test('forms',async()=>{
const host=document.createElement('div');host.innerHTML='<form id="contract"><fieldset disabled><legend><input name="legend" value="yes"></legend><input name="blocked" value="no"></fieldset><input readonly name="read" value="yes"><input name="repeat" value="a"><input name="repeat" value="b"><button name="go" value="yes">Submit</button></form><input form="contract" name="external" value="yes">';document.body.appendChild(host);
const form=host.querySelector('form'),data=new FormData(form);check('legend',data.get('legend')==='yes' && data.get('blocked')===null);check('readonly successful',data.get('read')==='yes');check('external owner',data.get('external')==='yes');check('repeated fields',data.getAll('repeat').join(',')==='a,b');
const fileValue=new File(['test'],'identity.txt');data.append('identity',fileValue);check('File identity',data.get('identity')===fileValue);
data.append('binary',new Blob([new Uint8Array([0,255,65])]),'sample.bin');const result=await fetch('/multipart',{method:'POST',body:data});check('multipart bytes',(await result.json()).valid);
const live=form.getElementsByTagName('input'),staticList=form.querySelectorAll('input'),old=live.length;form.appendChild(document.createElement('input'));check('live/static',live.length===old+1 && staticList.length===old);
const radios=document.createElement('div');radios.innerHTML='<input type="radio" name="group" checked><input type="radio" name="group">';host.appendChild(radios);const [first,second]=radios.querySelectorAll('input');second.onclick=e=>e.preventDefault();second.click();check('cancelled radio group',first.checked && !second.checked);
host.remove();
});
await test('events',async()=>{
const target=new EventTarget(),controller=new AbortController();let count=0;const fn=()=>count++;
target.addEventListener('x',fn,{signal:controller.signal});controller.signal.dispatchEvent(new Event('abort'));controller.signal.addEventListener('abort',e=>e.stopImmediatePropagation());controller.abort();target.dispatchEvent(new Event('x'));check('listener signal',count===0);
target.addEventListener('x',fn,{once:true});target.dispatchEvent(new Event('x'));target.dispatchEvent(new Event('x'));check('listener once',count===1);
target.addEventListener('p',e=>e.preventDefault(),{passive:true});check('passive cancellation',target.dispatchEvent(new Event('p',{cancelable:true})));
let removed=0;const second=()=>removed++;target.addEventListener('r',()=>target.removeEventListener('r',second));target.addEventListener('r',second);target.dispatchEvent(new Event('r'));check('removed listener',removed===0);
});
await test('timers',async()=>{const pending=Array.from({length:160},()=>setTimeout(()=>{},60000));pending.forEach(clearTimeout);check('ordinary timer fan-out',pending.length===160);let fired=0;const id=setInterval(()=>fired++,1);clearTimeout(id);await new Promise(resolve=>setTimeout(resolve,10));check('timer clear interoperability',fired===0)});
await test('xhr',async()=>{
const xhr=new XMLHttpRequest();xhr.open('GET','/binary');xhr.responseType='arraybuffer';await new Promise((resolve,reject)=>{xhr.onload=resolve;xhr.onerror=reject;xhr.send()});check('xhr binary',JSON.stringify([...new Uint8Array(xhr.response)])==='[0,255,65,128]');
const abort=new XMLHttpRequest(),events=[];abort.open('GET','/binary');abort.onreadystatechange=()=>{if(abort.readyState===2)abort.abort()};abort.onabort=()=>events.push('abort');await new Promise(resolve=>{abort.onloadend=resolve;abort.send()});check('xhr abort delivery',abort.readyState===0 && events.join(',')==='abort');
});
await test('components',async()=>{
check('Node type constants',Node.COMMENT_NODE===8 && Node.ELEMENT_NODE===1 && document.DOCUMENT_NODE===9 && document.createComment('x').COMMENT_NODE===8);
const root=document.createElement('div'),left=document.createElement('span'),right=document.createComment('right');root.append(left,right);
check('tree relation order',root.hasChildNodes() && !left.hasChildNodes() && left.compareDocumentPosition(right)===4 && right.compareDocumentPosition(left)===2 && root.compareDocumentPosition(left)===20 && left.compareDocumentPosition(root)===10);
const detached=document.createElement('div');check('disconnected relation',Boolean(root.compareDocumentPosition(detached)&33) && (root.compareDocumentPosition(detached)&6)!==(detached.compareDocumentPosition(root)&6));
check('pre element brand',document.createElement('pre') instanceof HTMLPreElement);
check('heading interface brand',document.createElement('h2') instanceof HTMLHeadingElement && !(document.createElement('div') instanceof HTMLHeadingElement));
const tokenNode=document.createElement('div');tokenNode.className='a a b';const tokens=tokenNode.classList;
check('live token iteration',tokens instanceof DOMTokenList && tokens===tokenNode.classList && [...tokens].join(',')==='a,b' && tokens[1]==='b');
tokenNode.className='c';check('token live updates',tokens.length===1 && tokens.item(0)==='c');
tokenNode.classList='c';check('token value forwarding',tokens.value==='c' && tokenNode.className==='c');
try{tokens.add('valid','two tokens');check('token validation atomicity',false)}catch(e){check('token validation atomicity',e.name==='InvalidCharacterError' && tokens.value==='c')}
tokens.add('d','e');tokens.replace('c','e');check('token replacement and force',tokens.value==='d e' && tokens.toggle('d',true) && !tokens.toggle('d',false) && [...tokens.entries()][0][1]==='e');
const calls=[];
class ContractElement extends HTMLElement {
 static get observedAttributes(){return ['data-value']}
 connectedCallback(){calls.push('connected')}
 disconnectedCallback(){calls.push('disconnected')}
 attributeChangedCallback(){calls.push('attribute')}
}
customElements.define('contract-element',ContractElement);
const fragment=document.createDocumentFragment(),node=document.createElement('contract-element');fragment.appendChild(node);
const mutations=[];const observer=new MutationObserver(records=>mutations.push(records.length));observer.observe(document.body,{childList:true});
document.body.appendChild(fragment);check('fragment component connection',calls.join(',')==='connected');
node.setAttribute('data-value','1');check('attribute reaction',calls.join(',')==='connected,attribute');
document.body.appendChild(node);check('component move',calls.join(',')==='connected,attribute,disconnected,connected');
node.remove();check('component removal',calls.at(-1)==='disconnected');
await Promise.resolve();check('mutation checkpoint',mutations.length===1);observer.disconnect();
const parent=document.createElement('div'),child=document.createElement('span');const children=parent.children,nodes=parent.childNodes;
check('collection interfaces',children instanceof HTMLCollection && nodes instanceof NodeList && !Array.isArray(children));
children.cacheMarker=42;check('collection expandos',children.cacheMarker===42 && Object.keys(children).includes('cacheMarker'));delete children.cacheMarker;
parent.appendChild(child);check('live children',children.length===1 && nodes.length===1);parent.removeChild(child);check('live removal',children.length===0 && nodes.length===0);
parent.appendChild(child);const snapshot=parent.querySelectorAll('span');parent.removeChild(child);
check('static NodeList interface',snapshot instanceof NodeList && snapshot.length===1 && snapshot.item(0)===child && [...snapshot][0]===child);
const anchor=document.createElement('a');anchor.href='/route?q=one#section';
check('anchor URL components',anchor.pathname==='/route' && anchor.search==='?q=one' && anchor.hash==='#section' && anchor.origin===location.origin && document.location===location);
anchor.search='q=two';check('anchor query mutation',anchor.href.includes('/route?q=two#section'));
const styled=document.createElement('div');styled.setAttribute('style','opacity:.55;float:left');
check('CSSOM feature detection',styled.style.opacity==='0.55' && styled.style.cssFloat==='left');
const computed=getComputedStyle(styled);styled.style.opacity='.75';
check('live computed style identity',computed===getComputedStyle(styled) && computed.opacity==='0.75');
const measured=document.createElement('div');measured.style.width='100px';measured.textContent='example';document.body.appendChild(measured);
const rect=measured.getBoundingClientRect();
check('layout remains unmeasured',rect instanceof DOMRect && rect.width===0 && rect.height===0 && measured.getClientRects().length===0);
styled.appendChild(measured);styled.style.display='none';document.body.appendChild(styled);check('hidden style',getComputedStyle(styled).display==='none');styled.remove();
const large=document.createElement('div');large.innerHTML='<!--'+'x'.repeat(70000)+'--><span>fragment</span>';check('fetched HTML fragment',large.querySelector('span').textContent==='fragment');
try{new HTMLCollection();check('collection illegal constructor',false)}catch(e){check('collection illegal constructor',e instanceof TypeError)}
const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),circle=document.createElementNS('http://www.w3.org/2000/svg','circle');svg.appendChild(circle);check('namespace collection',svg.getElementsByTagNameNS('http://www.w3.org/2000/svg','circle').length===1);
});
await test('cancellation',async()=>{const c=new AbortController();const p=fetch('/slow',{signal:c.signal});c.abort('cancelled');try{await p;check('fetch abort',false)}catch(e){check('fetch abort',e==='cancelled')}});
document.body.textContent='CONTRACT_DONE '+JSON.stringify({passed,failed});
})().catch(e=>{document.body.textContent='CONTRACT_DONE fatal '+e+' '+e.stack});
"""


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def response(self, body, kind="text/html", status=200, headers=None):
        self.send_response(status)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_GET(self):
        if self.path == "/parser-checkpoint":
            self.response(b"""<html><body><script>window.parserTimer=false;window.parserMicrotask=false;setTimeout(()=>window.parserTimer=true,0);Promise.resolve().then(()=>window.parserMicrotask=true);</script><script>document.body.appendChild(document.createTextNode(!window.parserTimer && window.parserMicrotask ? 'PARSER_CHECKPOINT_OK' : 'PARSER_CHECKPOINT_FAILED'));</script></body></html>""")
        elif self.path == "/queued-scripts":
            self.response(b"""<html><body><script>let loaded=0;for(let i=0;i<80;++i){const script=document.createElement('script');script.src='/queued-script.js?i='+i;script.onload=()=>{if(++loaded===80)document.body.appendChild(document.createTextNode('QUEUE_TRANSFER_OK'))};document.body.appendChild(script);}</script></body></html>""")
        elif self.path.startswith("/queued-script.js?"):
            time.sleep(2.05)
            self.response(b"void 0;", "text/javascript")
        elif self.path == "/many-inline":
            self.response(("<html><body><script>window.inlineCount=0</script>" +
                           "<script>++window.inlineCount</script>" * 160 +
                           '<script src="/inline-done.js"></script></body></html>').encode())
        elif self.path == "/inline-done.js":
            self.response(b"document.body.textContent='INLINE_DONE '+window.inlineCount", "text/javascript")
        elif self.path == "/encoding-form":
            self.response(b'<html><body><form action="/form-result?old=obsolete#part"><input name="q"><select name="language"><option value="en">English</option><option value="fr">French</option></select><button>Search</button></form></body></html>')
        elif self.path.startswith("/form-result"):
            self.response(("<html><body>FORM_RESULT " + self.path + "</body></html>").encode())
        elif self.path == "/binary":
            self.response(bytes([0, 255, 65, 128]), "application/octet-stream")
        elif self.path == "/redirect":
            self.response(b"", "text/plain", 302, {"Location": "/binary"})
        elif self.path == "/empty":
            self.response(b"<html><title>Empty</title><body></body></html>")
        elif self.path == "/challenge":
            self.response(
                b"<html><title>Access challenge</title><body></body></html>",
                status=202,
                headers={"x-amzn-waf-action": "challenge"},
            )
        elif self.path == "/clean":
            self.response(
                b'<html><title>Clean document</title><body>CLEAN<button onclick="history.back()">Back</button></body></html>'
            )
        elif self.path == "/lifecycle":
            self.response(
                b"""<html><title>Lifecycle</title><body><p id="state">START</p><button onclick="setTimeout(()=>document.getElementById('state').textContent='TIMER',500);fetch('/slow').then(()=>document.getElementById('state').textContent='FETCH').catch(()=>document.getElementById('state').textContent='ABORTED');location.href='/clean'">Navigate</button></body></html>"""
            )
        elif self.path == "/slow":
            time.sleep(2)
            self.response(b"done", "text/plain")
        else:
            self.response(
                (
                    "<html><head><title>Runtime contracts</title></head><body>Running<script>"
                    + script
                    + "</script></body></html>"
                ).encode()
            )

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        if self.path == "/multipart":
            valid = (
                b"\x00\xffA" in body
                and b'name="repeat"' in body
                and b'filename="sample.bin"' in body
                and self.headers.get("Content-Type", "").startswith(
                    "multipart/form-data; boundary="
                )
            )
            self.response(json.dumps({"valid": valid}).encode(), "application/json")
        else:
            self.response(body, "application/octet-stream")


http.server.ThreadingHTTPServer.request_queue_size = 128
server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
result = subprocess.run(
    [exe, "--json"],
    input=f"open http://127.0.0.1:{server.server_port}/\nwait text CONTRACT_DONE --timeout 5000\nquit\n",
    text=True,
    encoding="utf-8",
    capture_output=True,
    timeout=25,
)
if result.stderr:
    print(result.stderr)
rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
last = rows[-1]
assert "CONTRACT_DONE " in last["text"], last
summary = json.loads(last["text"].split("CONTRACT_DONE ", 1)[1])
assert not summary["failed"], summary
assert not last["scriptErrors"], last["scriptErrors"]
print("PASS", len(summary["passed"]))
# Same terminal owns a persistent worker, across separate command processes.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from terminal_driver import Terminal
import os

terminal = Terminal(exe, os.environ.copy())


def run(*args):
    result = terminal.run([*args, "--json"])
    page = json.loads(result.stdout)
    assert result.returncode == 0, page
    return page


try:
    run("open", f"http://127.0.0.1:{server.server_port}/queued-scripts")
    queued = run("wait", "text", "QUEUE_TRANSFER_OK", "--timeout", "15000")
    assert not queued["scriptErrors"] and "QUEUE_TRANSFER_OK" in queued["text"], queued
    print("PASS queued transfers retain their own timeout after waiting for a slot")
    parser = run("open", f"http://127.0.0.1:{server.server_port}/parser-checkpoint")
    assert "PARSER_CHECKPOINT_OK" in parser["text"] and not parser["scriptErrors"], parser
    print("PASS parser microtasks precede next script; timers wait for event-loop turn")
    inline = run("open", f"http://127.0.0.1:{server.server_port}/many-inline")
    assert "INLINE_DONE 160" in inline["text"] and not inline["scriptErrors"], inline
    run("open", f"http://127.0.0.1:{server.server_port}/encoding-form")
    run("fill", "I1", "a b~*+")
    selection = run("select", "S1", "fr")
    assert selection["fields"][-1]["value"] == "fr", selection
    form_result = run("keyboard", "I1", "Enter")
    assert "form_result /form-result?q=a+b%7e*%2b" in form_result["text"].lower(), form_result
    assert "&language=fr" in form_result["text"], form_result
    assert form_result["url"].endswith("#part"), form_result["url"]
    print("PASS inline scripts preserve request slots; native form serialization/query replacement")
    empty = run("open", f"http://127.0.0.1:{server.server_port}/empty")
    assert empty["outcome"]["status"] == "empty", empty
    challenge = run("open", f"http://127.0.0.1:{server.server_port}/challenge")
    assert (
        challenge["outcome"]["status"] == "access-challenge"
        and challenge["outcome"]["evidence"]
    ), challenge
    initial = run("open", f"http://127.0.0.1:{server.server_port}/lifecycle")
    clean = run("click", "B1")
    assert (
        clean["title"] == "Clean document"
        and clean["documentId"] != initial["documentId"]
    ), clean
    time.sleep(0.7)
    run("wait", "text", "CLEAN", "--timeout", "200")
    assert "CLEAN" in run("observe")["text"]
    restored = run("click", "B1")
    assert restored["documentId"] == initial["documentId"], restored
    assert "TIMER" not in restored["text"] and "FETCH" not in restored["text"], restored
    restored = run("wait", "text", "TIMER", "--timeout", "2000")
    assert not restored["scriptErrors"], restored
    stale = terminal.run(["click", "B1", "--document", clean["documentId"], "--json"])
    assert json.loads(stale.stdout)["error"] == "STALE_DOCUMENT", stale.stdout
    print(
        "PASS lifecycle: navigation cancellation, isolated document IDs, timer suspension/history resume, stale document guard"
    )
finally:
    terminal.close()

normal = subprocess.run(
    [exe],
    input=f"open http://127.0.0.1:{server.server_port}/empty\nopen http://127.0.0.1:{server.server_port}/challenge\nquit\n",
    text=True,
    encoding="utf-8",
    capture_output=True,
    timeout=15,
)
server.shutdown()
assert normal.returncode == 0 and not normal.stderr, (normal.returncode, normal.stderr)
assert (
    "Empty document:" in normal.stdout and "Access challenge:" in normal.stdout
), normal.stdout
print("PASS empty/challenge explanations in normal text mode")
