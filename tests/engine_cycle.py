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
    origin_requests = []
    hydrate_release = threading.Event()
    hydrate_done = threading.Event()
    fill_loads = 0
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length',0)))
        Handler.origin_requests.append((self.command,self.path,self.headers.get('Origin')))
        if self.path == '/origin-redirect':
            self.send_response(302);self.send_header('Location','/origin-result');self.end_headers();return
        self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(b'<body>POST_ORIGIN_READY</body>')
    def do_GET(self):
        if self.path == '/indexed-frame-child':
            body='<body>INDEXED_CHILD_READY</body>'
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/indexed-frames':
            body='''<body><iframe src=/indexed-frame-child onload="document.getElementById('loaded').textContent='INDEXED_PARENT_READ:'+window[0].document.body.textContent"></iframe>
<p id=parsed></p><p id=loaded></p><script>
document.getElementById('parsed').textContent=window[0]===document.querySelector('iframe').contentWindow?'INDEXED_PARSER_READY':'INDEXED_PARSER_FAIL';
</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/checked-activation':
            body='''<body><input type=checkbox aria-label=Accept><input type=checkbox aria-label=Cancelled onclick="event.preventDefault()">
<input type=radio name=plan aria-label=Basic checked><input type=radio name=plan aria-label=Pro>
<input aria-label="Focus marker"><p id=focus></p>
<p id=events>EVENTS=</p><script>document.addEventListener('focusin',event=>{document.getElementById('focus').textContent='FOCUS='+event.target.getAttribute('aria-label')});
const events=[];for(const type of ['click','input','change'])
document.querySelector('input').addEventListener(type,()=>{events.push(type);document.getElementById('events').textContent='EVENTS='+events.join(',')});</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/frame-worker.js':
            self.send_response(200);self.send_header('Content-Type','application/javascript');self.end_headers()
            self.wfile.write(b'onmessage=e=>setTimeout(()=>postMessage(e.data),0)');return
        if self.path == '/frame-worker-child':
            body='''<body><p id=result>CHILD_INITIAL</p><script>
const worker=new Worker('/frame-worker.js');
onmessage=e=>{document.getElementById('result').textContent='CHILD_PENDING';worker.postMessage(e.data)};
worker.onmessage=e=>{document.getElementById('result').textContent='CHILD_'+e.data;parent.postMessage(e.data,location.origin)};
</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/frame-worker':
            body='''<body><form><input aria-label=Question><button>Ask</button></form>
<p id=result>PARENT_INITIAL</p><iframe src=/frame-worker-child></iframe><script>
document.querySelector('form').onsubmit=e=>{e.preventDefault();document.getElementById('result').textContent='PARENT_PENDING';
document.querySelector('iframe').contentWindow.postMessage(document.querySelector('input').value,location.origin)};
onmessage=e=>{document.getElementById('result').textContent='PARENT_'+e.data};
</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/opacity.css':
            self.send_response(200);self.send_header('Content-Type','text/css');self.end_headers()
            self.wfile.write(b'.external {opacity:0!important}');return
        if self.path == '/opacity':
            body='''<head><link rel=stylesheet href=/opacity.css></head><body>
<input aria-label="Transparent password" type=password style="opacity:0;pointer-events:none">
<div style="opacity:0"><h2>HIDDEN_HEADING</h2><span style="visibility:visible;opacity:1">HIDDEN_TEXT</span>
<input aria-label="Transparent ancestor" style="opacity:1"></div>
<input class=external aria-label="External opacity" style="opacity:1">
<input aria-label="Percent zero" style="opacity:0%">
<input aria-label="Invalid declaration" style="opacity:0;opacity:invalid">
<input id=half aria-label="Half-visible" style="opacity:50%">
<input id=winner class=external aria-label="Inline winner" style="opacity:1!important">
<input id=restored aria-label="Restored opacity" style="opacity:0">
<button onclick="document.getElementById('restored').style.opacity='1'">Reveal</button>
<p id=result></p><script>
const ok=getComputedStyle(document.getElementById('half')).opacity==='0.5'&&
getComputedStyle(document.getElementById('winner')).opacity==='1'&&
getComputedStyle(document.querySelector('.external')).opacity==='0';
document.getElementById('result').textContent=ok?'OPACITY_PASS':'OPACITY_FAIL';
</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/fill-events':
            Handler.fill_loads+=1
            body='''<body><form><input id=edit aria-label=Edit><button type=button>Blur</button></form>
