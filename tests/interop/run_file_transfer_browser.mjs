// Real Chromium browser against a synthetic loopback native transfer server.
// No browser package, owner profile, credential or external page is used.
import {spawn} from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import assert from 'node:assert/strict';
const repo=process.cwd(),stamp=process.pid+'-'+Date.now();
const root=path.join(repo,'artifacts','AURA-transfer-browser-'+stamp);
const profile=path.join(repo,'artifacts','transfer-browser-profile-'+stamp);
const exe=path.join(repo,'build','native','aura_file_transfer_runtime_tests.exe');
const candidates=[process.env['ProgramFiles(x86)']&&path.join(process.env['ProgramFiles(x86)'],'Microsoft','Edge','Application','msedge.exe'),process.env.ProgramFiles&&path.join(process.env.ProgramFiles,'Google','Chrome','Application','chrome.exe')].filter(Boolean);
let binary;
for(const item of candidates){try{await fs.access(item);binary=item;break}catch{}}
assert(binary,'An installed Chromium browser is required for this optional QA');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
async function until(fn,label){for(let i=0;i<250;i++){if(await fn())return;await delay(40)}throw new Error(label)}
const fixture=spawn(exe,['--browser-qa',root],{cwd:repo,windowsHide:true,stdio:['pipe','pipe','pipe']});
let browser,ws,readyText='',fixtureError='',nextId=0;
const pending=new Map();
fixture.stdout.on('data',b=>{readyText+=b.toString()});fixture.stderr.on('data',b=>{fixtureError+=b.toString()});
function call(method,params={}){const id=++nextId;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{pending.delete(id);reject(new Error('CDP timeout: '+method))},10000);pending.set(id,{resolve:v=>{clearTimeout(timer);resolve(v)},reject:e=>{clearTimeout(timer);reject(e)}});ws.send(JSON.stringify({id,method,params}))})}
async function evaluate(expression){const result=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true,userGesture:true});if(result.exceptionDetails)throw new Error('Browser evaluation failed');return result.result.value}
async function screenshot(name){const result=await call('Page.captureScreenshot',{format:'png',captureBeyondViewport:true});await fs.writeFile(path.join(repo,'artifacts',name),Buffer.from(result.data,'base64'))}
async function selectFile(file){const doc=await call('DOM.getDocument',{});const node=await call('DOM.querySelector',{nodeId:doc.root.nodeId,selector:'#picker'});await call('DOM.setFileInputFiles',{nodeId:node.nodeId,files:Array.isArray(file)?file:[file]});await until(()=>evaluate('!document.getElementById("send").disabled'),'file selection did not enable Send')}
try{
 await until(()=>readyText.includes('\n')||fixture.exitCode!==null,'native fixture did not start');
 assert(fixture.exitCode===null,'Native fixture failed: '+fixtureError);
 const line=readyText.split(/\r?\n/).find(x=>x.startsWith('DEMO_TRANSFER '));assert(line,'missing synthetic handshake');
 const endpoint=JSON.parse(line.slice('DEMO_TRANSFER '.length));
 await fs.mkdir(profile,{recursive:true});
 browser=spawn(binary,['--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--disable-background-networking','--disable-component-update','--disable-sync','--remote-debugging-address=127.0.0.1','--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{windowsHide:true,stdio:'ignore'});
 const active=path.join(profile,'DevToolsActivePort');await until(async()=>{try{await fs.access(active);return true}catch{return false}},'browser debugger did not start');
 const debugPort=(await fs.readFile(active,'utf8')).split(/\r?\n/)[0];
 const page=await(await fetch('http://127.0.0.1:'+debugPort+'/json/new?'+encodeURIComponent(endpoint.address),{method:'PUT'})).json();
 ws=new WebSocket(page.webSocketDebuggerUrl);
 await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true})});
 ws.addEventListener('message',event=>{const message=JSON.parse(event.data);if(!message.id)return;const entry=pending.get(message.id);if(!entry)return;pending.delete(message.id);if(message.error)entry.reject(new Error('CDP error: '+message.error.message));else entry.resolve(message.result)});
 await call('Page.enable');await call('Runtime.enable');
 await call('Emulation.setDeviceMetricsOverride',{width:960,height:1000,deviceScaleFactor:1,mobile:false});
 await until(()=>evaluate('document.readyState==="complete"&&!!document.getElementById("connect")'),'portal did not render');
 assert.equal(await evaluate('document.querySelector("h1").textContent'),'Μεταφορά αρχείων');
 await screenshot('transfer-browser-login-el.png');
 await evaluate('document.getElementById("pin").value='+JSON.stringify(endpoint.pin)+';document.getElementById("connect").click();true');
 await until(()=>evaluate('!document.getElementById("upload").classList.contains("hidden")'),'browser login failed');
 assert.equal(await evaluate('document.getElementById("pin").value'),'');
 const image=path.join(repo,'tests','fixtures','photos','DEMO-image.png');
 await selectFile(image);await evaluate('document.getElementById("send").click();true');
 await until(()=>evaluate('document.getElementById("message").textContent==="Η μεταφορά ολοκληρώθηκε."'),'browser photo upload failed');
 assert.deepEqual(await fs.readFile(path.join(root,'photos','DEMO-image.png')),await fs.readFile(image));
 const tall=path.join(repo,'tests','fixtures','photos','DEMO-tall.png');
 const originalTall=await fs.readFile(tall);
 await selectFile(tall);await evaluate('document.getElementById("send").click();true');
 await until(()=>evaluate('document.getElementById("message").textContent==="Η μεταφορά ολοκληρώθηκε."'),'large-resolution PNG was not automatically prepared');
 const fitted=await fs.readFile(path.join(root,'photos','DEMO-tall.png'));
 assert.equal(fitted.readUInt32BE(16),320);assert.equal(fitted.readUInt32BE(20),480);
 assert.deepEqual(await fs.readFile(tall),originalTall,'original source must remain unchanged');
 await screenshot('transfer-browser-desktop-el.png');
 await evaluate('document.getElementById("language").click();true');
 assert.equal(await evaluate('document.querySelector("h1").textContent'),'File transfer');
 const text=path.join(root,'DEMO-note.txt');await fs.writeFile(text,'DEMO browser file transfer\n');
 const second=path.join(root,'DEMO-second.txt');await fs.writeFile(second,'DEMO second file\n'.repeat(65536));
 await selectFile([text,second]);
 assert.equal(await evaluate('document.getElementById("progress").hidden&&document.getElementById("progress").value===0'),true);
 await evaluate('document.getElementById("send").click();true');
 await until(()=>evaluate('document.getElementById("message").textContent==="Transfer complete."'),'browser generic upload failed');
 assert.equal(await fs.readFile(path.join(root,'files','DEMO-note.txt'),'utf8'),'DEMO browser file transfer\n');
 assert.equal(await fs.readFile(path.join(root,'files','DEMO-second.txt'),'utf8'),'DEMO second file\n'.repeat(65536));
 await call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
 await screenshot('transfer-browser-mobile-en.png');
 await selectFile(image);await evaluate('document.getElementById("send").click();true');
 await until(()=>evaluate('document.getElementById("message").textContent.includes("already exists")'),'browser collision message missing');
 assert.deepEqual(await fs.readFile(path.join(root,'photos','DEMO-image.png')),await fs.readFile(image));
 await evaluate('document.getElementById("stored-area").value="files";document.getElementById("stored-area").dispatchEvent(new Event("change"));true');
 await until(()=>evaluate('document.getElementById("stored-list").textContent.includes("DEMO-note.txt")'),'authenticated file list did not load');
 const openDelete='[...document.querySelectorAll("#stored-list li")].find(el=>el.textContent.includes("DEMO-note.txt")).querySelector("button").click();true';
 await evaluate(openDelete);
 assert.equal(await evaluate('document.getElementById("delete-dialog").open'),true);
 assert.equal(await evaluate('document.getElementById("delete-name").textContent'),'DEMO-note.txt');
 await screenshot('transfer-browser-delete-en.png');
 await evaluate('document.getElementById("delete-cancel").click();true');
 assert.equal(await fs.readFile(path.join(root,'files','DEMO-note.txt'),'utf8'),'DEMO browser file transfer\n');
 await evaluate(openDelete);await evaluate('document.getElementById("delete-confirm").click();true');
 await until(async()=>{try{await fs.access(path.join(root,'files','DEMO-note.txt'));return false}catch{return true}},'confirmed file deletion failed');
 await until(()=>evaluate('!document.getElementById("refresh").disabled'),'delete list refresh did not complete');
 await evaluate('document.getElementById("stored-area").value="photos";document.getElementById("stored-area").dispatchEvent(new Event("change"));true');
 await until(()=>evaluate('document.getElementById("stored-list").textContent.includes("DEMO-tall.png")'),'photo list did not load');
 await evaluate('[...document.querySelectorAll("#stored-list li")].find(el=>el.textContent.includes("DEMO-tall.png")).querySelector("button").click();document.getElementById("delete-confirm").click();true');
 await until(async()=>{try{await fs.access(path.join(root,'photos','DEMO-tall.png'));return false}catch{return true}},'confirmed photo deletion failed');
 console.log('Browser QA passed: PIN login, automatic 1024x1536 PNG resizing, preserved source, byte-exact small PNG and 1 MiB generic files, collisions, authenticated listing, cancel and confirmed file/photo deletion.');
}finally{
 if(ws&&ws.readyState===WebSocket.OPEN){try{await call('Browser.close')}catch{}ws.close()}
 if(browser){await delay(300);if(browser.exitCode===null)browser.kill()}
 fixture.stdin.end('stop\n');
 await until(()=>fixture.exitCode!==null,'native fixture did not stop').catch(()=>fixture.kill());
 // Exact synthetic files only; leave the isolated browser profile as QA evidence.
 for(const relative of ['photos/DEMO-image.png','photos/DEMO-tall.png','files/DEMO-note.txt','files/DEMO-second.txt','DEMO-note.txt','DEMO-second.txt','.transfer/upload.part','.transfer/active.lock'])await fs.unlink(path.join(root,relative)).catch(error=>{if(error.code!=='ENOENT')throw error});
 for(const relative of ['photos','files','.transfer'])await fs.rmdir(path.join(root,relative)).catch(error=>{if(error.code!=='ENOENT')throw error});
 await fs.rmdir(root).catch(error=>{if(error.code!=='ENOENT')throw error});
}
