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
const beforeEvent=performance.now(),timedEvent=new Event('clock-check');
check('event timestamp uses performance clock',timedEvent.timeStamp>=beforeEvent&&timedEvent.timeStamp<=performance.now());
await test('live Window frame indices',async()=>{
const host=document.createElement('div'),first=document.createElement('iframe'),second=document.createElement('iframe');
document.body.appendChild(host);host.appendChild(first);
check('window index exists without length read',window[0]===first.contentWindow);
host.appendChild(second);check('new frame index exists synchronously',window[1]===second.contentWindow);
host.insertBefore(second,first);check('indices follow DOM order',window[0]===second.contentWindow&&window[1]===first.contentWindow);
first.remove();check('removed last index is undefined',window[1]===undefined&&!Object.hasOwn(window,'1'));
host.remove();check('removed first index is undefined',window[0]===undefined&&!Object.hasOwn(window,'0'));
});
await test('event propagation contracts',async()=>{
const parent=document.createElement('div'),child=document.createElement('button');
parent.appendChild(child);document.body.appendChild(parent);
const order=[];
for(const [node,name] of [[parent,'parent'],[child,'target']]) {
for(const capture of [true,false]) node.addEventListener('probe',event=>{
order.push(name+':'+capture+':'+event.eventPhase);
check('propagation target and currentTarget '+name+capture,event.target===child&&event.currentTarget===node);
},{capture});
}
const event=new Event('probe',{bubbles:true});child.dispatchEvent(event);
check('capture target bubble order',order.join(',')==='parent:true:1,target:true:2,target:false:2,parent:false:3');
check('dispatch clears currentTarget and phase',event.currentTarget===null&&event.eventPhase===0);
let once=0;child.addEventListener('recursive',()=>{once++;child.dispatchEvent(new Event('recursive'))},{once:true});
child.dispatchEvent(new Event('recursive'));check('once removed before reentrant dispatch',once===1);
const stopped=[];child.addEventListener('stop',event=>{stopped.push('first');event.stopImmediatePropagation()});
child.addEventListener('stop',()=>stopped.push('second'));parent.addEventListener('stop',()=>stopped.push('parent'));
child.dispatchEvent(new Event('stop',{bubbles:true}));check('immediate stop blocks remaining listeners and ancestors',stopped.join(',')==='first');
parent.remove();
});
await test('responsive media ranges',async()=>{
for(const [query,expected] of [
['(width >= 512px)',true],['(width < 512px)',false],
['(512px <= width)',true],['(512px > width)',false],
['(1px < width < 10000px)',true],['(10000px > width >= 1px)',true],
['(1px < width > 2px)',false],['(height >= 1rem)',true],
['(width = '+innerWidth+'px)',true],['(width > '+innerWidth+'px)',false],
['not screen and (width < 512px)',true]])
check('media range '+query,matchMedia(query).matches===expected);
const style=document.createElement('style'),node=document.createElement('div');node.id='media-range-test';
style.textContent='#media-range-test{display:none}@media (width >= 512px){#media-range-test{display:block}}';
document.head.appendChild(style);document.body.appendChild(node);
check('CSS and matchMedia share range matching',getComputedStyle(node).display==='block');
node.remove();style.remove();
});
await test('estimated hit testing',async()=>{
const root=document.createElement('div'),target=document.createElement('button');
target.textContent='estimated target';root.appendChild(target);document.body.appendChild(root);
let rect=target.getBoundingClientRect();
check('hit testing agrees with estimated bounds',document.elementFromPoint(rect.left+1,rect.top+1)===target);
check('hit testing ancestor chain',document.elementsFromPoint(1,rect.top+1)[1]===root);
check('hit testing viewport bounds',document.elementFromPoint(-1,1)===null&&document.elementsFromPoint(innerWidth,1).length===0);
try{document.elementFromPoint(NaN,1);check('hit testing finite coordinates',false)}catch(e){check('hit testing finite coordinates',e instanceof TypeError)}
const spacer=document.createTextNode('spacer');root.insertBefore(spacer,target);
check('estimated position invalidated by mutation',target.getBoundingClientRect().top===rect.top+5);
rect=target.getBoundingClientRect();target.style.display='none';
check('hit testing excludes hidden target',document.elementFromPoint(1,rect.top+1)!==target);
target.style.display='block';target.style.pointerEvents='none';
check('hit testing excludes pointer-events none',document.elementFromPoint(1,rect.top+1)!==target);
root.remove();
});
await test('dialog forms and inert reflection',async()=>{
const background=document.createElement('div');background.inert=true;
check('inert property reflects attribute',background.hasAttribute('inert'));
background.removeAttribute('inert');check('inert attribute reflects property',!background.inert);
const dialog=document.createElement('dialog'),form=document.createElement('form'),button=document.createElement('button');
button.value='accepted';form.method='dialog';form.appendChild(button);dialog.appendChild(form);document.body.appendChild(dialog);
let closes=0;dialog.addEventListener('close',()=>closes++);dialog.showModal();
check('estimated modal backdrop blocks background hit',document.elementFromPoint(0,0)===dialog);
const cancel=event=>event.preventDefault();form.addEventListener('submit',cancel);
form.requestSubmit(button);check('cancelled dialog submission stays open',dialog.open);
form.removeEventListener('submit',cancel);form.requestSubmit(button);await Promise.resolve();
check('dialog submission closes locally',!dialog.open&&dialog.returnValue==='accepted'&&closes===1);
form.method='get';button.setAttribute('formmethod','dialog');dialog.show();form.requestSubmit(button);
check('dialog submitter method overrides form',!dialog.open);
dialog.show();dialog.returnValue='retained';form.method='dialog';form.submit();
check('dialog submit without button preserves return value',!dialog.open&&dialog.returnValue==='retained');
dialog.remove();
});
await test('input gesture focus and event detail',async()=>{
const root=document.createElement('div');
root.innerHTML='<input><button type="button"><span>inside button</span></button><div>decoration</div>';
document.body.appendChild(root);
const input=root.querySelector('input'),button=root.querySelector('button'),span=root.querySelector('span');
const focusEvent=new FocusEvent('focusin',{relatedTarget:input});
check('FocusEvent inherits UIEvent',focusEvent instanceof FocusEvent&&focusEvent instanceof UIEvent&&!focusEvent.isTrusted);
check('FocusEvent exposes related target',focusEvent.relatedTarget===input);
let invalidFocusTarget=false;try{new FocusEvent('focus',{relatedTarget:{}})}catch(error){invalidFocusTarget=error instanceof TypeError}
check('FocusEvent validates related target',invalidFocusTarget);
const sequence=[];
for(const type of ['pointerdown','mousedown','pointerup','mouseup','click'])
button.addEventListener(type,event=>sequence.push([type,event.detail,event.isTrusted]));
input.focus();
let lostFocus=false,gainedFocus=false;
input.addEventListener('focusout',event=>{lostFocus=event instanceof FocusEvent&&event.relatedTarget===button&&event.isTrusted});
button.addEventListener('focusin',event=>{gainedFocus=event instanceof FocusEvent&&event.relatedTarget===input&&event.isTrusted});
__inputClick(span);
check('mouse focuses nearest control',document.activeElement===button);
check('focus transitions dispatch FocusEvent with related targets',lostFocus&&gainedFocus);
check('pointer and mouse click detail',JSON.stringify(sequence)===JSON.stringify([
['pointerdown',0,true],['mousedown',1,true],['pointerup',0,true],['mouseup',1,true],['click',1,true]]));
input.focus();__inputClick(root.lastElementChild);
check('decorative mouse target does not retain focus',document.activeElement===document.body);
const dialog=document.createElement('dialog'),field=document.createElement('input');
dialog.appendChild(field);root.appendChild(dialog);dialog.showModal();field.focus();
__inputOutside(dialog);
check('virtual outside action preserves static dialog focus',dialog.open&&document.activeElement===field);
dialog.close();
root.remove();
});
await test('native dialog focus containment',async()=>{
const root=document.createElement('div');root.innerHTML='<button id="focus-launcher">Open</button><dialog><input aria-label="first"><input autofocus tabindex="-1"><button type="button">Last</button><input disabled><input hidden><input inert></dialog>';
document.body.appendChild(root);
const launcher=root.querySelector('button'),dialog=root.querySelector('dialog'),fields=dialog.querySelectorAll('input'),last=dialog.querySelector('button');
launcher.focus();dialog.showModal();
check('dialog autofocus accepts negative tabindex',document.activeElement===fields[1]);
launcher.focus();check('native modal blocks outside focus',document.activeElement===fields[1]);
fields[0].focus();__keyboard(fields[0],'Tab');
check('modal Tab skips negative/disabled/hidden/inert fields',document.activeElement===last);
__keyboard(last,'Tab');check('modal Tab wraps forward',document.activeElement===fields[0]);
__keyboard(fields[0],'Shift+Tab');check('modal Shift+Tab wraps backward',document.activeElement===last);
const prevent=event=>event.preventDefault();last.addEventListener('keydown',prevent);
__keyboard(last,'Tab');check('cancelled Tab does not move focus',document.activeElement===last);
last.removeEventListener('keydown',prevent);
const nested=document.createElement('dialog');nested.innerHTML='<input>';dialog.appendChild(nested);
last.focus();nested.showModal();check('nested modal receives focus',document.activeElement===nested.firstElementChild);
fields[0].focus();check('nested modal blocks parent focus',document.activeElement===nested.firstElementChild);
nested.close();check('closing nested modal restores parent focus',document.activeElement===last);
dialog.close();check('closing modal restores launcher focus',document.activeElement===launcher);
dialog.show();check('nonmodal dialog receives initial focus',document.activeElement===fields[1]);
dialog.close();check('nonmodal close restores launcher focus',document.activeElement===launcher);
const outside=document.createElement('button');root.appendChild(outside);
dialog.show();outside.focus();dialog.close();
check('nonmodal close preserves focus deliberately moved outside',document.activeElement===outside);
const detached=document.createElement('dialog');let rejected=false;
try{detached.showModal()}catch(error){rejected=error.name==='InvalidStateError'}
check('detached modal throws InvalidStateError',rejected);
root.remove();
});
await test('character data capacity',async()=>{
for(let size=8;size<=128;size+=8){
const text=document.createTextNode('x');
text.data='a'.repeat(size);
check('character data terminator '+size,text.data==='a'.repeat(size));
}
});
await test('streams',async()=>{
const settled=async promise=>{try{await promise;return false}catch(error){return error}};
const identity=new TransformStream(),reader=identity.readable.getReader(),writer=identity.writable.getWriter();
const chunk={value:7};let written=false;const write=writer.write(chunk).then(()=>written=true);
await Promise.resolve();await Promise.resolve();check('transform backpressure',!written);
check('transform preserves chunk identity',(await reader.read()).value===chunk);await write;
await writer.close();check('transform closes readable',(await reader.read()).done);
reader.releaseLock();writer.releaseLock();check('stream locks release',!identity.readable.locked&&!identity.writable.locked);
const transformed=new TransformStream({async transform(value,c){await Promise.resolve();c.enqueue(value*2)},flush(c){c.enqueue('tail')}});
const r=transformed.readable.getReader(),w=transformed.writable.getWriter();const pendingRead=r.read();const pendingWrite=w.write(3);
check('async transformation',(await pendingRead).value===6);await pendingWrite;await w.close();
check('transform flush',(await r.read()).value==='tail'&&(await r.read()).done);
const bad=new TransformStream({transform(value,c){c.error(new Error('transform failure'))}});
const br=bad.readable.getReader(),bw=bad.writable.getWriter();const readError=settled(br.read());
check('transform write rejection',(await settled(bw.write(1))).message==='transform failure');
check('transform read rejection',(await readError).message==='transform failure');
const cancelled=new TransformStream(),cw=cancelled.writable.getWriter();const blocked=settled(cw.write(1));
await cancelled.readable.cancel('cancelled');check('readable cancellation rejects write',(await blocked)==='cancelled');
const aborted=new TransformStream(),aw=aborted.writable.getWriter();const awaiting=settled(aw.write(1));
await aw.abort('aborted');check('abort releases blocked transform',(await awaiting)==='aborted');
const terminating=new TransformStream({transform(value,c){c.enqueue(value);c.terminate()}});
const tr=terminating.readable.getReader(),tw=terminating.writable.getWriter();const value=tr.read();
const termination=settled(tw.write(5));check('termination drains readable',(await value).value===5&&(await tr.read()).done);
check('termination errors writable',(await termination) instanceof TypeError);
const order=[];const sink=new WritableStream({async write(value){await Promise.resolve();order.push(value)},close(){order.push('closed')}});
const sw=sink.getWriter();await Promise.all([sw.write(1),sw.write(2)]);await sw.close();
check('writable FIFO and close',JSON.stringify(order)==='[1,2,"closed"]');
const source=new ReadableStream({start(c){c.enqueue(2);c.enqueue(4);c.close()}});
const output=[];await source.pipeThrough(new TransformStream({transform(value,c){c.enqueue(value+1)}})).pipeTo(new WritableStream({write(value){output.push(value)}}));
check('pipe through transformation',JSON.stringify(output)==='[3,5]'&&!source.locked);
const flushFailure=new TransformStream({flush(){throw new Error('flush failure')}});
const fr=flushFailure.readable.getReader(),fw=flushFailure.writable.getWriter();const flushRead=settled(fr.read());
check('flush rejects close',(await settled(fw.close())).message==='flush failure');
check('flush errors readable',(await flushRead).message==='flush failure');
const failedSink=new WritableStream({write(){throw new Error('sink failure')}}),fs=failedSink.getWriter();
check('writable failure',(await settled(fs.write(1))).message==='sink failure');
check('writable ready rejection',(await settled(fs.ready)).message==='sink failure');
let sourceCancelled=false;const piping=new ReadableStream({start(c){c.enqueue(1)},cancel(){sourceCancelled=true}});
check('pipe propagates sink failure',(await settled(piping.pipeTo(new WritableStream({write(){throw new Error('pipe failure')}})))).message==='pipe failure');
check('pipe cancellation and unlock',sourceCancelled&&!piping.locked);
try{new TransformStream({start(){throw new Error('start failure')}});check('transform start throws',false)}catch(error){check('transform start throws',error.message==='start failure')}
});
await test('file list',async()=>{
const input=document.createElement('input');check('non-file input files null',input.files===null);
input.type='file';const files=input.files;
check('file list brand',files instanceof FileList&&Object.prototype.toString.call(files)==='[object FileList]');
check('file list stable identity',input.files===files);
check('empty file list',files.length===0&&files.item(0)===null&&files[0]===undefined&&[...files].length===0);
try{new FileList();check('file list illegal constructor',false)}catch(error){check('file list illegal constructor',error instanceof TypeError)}
try{input.files=[];check('file list setter type check',false)}catch(error){check('file list setter type check',error instanceof TypeError)}
const other=document.createElement('input');other.type='file';other.files=files;
check('file list assignment',other.files===files);other.files=null;check('null file list assignment ignored',other.files===files);
input.type='text';check('file list hidden on non-file input',input.files===null);
});
await test('body',async()=>{
const metadata=await (await fetch('/metadata',{headers:{'Sec-Fetch-Site':'cross-site'}})).json();
check('native fetch metadata',metadata.site==='same-origin'&&metadata.mode==='cors'&&metadata.dest==='empty');
check('cookies enabled',navigator.cookieEnabled===true);
check('secure loopback context',isSecureContext===true);
const hints=await navigator.userAgentData.getHighEntropyValues(['architecture','bitness','platformVersion']);
check('requested UA hints',typeof hints.architecture==='string'&&typeof hints.bitness==='string'&&typeof hints.platformVersion==='string');
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
const selection=document.createElement('input');selection.value='a😀bc';
selection.setSelectionRange(1,3,'backward');
check('UTF-16 text selection',selection.selectionStart===1&&selection.selectionEnd===3&&selection.selectionDirection==='backward');
selection.setSelectionRange(4,2);check('reversed text selection',selection.selectionStart===2&&selection.selectionEnd===2);
selection.value='new';check('value replacement caret',selection.selectionStart===3&&selection.selectionEnd===3);
selection.type='email';check('unsupported input selection',selection.selectionStart===null);
try{selection.setSelectionRange(0,1);check('selection state rejection',false)}catch(e){check('selection state rejection',e.name==='InvalidStateError')}
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
check('nonvisual size estimates',rect instanceof DOMRect && rect.width===100 && rect.height>0 && measured.getClientRects().length===1);
styled.appendChild(measured);styled.style.display='none';document.body.appendChild(styled);check('hidden style',getComputedStyle(styled).display==='none');styled.remove();
const large=document.createElement('div');large.innerHTML='<!--'+'x'.repeat(70000)+'--><span>fragment</span>';check('fetched HTML fragment',large.querySelector('span').textContent==='fragment');
try{new HTMLCollection();check('collection illegal constructor',false)}catch(e){check('collection illegal constructor',e instanceof TypeError)}
const svg=document.createElementNS('http://www.w3.org/2000/svg','svg'),circle=document.createElementNS('http://www.w3.org/2000/svg','circle');svg.appendChild(circle);check('namespace collection',svg.getElementsByTagNameNS('http://www.w3.org/2000/svg','circle').length===1);
});
await test('popup ownership',async()=>{
const iframe=document.createElement('iframe');document.body.appendChild(iframe);
await new Promise(resolve=>setTimeout(resolve,0));
const popup=iframe.contentWindow.open('','contract-popup');
check('initial popup name',popup.name==='contract-popup');
iframe.remove();await new Promise(resolve=>setTimeout(resolve,0));
check('popup survives opener removal',!popup.closed && popup.name==='contract-popup');
popup.close();
const loaded=window.open('/popup-name','loaded-name');
for(let i=0;i<100 && loaded.document.body.textContent!=='loaded-name';i++) await new Promise(resolve=>setTimeout(resolve,10));
check('popup name before author scripts',loaded.document.body.textContent==='loaded-name');loaded.close();
const unnamed=window.open('','_blank');check('blank target has no name',unnamed.name==='');unnamed.close();
});
await test('message ports',async()=>{
const channel=new MessageChannel(),received=[],order=[],payload={value:1};
channel.port2.postMessage(payload);payload.value=9;channel.port2.postMessage(2);
const delivered=new Promise(resolve=>channel.port1.addEventListener('message',event=>{
received.push(event.data);order.push('message');queueMicrotask(()=>order.push('microtask'));
if(received.length===2)resolve();}));
await new Promise(resolve=>setTimeout(resolve,0));
check('port requires start',received.length===0);channel.port1.start();
await Promise.race([delivered,new Promise((_,reject)=>setTimeout(()=>reject(Error('port delivery timeout')),1000))]);
check('port clone and FIFO',received[0].value===1&&received[1]===2);
check('port microtask checkpoint',order.join(',')==='message,microtask,message,microtask');
channel.port1.close();channel.port2.close();
const closed=new MessageChannel();let calls=0;
closed.port1.onmessage=()=>calls++;closed.port2.postMessage(0);closed.port1.close();
await new Promise(resolve=>setTimeout(resolve,0));check('closed port cancels delivery',calls===0);
closed.port2.close();
const automatic=new MessageChannel();
await Promise.race([new Promise(resolve=>{automatic.port1.onmessage=e=>{check('port onmessage starts',e.isTrusted&&e.data===3);resolve()};automatic.port2.postMessage(3)}),new Promise((_,reject)=>setTimeout(()=>reject(Error('port auto-start timeout')),1000))]);
automatic.port1.close();automatic.port2.close();
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
        if self.path == "/popup-name":
            self.response(b"<body><script>document.body.textContent=window.name</script></body>")
        elif self.path == "/metadata":
            self.response(json.dumps({"site":self.headers.get("Sec-Fetch-Site"),"mode":self.headers.get("Sec-Fetch-Mode"),"dest":self.headers.get("Sec-Fetch-Dest")}).encode(), "application/json")
        elif self.path == "/parser-checkpoint":
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
                b"""<html><title>Lifecycle</title><body><p id="state">START</p><p id="timer"></p><button onclick="setTimeout(()=>document.getElementById('timer').textContent='TIMER',500);fetch('/slow').then(()=>document.getElementById('state').textContent='FETCH').catch(()=>document.getElementById('state').textContent='ABORTED');location.href='/clean'">Navigate</button></body></html>"""
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
    form = run("open", f"http://127.0.0.1:{server.server_port}/encoding-form")
    text_ref = next(field['ref'] for field in form['fields'] if field['type']=='text')
    select_ref = next(field['ref'] for field in form['fields'] if field['type']=='select')
    run("fill", text_ref, "a b~*+")
    selection = run("select", select_ref, "fr")
    assert selection["fields"][-1]["value"] == "fr", selection
    form_result = run("keyboard", text_ref, "Enter")
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
    clean = run("click", initial['buttons'][0]['ref'])
    assert (
        clean["title"] == "Clean document"
        and clean["documentId"] != initial["documentId"]
    ), clean
    time.sleep(0.7)
    run("wait", "text", "CLEAN", "--timeout", "200")
    assert "CLEAN" in run("observe")["text"]
    restored = run("click", clean['buttons'][0]['ref'])
    assert restored["documentId"] == initial["documentId"], restored
    # Retained history documents resume their queued work on traversal. Click's
    # readiness wait can now run both the resumed timer and fetch completion.
    assert restored["title"] == "Lifecycle", restored
    restored = run("wait", "text", "TIMER", "--timeout", "2000")
    assert not restored["scriptErrors"], restored
    stale = terminal.run(["click", "B1", "--document", clean["documentId"], "--json"])
    assert json.loads(stale.stdout)["error"] == "STALE_DOCUMENT", stale.stdout
    print(
        "PASS lifecycle: document isolation, timer suspension/history resume, stale document guard"
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