<p id=report>EVENTS=</p><script>
const events=[],edit=document.getElementById('edit');
const report=type=>{events.push(type);document.getElementById('report').textContent='EVENTS='+events.join(',')};
edit.addEventListener('input',()=>report('input'));edit.addEventListener('change',()=>report('change'));
document.querySelector('form').addEventListener('submit',event=>{event.preventDefault();report('submit')});
</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/hydrate-release':
            Handler.hydrate_release.wait(10)
            self.send_response(200);self.end_headers();self.wfile.write(b'ready');return
        if self.path == '/hydrate-done':
            Handler.hydrate_done.set()
            self.send_response(200);self.end_headers();return
        if self.path.startswith('/hydrate-field/'):
            mode=self.path.rsplit('/',1)[-1]
            replacement='<input aria-label="Email" id="client" name="client-email">'
            if mode=='ambiguous': replacement+=replacement.replace('client','other')
            if mode=='password': replacement=replacement.replace('<input','<input type="password"')
            update='document.getElementById("fields").innerHTML='+json.dumps(replacement)+';fetch("/hydrate-done")'
            if mode=='detached':
                update='''{let detached=new DOMParser().parseFromString('<body>','text/html');
detached.adoptNode(document.querySelector('input'));}
setTimeout(()=>{for(let i=0;i<40;i++)new DOMParser().parseFromString('<body>','text/html');
document.getElementById('fields').innerHTML=Array.from({length:100},(_,i)=>'<input aria-label="Other '+i+'">').join('');
fetch('/hydrate-done')},0)'''
            body=('<body><div id="fields"><input aria-label="Email" id="server" name="server-email"></div><script>'
                  'fetch("/hydrate-release").then(()=>{'+update+'});</script></body>').encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body);return
        if self.path in ('/ref-frame-a','/ref-frame-b'):
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers()
            self.wfile.write(b'<body><input aria-label="Email" id="same" name="same"></body>');return
        if self.path == '/ref-frame':
            body='''<body><iframe id=child src="/ref-frame-a"></iframe><script>
fetch('/hydrate-release').then(()=>{const child=document.getElementById('child');
child.addEventListener('load',()=>fetch('/hydrate-done'),{once:true});child.src='/ref-frame-b'});
</script></body>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path.startswith('/hydrate-action/'):
            mode=self.path.rsplit('/',1)[-1]
            initial='<a href="/echo-original">Visit</a>'
            replacement=initial if mode=='link' else '<a href="/echo-changed">Visit</a>'
            if mode=='button':
                initial='<button>Update</button>'
                replacement='<button onclick="document.getElementById(\'result\').textContent=\'BUTTON_RECOVERED\'">Update</button>'
            body=('<body><div id=actions>'+initial+'</div><p id=result></p><script>'
                  'fetch("/hydrate-release").then(()=>{document.getElementById("actions").innerHTML='
                  +json.dumps(replacement)+';fetch("/hydrate-done")});</script></body>')
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body.encode());return
        if self.path == '/origin-frame':
            body=('<body><iframe src="http://localhost:'+str(self.server.server_port)+'/origin-form"></iframe></body>').encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body);return
        if self.path == '/origin-result':
            Handler.origin_requests.append((self.command,self.path,self.headers.get('Origin')))
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(b'<body>GET_ORIGIN_READY</body>');return
        if self.path in ('/origin-form','/origin-redirect-form'):
            action='/origin-post' if self.path=='/origin-form' else '/origin-redirect'
            body=('<body><form method=post action="'+action+'"><button>Send</button></form></body>').encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(body);return
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
        elif self.path == '/popup':
            body = '''<html><body>OPENER
<button onclick="popup.document.body.innerHTML='<button onclick=&quot;document.body.textContent=this.textContent.toUpperCase()&quot;>Popup action</button>'">Populate popup</button>
<button onclick="popup.close()">Close popup</button>
<script>window.popup=window.open('','regression-popup')</script></body></html>'''
        elif self.path == '/modals':
            body = '''<html><body>
