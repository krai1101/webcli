"""Exercise WebAssembly through the real native CLI and a local HTTP page."""
import http.server
import json
from pathlib import Path
import subprocess
import sys
import threading

SCRIPT = r"""
(async()=>{
const passed=[],failed=[];
const check=(name,condition)=>(condition?passed:failed).push(name);
const hex=text=>Uint8Array.from(text.match(/../g),value=>parseInt(value,16));
const add=hex('0061736d0100000001070160027f7f017f030201000707010361646400000a09010700200020016a0b');
try{
check('valid module',WebAssembly.validate(add));
check('invalid module',!WebAssembly.validate(new Uint8Array([1,2,3])));
const compiled=await WebAssembly.compile(add);
const instance=await WebAssembly.instantiate(compiled);
check('module instance',compiled instanceof WebAssembly.Module&&instance instanceof WebAssembly.Instance);
check('integer export',instance.exports.add(2,3)===5);
check('export metadata',WebAssembly.Module.exports(compiled)[0].name==='add');
const raw=await WebAssembly.instantiate(add);
check('byte instantiation',raw.instance.exports.add(7,8)===15&&raw.module instanceof WebAssembly.Module);
const imports=hex('0061736d0100000001070160027f7f017f020b0103656e76036164640000070701036164640000');
const host=await WebAssembly.instantiate(imports,{env:{add:(a,b)=>a+b}});
check('JavaScript import',host.instance.exports.add(11,12)===23);
const sentinel={test:true};
const throwing=await WebAssembly.instantiate(imports,{env:{add:()=>{throw sentinel}}});
try{throwing.instance.exports.add(1,2);check('import exception',false)}catch(error){check('import exception',error===sentinel)}
const throwingUndefined=await WebAssembly.instantiate(imports,{env:{add:()=>{throw undefined}}});
try{throwingUndefined.instance.exports.add(1,2);check('undefined import exception',false)}catch(error){check('undefined import exception',error===undefined)}
try{host.instance.exports.add({valueOf(){throw sentinel}},2);check('argument conversion exception',false)}catch(error){check('argument conversion exception',error===sentinel)}
const badResult=await WebAssembly.instantiate(imports,{env:{add:()=>({valueOf(){throw sentinel}})}});
try{badResult.instance.exports.add(1,2);check('result conversion exception',false)}catch(error){check('result conversion exception',error===sentinel)}
try{new WebAssembly.Instance(new WebAssembly.Module(imports));check('link error',false)}catch(error){check('link error',error instanceof WebAssembly.LinkError)}
let started=0;
const start=hex('0061736d01000000010401600000020c0103656e76047469636b0000080100');
await WebAssembly.instantiate(start,{env:{tick:()=>{started++}}});check('start function',started===1);
try{await WebAssembly.instantiate(start,{env:{tick:()=>{throw undefined}}});check('undefined start exception',false)}catch(error){check('undefined start exception',error===undefined)}
const memory=new WebAssembly.Memory({initial:1,maximum:2});
const original=memory.buffer;new Uint8Array(original)[0]=42;
check('memory size',original.byteLength===65536);
check('memory identity',memory.buffer===original);
check('memory grow',memory.grow(1)===1&&memory.buffer.byteLength===131072);
check('memory detach',original.byteLength===0&&new Uint8Array(memory.buffer)[0]===42);
const before=memory.buffer;try{memory.grow(1);check('memory maximum',false)}catch(error){check('memory maximum',error instanceof RangeError&&memory.buffer===before)}
check('zero grow',memory.grow(0)===2&&before.byteLength===0);
const global=new WebAssembly.Global({value:'i32',mutable:true},9);global.value=12;
check('mutable global',global.value===12&&global.valueOf()===12);
const immutable=new WebAssembly.Global({value:'i64'},19n);check('BigInt global',immutable.value===19n);
try{immutable.value=20n;check('immutable global',false)}catch(error){check('immutable global',error instanceof TypeError)}
const table=new WebAssembly.Table({element:'externref',initial:1,maximum:2});
check('reference table default',table.get(0)===undefined);
table.set(0,undefined);check('explicit undefined reference',table.get(0)===undefined);
const object={item:1};table.set(0,object);check('reference table',table.get(0)===object);
check('table grow',table.grow(1)===1&&table.length===2&&table.get(1)===undefined);
const tableModule=hex('0061736d0100000001070160027f7f017f03020100040401700001070b02036164640000017401000907010041000b01000a09010700200020016a0b');
const tableInstance=new WebAssembly.Instance(new WebAssembly.Module(tableModule));
check('table function identity',tableInstance.exports.t.get(0)===tableInstance.exports.add &&
  tableInstance.exports.t.get(0)===tableInstance.exports.t.get(0));
const referenceModule=hex('0061736d0100000001060160016f016f0302010007060102696400000a0601040020000b');
const referenceInstance=new WebAssembly.Instance(new WebAssembly.Module(referenceModule));
const referenceValue={reference:true};
check('externref return survives collection',referenceInstance.exports.id(referenceValue)===referenceValue);
const baseline=__webcliWasm('allocatedBytes');
for(let i=0;i<1000;i++) {table.set(0,{index:i});table.set(0,null);}
check('discarded reference recycling',__webcliWasm('allocatedBytes')<baseline+8192);
const data=await fetch('data:application/octet-stream,%00%FF%41#ignored');
check('data bytes',JSON.stringify([...await data.bytes()])==='[0,255,65]');
const response=await fetch('data:application/wasm;base64,'+btoa(String.fromCharCode(...add)));
const streaming=await WebAssembly.instantiateStreaming(response);check('streaming',streaming.instance.exports.add(20,22)===42);
try{await WebAssembly.compileStreaming(new Response(add));check('streaming MIME',false)}catch(error){check('streaming MIME',error instanceof TypeError)}
try{await fetch('data:text/plain,hello',{method:'POST'});check('data method',false)}catch(error){check('data method',error instanceof TypeError)}
try{new WebAssembly.Module(new Uint8Array([1]));check('compile error',false)}catch(error){check('compile error',error instanceof WebAssembly.CompileError)}
const workerSource=`const add=Uint8Array.from('${Array.from(add).map(v=>v.toString(16).padStart(2,'0')).join('')}'.match(/../g),v=>parseInt(v,16));
postMessage(new WebAssembly.Instance(new WebAssembly.Module(add)).exports.add(10,11));`;
await new Promise((resolve,reject)=>{const worker=new Worker(URL.createObjectURL(new Blob([workerSource])));
const timeout=setTimeout(()=>{worker.terminate();reject(Error('worker Wasm timeout'))},2000);
worker.onmessage=event=>{clearTimeout(timeout);check('worker Wasm',event.data===21);worker.terminate();resolve()};worker.onerror=reject;});
const loop='0061736d0100000001040160000003020100070801046c6f6f7000000a0901070003400c000b0b';
await new Promise((resolve,reject)=>{const worker=new Worker(URL.createObjectURL(new Blob([`const code=Uint8Array.from('${loop}'.match(/../g),v=>parseInt(v,16));const i=new WebAssembly.Instance(new WebAssembly.Module(code));postMessage('ready');i.exports.loop();`])));
const timeout=setTimeout(()=>{worker.terminate();reject(Error('worker termination timeout'))},2000);
worker.onmessage=()=>{clearTimeout(timeout);worker.terminate();check('worker cancellation',true);resolve()};worker.onerror=reject;});
}catch(error){failed.push(String(error)+' '+error.stack)}
document.body.textContent=JSON.stringify({passed,failed});
})();
"""

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body=('<!doctype html><body>pending<script>'+SCRIPT+'</script>').encode()
        self.send_response(200)
        self.send_header('Content-Type','text/html; charset=utf-8')
        self.end_headers()
        self.wfile.write(body)
    def log_message(self,*args):
        pass

if len(sys.argv)!=2:
    raise SystemExit('Usage: python tests/wasm_contracts.py /path/to/webcli')
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
exe=str(Path(sys.argv[1]).resolve())
try:
    process=subprocess.run([exe,'--json'],input=f'open http://127.0.0.1:{server.server_port}/\nwait idle --timeout 5000\nobserve\nclose\nquit\n',
                           text=True,encoding='utf8',capture_output=True,timeout=30)
    pages=[json.loads(line) for line in process.stdout.splitlines() if line.startswith('{')]
    page=next(page for page in reversed(pages) if 'text' in page)
    if page.get('scriptErrors'):
        raise AssertionError(page['scriptErrors'])
    result=json.loads(page['text'])
    assert process.returncode==0 and not result['failed'],result
    print(f"PASS Wasm {len(result['passed'])}")
finally:
    server.shutdown()