<button onclick="native.closedBy='closerequest';blockCancel=false;native.showModal()">Open native dialog</button>
<button onclick="native.closedBy='any';native.showModal()">Open dismissible dialog</button>
<button onclick="native.closedBy='none';native.showModal()">Open locked dialog</button>
<button onclick="native.closedBy='closerequest';blockCancel=true;native.showModal()">Open cancel-protected dialog</button>
<dialog id=inner><p>INNER_MODAL</p><button onclick="inner.close()">Close inner</button></dialog>
<dialog id=native><p>NATIVE_MODAL</p><p id=focusReport></p><form method=dialog><input aria-label="Dialog value"><button value=accepted>Accept dialog</button><button type=button onclick="inner.showModal()">Open inner</button></form></dialog>
<button onclick="overlay.hidden=false;background.inert=true;background.setAttribute('aria-hidden','true')">Open custom dialog</button>
<div id=background><button>Background action</button><p>BACKGROUND_CONTENT</p></div>
<div id=overlay role=dialog hidden><p>CUSTOM_MODAL</p><button onclick="overlay.hidden=true;background.inert=false;background.removeAttribute('aria-hidden')">Close custom dialog</button></div>
<button popovertarget=popover>Toggle popover</button><div id=popover popover><p>POPOVER_CONTENT</p><button popovertarget=popover popovertargetaction=hide>Close popover</button></div>
<p id=result></p><script>
const native=document.getElementById('native'),inner=document.getElementById('inner'),overlay=document.getElementById('overlay'),background=document.getElementById('background');
let blockCancel=false;
native.addEventListener('cancel',event=>{if(blockCancel)event.preventDefault()});
native.addEventListener('focusin',event=>document.getElementById('focusReport').textContent='FOCUSED='+(event.target.getAttribute('aria-label')||event.target.textContent));
native.addEventListener('close',()=>document.getElementById('result').textContent='DIALOG_RESULT='+native.returnValue);
document.addEventListener('pointerdown',event=>{if(!overlay.hidden&&!overlay.contains(event.target)){
overlay.hidden=true;background.inert=false;background.removeAttribute('aria-hidden');}});
</script></body></html>'''
        elif self.path == '/queued-modal':
            body = '''<html><body><button onclick="advance(0)">Open queued modal</button>
<div id=queued hidden role=dialog>QUEUED_MODAL_READY<button onclick="document.getElementById('queued').hidden=true">Close queued modal</button></div>
<script>function advance(step){setTimeout(()=>{const end=performance.now()+40;while(performance.now()<end){};
if(step<19)advance(step+1);else document.getElementById('queued').hidden=false},0)}</script></body></html>'''
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
    page=run(a,'open',url+'/indexed-frames')
    page=run(a,'wait','text','INDEXED_PARENT_READ:INDEXED_CHILD_READY','--timeout','3000')
    assert 'INDEXED_PARSER_READY' in page['text'] and not page['scriptErrors'],page
    print('PASS frame indices: available to parser scripts and same-origin load callbacks',flush=True)
    page=run(a,'open',url+'/checked-activation')
    fields={field['name']:field['ref'] for field in page['fields']}
    page=run(a,'fill',fields['Accept'],'true')
    assert 'EVENTS=click,input,change' in page['text'],page
    run(a,'fill',fields['Focus marker'],'held')
    page=run(a,'fill',fields['Accept'],'true')
    assert 'EVENTS=click,input,change' in page['text'] and 'change,click' not in page['text'],page
    assert 'FOCUS=Focus marker' in page['text'],page
    result=a.run(['fill',fields['Cancelled'],'true','--json'])
    assert result.returncode!=0 and json.loads(result.stdout)['error']=='ACTION_FAILED',result.stdout
    page=run(a,'observe')
    assert not next(field['checked'] for field in page['fields'] if field['ref']==fields['Cancelled']),page
    result=a.run(['fill',fields['Basic'],'false','--json'])
    assert result.returncode!=0 and json.loads(result.stdout)['error']=='INVALID_ARGUMENT',result.stdout
    page=run(a,'fill',fields['Pro'],'false')
    page=run(a,'fill',fields['Pro'],'true')
    assert next(field['checked'] for field in page['fields'] if field['ref']==fields['Pro']),page
    assert not next(field['checked'] for field in page['fields'] if field['ref']==fields['Basic']),page
    assert not page['scriptErrors'],page
    print('PASS checked activation: event order, repeated state, cancellation and radio group semantics',flush=True)
    page=run(a,'open',url+'/frame-worker')
    page=run(a,'wait','text','CHILD_INITIAL','--timeout','3000')
    document=page['documentId']
    for index in range(6):
        field=page['fields'][0]['ref'];value='REPLY_'+str(index)
        run(a,'fill',field,value)
        run(a,'keyboard',field,'Enter')
        page=run(a,'wait','text','PARENT_'+value,'--timeout','3000')
        assert 'CHILD_'+value in page['text'] and not page['scriptErrors'],page
        assert page['documentId']==document,page
        page=run(a,'observe')
        assert 'PARENT_'+value in page['text'] and 'CHILD_'+value in page['text'],page
    print('PASS observation invalidation: six iframe/worker replies update parent and child without navigation',flush=True)
    page=run(a,'open',url+'/opacity')
    assert {field['name'] for field in page['fields']}=={'Half-visible','Inline winner'},page
    assert 'HIDDEN_' not in page['text'] and 'OPACITY_PASS' in page['text'],page
    page=run(a,'click',page['buttons'][0]['ref'])
    assert {field['name'] for field in page['fields']}=={'Half-visible','Inline winner','Restored opacity'},page
    assert not page['scriptErrors'],page
    print('PASS opacity: inline/external cascade, transparent ancestors, numeric values, computed style and dynamic reveal',flush=True)
    page=run(a,'open',url+'/fill-events');document=page['documentId'];field=page['fields'][0]['ref']
    for index,value in enumerate(('one','two'),1):
        page=run(a,'fill',field,value)
        assert page['documentId']==document and page['url']==url+'/fill-events',page
        assert page['fields'][0]['ref']==field and page['fields'][0]['value']==value,page
        assert page['text'].split('EVENTS=',1)[1].strip()==','.join(['input']*index),page
        assert Handler.fill_loads==1,'fill refetched the document'
    page=run(a,'click',page['buttons'][0]['ref'])
    assert page['text'].split('EVENTS=',1)[1].strip()=='input,input,change',page
    page=run(a,'fill',field,'three')
    page=run(a,'keyboard',field,'Enter')
    assert page['text'].split('EVENTS=',1)[1].strip()=='input,input,change,input,change,submit',page
    assert page['documentId']==document and Handler.fill_loads==1,page
    print('PASS fill: stable document/ref, no reload or submission, input on edit and change on blur/Enter',flush=True)
    for mode in ('unique','failed-wait','failed-action','wait-ref','observed','ambiguous','password','detached'):
        Handler.hydrate_release.clear();Handler.hydrate_done.clear()
        page=run(a,'open',url+'/hydrate-field/'+mode)
        original_ref=page['fields'][0]['ref']
        assert run(a,'observe')['fields'][0]['ref']==original_ref,'unchanged node lost its reference'
        Handler.hydrate_release.set()
        assert Handler.hydrate_done.wait(10),'hydration did not complete'
        if mode=='failed-wait':
            failed=a.run(['wait','text','ABSENT','--timeout','50','--json'])
            assert json.loads(failed.stdout).get('error')=='WAIT_TIMEOUT',failed.stdout
        if mode=='failed-action':
            failed=a.run(['fill','I999999','ignored','--json'])
            assert json.loads(failed.stdout).get('error')=='STALE_REF',failed.stdout
        if mode=='wait-ref':
            page=run(a,'wait','ref',original_ref,'--timeout','1000')
            original_ref=page['fields'][0]['ref']
        if mode=='observed':
            page=run(a,'observe')
            assert page['fields'][0]['ref']!=original_ref,'fixture did not replace the input'
        result=a.run(['fill',original_ref,'agent@example.invalid','--json'])
        page=json.loads(result.stdout)
        if mode in ('unique','failed-wait','failed-action','wait-ref'):
            assert result.returncode==0 and page['fields'][0]['value']=='agent@example.invalid',page
            if mode!='wait-ref':
                assert page['fields'][0]['ref']!=original_ref,'fixture did not replace the input'
        else:
            assert page.get('error')=='STALE_REF',page
            page=run(a,'observe')
            assert all(field['value'] in ('','[redacted]') for field in page['fields']),page
    previous_ref=page['fields'][0]['ref']
    page=run(a,'open',url+'/hydrate-field/new-document')
    assert all(field['ref']!=previous_ref for field in page['fields']),page
    failed=a.run(['fill',previous_ref,'wrong-document','--json'])
    assert json.loads(failed.stdout).get('error')=='STALE_REF',failed.stdout
    Handler.hydrate_release.set()
    for mode in ('link','button','changed-link'):
        Handler.hydrate_release.clear();Handler.hydrate_done.clear()
        page=run(a,'open',url+'/hydrate-action/'+mode)
        original_ref=(page['buttons'] if mode=='button' else page['links'])[0]['ref']
        Handler.hydrate_release.set()
        assert Handler.hydrate_done.wait(10),'action hydration did not complete'
        # A failed command snapshots the replacement but returns no new refs.
        failed=a.run(['fill','I999999','ignored','--json'])
        assert json.loads(failed.stdout).get('error')=='STALE_REF',failed.stdout
        result=a.run(['click',original_ref,'--json']);page=json.loads(result.stdout)
        if mode=='changed-link':
            assert page.get('error')=='STALE_REF',page
        else:
            assert result.returncode==0,page
            assert ('BUTTON_RECOVERED' in page['text'] if mode=='button' else page['url'].endswith('/echo-original')),page
    Handler.hydrate_release.clear();Handler.hydrate_done.clear()
    page=run(a,'open',url+'/ref-frame');original_ref=page['fields'][0]['ref']
    Handler.hydrate_release.set()
    assert Handler.hydrate_done.wait(10),'frame navigation did not complete'
    result=a.run(['fill',original_ref,'wrong-frame-document','--json'])
    assert json.loads(result.stdout).get('error')=='STALE_REF',result.stdout
    page=run(a,'observe')
    assert page['fields'][0]['ref']!=original_ref and not page['fields'][0]['value'],page
    print('PASS reference lifecycle: hydration, failed waits/actions, wait ref, observation boundaries, ambiguity, password, detached documents and new-document isolation',flush=True)
    print('PASS reference lifecycle: link/button recovery, changed destination and replaced frame rejection',flush=True)
    for path in ('/origin-form','/origin-redirect-form'):
        page=run(a,'open',url+path)
        run(a,'click',page['buttons'][0]['ref'])
    assert Handler.origin_requests == [('POST','/origin-post',url),('POST','/origin-redirect',url),('GET','/origin-result',None)],Handler.origin_requests
    page=run(a,'open',url+'/origin-frame')
    run(a,'click',next(x['ref'] for x in page['buttons'] if x['name']=='Send'))
    assert Handler.origin_requests[-1] == ('POST','/origin-post','http://localhost:'+str(server.server_port)),Handler.origin_requests
    print('PASS native form Origin, redirect removal and cross-origin frame source',flush=True)
    queued = run(a, 'open', url+'/queued-modal')
    queued = run(a, 'click', queued['buttons'][0]['ref'])
    assert 'QUEUED_MODAL_READY' in queued['text'], 'Click returned while runnable tasks remained'
    assert any(item['name'] == 'Close queued modal' for item in queued['buttons']), queued
    print('PASS click readiness: queued UI work completes before observation', flush=True)
    modal = run(a, 'open', url+'/modals')
    document = modal['documentId']
    assert all(text not in modal['text'] for text in ('NATIVE_MODAL','CUSTOM_MODAL','POPOVER_CONTENT')), modal
    def modal_action(name):
        return run(a, 'click', next(item['ref'] for item in modal['buttons'] if item['name'] == name))
    modal = modal_action('Open native dialog')
    assert 'NATIVE_MODAL' in modal['text'], modal
    assert 'BACKGROUND_CONTENT' not in modal['text'], modal
    assert not any(item['name'] == 'Open custom dialog' for item in modal['buttons']), modal
    modal = modal_action('Outside dialog')
    assert 'NATIVE_MODAL' in modal['text'], 'Non-dismissible dialog was forcibly closed'
    field = next(item['ref'] for item in modal['fields'] if item['name'] == 'Dialog value')
    modal = run(a, 'keyboard', field, 'Tab')
    assert 'FOCUSED=Accept dialog' in modal['text'], 'CLI Tab did not move focus'
    accept = next(item['ref'] for item in modal['buttons'] if item['name'] == 'Accept dialog')
    modal = run(a, 'keyboard', accept, 'Shift+Tab')
    assert 'FOCUSED=Dialog value' in modal['text'], 'CLI Shift+Tab did not move focus'
    modal = run(a, 'fill', field, 'inside dialog')
    modal = modal_action('Accept dialog')
    assert 'NATIVE_MODAL' not in modal['text'] and 'DIALOG_RESULT=accepted' in modal['text'], modal
    assert modal['documentId'] == document, 'Dialog form performed navigation'
    modal = modal_action('Open dismissible dialog')
    modal = modal_action('Outside dialog')
    assert 'NATIVE_MODAL' not in modal['text'] and 'BACKGROUND_CONTENT' in modal['text'], modal
    def modal_escape():
        outside = next(item['ref'] for item in modal['buttons'] if item['name'] == 'Outside dialog')
        return run(a, 'keyboard', outside, 'Escape')
    modal = modal_action('Open native dialog')
    modal = modal_escape()
    assert 'NATIVE_MODAL' not in modal['text'], 'Native Escape did not request close'
    for launcher in ('Open locked dialog', 'Open cancel-protected dialog'):
        modal = modal_action(launcher)
        modal = modal_escape()
        assert 'NATIVE_MODAL' in modal['text'], launcher
        modal = modal_action('Accept dialog')
        assert 'NATIVE_MODAL' not in modal['text'], modal
    modal = modal_action('Open native dialog')
    modal = modal_action('Open inner')
    assert 'INNER_MODAL' in modal['text'] and 'NATIVE_MODAL' not in modal['text'], 'Stacked dialog did not follow opening order'
    modal = modal_escape()
    assert 'NATIVE_MODAL' in modal['text'] and 'INNER_MODAL' not in modal['text'], modal
    modal = modal_action('Accept dialog')
    modal = modal_action('Open custom dialog')
    assert 'CUSTOM_MODAL' in modal['text'] and 'BACKGROUND_CONTENT' not in modal['text'], modal
    assert not any(item['name'] == 'Background action' for item in modal['buttons']), modal
    modal = modal_action('Outside dialog')
    assert 'CUSTOM_MODAL' not in modal['text'] and 'BACKGROUND_CONTENT' in modal['text'], modal
    modal = modal_action('Toggle popover')
    assert 'POPOVER_CONTENT' in modal['text'], modal
    modal = modal_action('Close popover')
    assert 'POPOVER_CONTENT' not in modal['text'] and not modal['scriptErrors'], modal
    print('PASS dialogs: fill, form close, outside, Escape/cancellation, stack order, custom visibility, popovers', flush=True)
    popup = run(a, 'open', url+'/popup')
    assert 'Window: about:blank' in popup['text'] and 'Window has no observable content yet.' in popup['text'], popup
    populate = next(item['ref'] for item in popup['buttons'] if item['name'] == 'Populate popup')
    popup = run(a, 'click', populate)
    action = next(item['ref'] for item in popup['buttons'] if item['name'] == 'Popup action')
    popup = run(a, 'click', action)
    assert 'POPUP ACTION' in popup['text'] and 'OPENER' in popup['text'], popup
    close_popup = next(item['ref'] for item in popup['buttons'] if item['name'] == 'Close popup')
    popup = run(a, 'click', close_popup)
    assert 'Window:' not in popup['text'] and 'POPUP ACTION' not in popup['text'], popup
    print('PASS popup: blank window observation, realm action, close returns opener', flush=True)
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
    rollback=run(a,'open',url+'/rollback');run(a,'click',rollback['buttons'][0]['ref'])
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
