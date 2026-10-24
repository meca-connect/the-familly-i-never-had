#pragma once
// Web pages for "The Familly I Never Had" — gallery-label design system:
// warm paper, ink, one terracotta accent, book serif, small-caps labels.
// No external fonts or scripts (the device may be offline; privacy).

#define FAMILY_CSS R"CSS(
:root{--paper:#f3eee3;--sheet:#faf7f0;--ink:#1c1a17;--muted:#6e665b;--rule:#d9d1c2;--accent:#b4502c;
--serif:"Iowan Old Style","Palatino Linotype",Palatino,"Book Antiqua","URW Palladio L","Noto Serif",Georgia,serif}
*{box-sizing:border-box}html{background:var(--paper)}
body{margin:0;color:var(--ink);font:18px/1.55 var(--serif);font-variant-numeric:oldstyle-nums;-webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility}
.wrap{max-width:1120px;margin:0 auto;padding:clamp(20px,4vw,56px)}
.mast{display:flex;justify-content:space-between;align-items:flex-end;gap:28px;border-bottom:1px solid var(--rule);padding-bottom:20px}
.label{font-variant:all-small-caps;letter-spacing:.14em;color:var(--muted);font-size:15px;margin:0}
h1{font-weight:400;font-size:clamp(36px,5.6vw,66px);line-height:1;letter-spacing:-.012em;margin:8px 0 0}
h1 em{font-style:italic;color:var(--accent);white-space:nowrap}
h2{font-weight:400;font-size:24px;line-height:1.2;margin:6px 0 10px;text-wrap:balance}
.lede{max-width:30ch;color:var(--muted);font-style:italic;margin:0;text-wrap:pretty}
.grid{display:grid;grid-template-columns:minmax(0,1fr) minmax(280px,370px);gap:clamp(28px,5vw,76px);margin-top:clamp(28px,4vw,52px);align-items:start}
@media(max-width:780px){.grid{grid-template-columns:1fr}.mast{flex-direction:column;align-items:flex-start}}
.plate{margin:0}
.frame{background:var(--sheet);padding:clamp(14px,2.4vw,30px);border:1px solid var(--rule);box-shadow:0 22px 44px -32px rgba(48,34,20,.45)}
.frame img{display:block;width:100%;aspect-ratio:2/3;object-fit:cover;image-rendering:pixelated;background:#e8e2d5}
.frame.portrait img{aspect-ratio:3/4;image-rendering:auto}
.void{display:grid;place-items:center;aspect-ratio:2/3;background:#e8e2d5;color:var(--muted);font-style:italic;text-align:center;padding:24px}
.frame.portrait .void{aspect-ratio:3/4}
figcaption{margin-top:16px;display:grid;gap:2px;font-size:16px;color:var(--muted)}
figcaption .no{color:var(--ink);font-size:21px}
figcaption .scene{font-style:italic;color:var(--ink)}
.ledger section{padding:20px 0;border-top:1px solid var(--rule)}
.ledger section:first-child{border-top:0;padding-top:0}
.state{font-size:27px;line-height:1.18;margin:6px 0 14px;text-wrap:balance}
.bar{height:2px;background:var(--rule);overflow:hidden;max-width:220px;opacity:0;transition:opacity .4s}.bar.on{opacity:1}.bar i{display:block;height:100%;width:0;background:var(--accent);transition:width .7s ease}
p{margin:0 0 10px}
button,.button{font:inherit;font-size:17px;letter-spacing:.01em;border-radius:6px;padding:11px 20px;min-height:44px;border:1px solid var(--ink);background:var(--ink);color:var(--sheet);cursor:pointer;display:inline-flex;align-items:center;gap:8px}
button.ghost,.button.ghost{background:transparent;color:var(--ink)}
button:disabled{opacity:.35;cursor:not-allowed}
:focus-visible{outline:2px solid var(--accent);outline-offset:3px}
a{color:var(--ink);text-decoration-color:var(--accent);text-underline-offset:4px}
select,input[type=text],input[type=password]{font:inherit;color:var(--ink);background:transparent;border:0;border-bottom:1px solid var(--ink);padding:4px 2px;border-radius:0}
select{appearance:none;-webkit-appearance:none;cursor:pointer;color:var(--accent);border-bottom-color:var(--accent);padding:0 2px;font-style:italic}
input[type=text],input[type=password]{width:100%;margin-top:4px}
input[type=file]{position:absolute;width:1px;height:1px;opacity:0}
label.check{display:flex;gap:12px;align-items:flex-start;font-size:16px;line-height:1.45;cursor:pointer}
label.check input{margin-top:5px;accent-color:var(--accent);width:18px;height:18px;flex:none}
.fine{font-size:14px;color:var(--muted);line-height:1.5;margin-top:10px}
.row{display:flex;flex-wrap:wrap;gap:10px;align-items:center}
footer{margin-top:60px;padding-top:18px;border-top:1px solid var(--rule);display:flex;justify-content:space-between;flex-wrap:wrap;gap:12px 28px;color:var(--muted);font-style:italic;font-size:15px}
@media(prefers-reduced-motion:reduce){.bar i{transition:none}}
)CSS"

#define FAMILY_HEAD(title) "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" title "</title><style>" FAMILY_CSS "</style>"

#define FAMILY_MAST(lede) "<header class=\"mast\"><div><p class=\"label\">An installation in six colours of ink</p><h1>The Familly <em>I Never Had</em></h1></div><p class=\"lede\">" lede "</p></header>"

static const char DASHBOARD_HTML[] PROGMEM = FAMILY_HEAD("The Familly I Never Had") R"HTML(<body><div class="wrap">)HTML"
FAMILY_MAST("The subject remains. The household is reassigned every night, and the last one is not kept.")
R"HTML(<main class="grid">
<figure class="plate"><div class="frame" id="frame"><img id="preview" alt="The household currently assigned to the subject"></div>
<figcaption><span class="no">Household Nº <b id="no">—</b></span><span class="scene" id="scene">No household has been assigned.</span><span id="when"></span></figcaption></figure>
<aside class="ledger">
<section><p class="label">State</p><p class="state" id="state" role="status" aria-live="polite">Listening for the device…</p><div class="bar" aria-hidden="true"><i id="prog"></i></div></section>
<section><div class="row"><button id="assign">Assign another household</button></div><p class="fine">The current household will be destroyed. Each assignment sends the subject’s photograph to OpenAI and is paid for.</p></section>
<section><p class="label">The subject</p><h2 id="subject">—</h2><a href="/face">Replace the subject</a></section>
<section><p class="label">The night</p><p>Reassigned nightly at <select id="hour" aria-label="Night hour"></select></p><p class="fine" id="next"></p></section>
<section><p class="label">Power</p><p id="power">—</p><button class="ghost" id="awake" hidden>Keep it awake</button></section>
</aside></main>
<footer><span>Nothing here is remembered. Each household exists for one night.</span><span id="boot"></span></footer></div>
<script>
const token='TOKEN',$=id=>document.getElementById(id);
const words={ready:'Waiting for the night.',requesting:'Consulting the subject.',reading_reference:'Consulting the subject.',openai_generating:'Composing a household.',downloading:'Receiving them.',decoding:'Developing the photograph.',dithering_C:'Reducing them to six colours.',saving_sd:'Filing the household.',refreshing_display:'Hanging the photograph.',complete:'Assigned.',imported_sample:'Assigned.',no_subject:'We don’t know who you are yet.',api_key_missing:'Your authorization could not be verified.',api_key_rejected:'Your authorization could not be verified.',wifi_unreachable:'Your family is unreachable right now.',setup_open:'Contact is being established.'};
const steps=['reading_reference','openai_generating','downloading','decoding','dithering_C','saving_sd','refreshing_display'];
const origins={night:'assigned by the night',request:'assigned on request',subject:'assigned when the subject changed'};
let last='',hourSet=false;
for(let h=0;h<24;h++){const o=document.createElement('option');o.value=h;o.textContent=String(h).padStart(2,'0')+':00';$('hour').append(o)}
async function post(path,body){const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'token='+token+(body||'')});return [r.ok,await r.text()]}
$('assign').onclick=async()=>{$('assign').disabled=true;const [ok,t]=await post('/generate');if(!ok)$('state').textContent=words[t]||t;poll(true)};
$('hour').onchange=async()=>{const [ok,t]=await post('/schedule','&hour='+$('hour').value);$('next').textContent=t;poll(true)};
$('awake').onclick=()=>post('/awake').then(()=>poll(true));
function mmss(s){return Math.floor(s/60)+':'+String(s%60).padStart(2,'0')}
async function poll(once){try{const s=await(await fetch('/generation-status',{cache:'no-store'})).json();
$('state').textContent=words[s.phase]||'No family could be assigned tonight.';
const i=steps.indexOf(s.phase);$('prog').parentNode.classList.toggle('on',s.busy);$('prog').style.width=s.busy?Math.max(6,(i+1)/steps.length*100)+'%':'0';
$('assign').disabled=s.busy||!s.subject;
$('no').textContent=s.household?String(s.household).padStart(4,'0'):'—';
$('scene').textContent=s.scene?s.scene.charAt(0).toUpperCase()+s.scene.slice(1)+'.':'No household has been assigned.';
$('when').textContent=s.assigned_at?(s.assigned_at+' — '+(origins[s.origin]||'assigned')):'';
$('subject').textContent=s.subject?'Registered. The subject remains.':'We don’t know who you are yet.';
if(!hourSet||document.activeElement!==$('hour')){$('hour').value=s.night_hour;hourSet=true}
$('next').textContent='Next reassignment '+s.next+'. The previous household will not be kept.';
if(s.power==='usb'){$('power').textContent='Connected to power. Always awake.';$('awake').hidden=true}
else{$('power').textContent='On battery'+(s.battery_pct>=0?' · '+s.battery_pct+'%':'')+(s.sleep_in>0?' · sleeps in '+mmss(s.sleep_in):' · going to sleep');$('awake').hidden=false}
$('boot').textContent='Woken by: '+s.boot.replace('_',' ');
if(!s.busy&&s.frame_crc!==last){last=s.frame_crc;$('preview').src='/preview.bmp?v='+last}
}catch(e){$('state').textContent='The device is asleep or unreachable.'}if(!once)setTimeout(poll,2000)}
$('preview').onerror=()=>{$('frame').innerHTML='<div class="void">No household hangs here yet.</div>'};
poll();
</script></body></html>)HTML";

static const char FACE_HTML[] PROGMEM = FAMILY_HEAD("The subject — The Familly I Never Had") R"HTML(<body><div class="wrap">)HTML"
FAMILY_MAST("Every household is built around one face. It is the only thing that is kept.")
R"HTML(<main class="grid">
<figure class="plate"><div class="frame portrait"><div class="void" id="void">No subject has been registered.</div><img id="preview" alt="The subject's reference photograph" hidden></div>
<figcaption><span class="no">The subject</span><span id="dims">Unknown.</span></figcaption></figure>
<aside class="ledger">
<section><p class="label">I · The photograph</p><h2 id="heading">We don’t know who you are yet.</h2><p>A clear photograph of only you: whole face visible, good light.</p>
<div class="row"><label class="button ghost" for="photo">Choose a photograph</label><input id="photo" type="file" accept="image/jpeg,image/png,image/webp"></div></section>
<section><p class="label">II · Consent</p><label class="check"><input id="consent" type="checkbox"><span>This photograph may be sent to OpenAI every time a household is assigned.</span></label></section>
<section><p class="label">III · Registration</p><div class="row"><button id="save" disabled>Register the subject</button></div>
<p class="state" id="state" role="status" aria-live="polite">Checking the device…</p>
<p class="fine">Registering replaces the subject and immediately assigns a new household. The photograph is reduced in your browser to at most 1024 pixels as a JPEG, which strips its metadata, and kept on this device’s SD card. The card is not encrypted, and deleting it there is not secure erasure.</p></section>
<section><div class="row"><a href="/">Return to the household</a><button class="ghost" id="remove" hidden>Forget the subject</button></div></section>
</aside></main>
<footer><span>The subject remains. Everyone else is invented.</span></footer></div>
<script nonce="TOKEN">
const headers={'X-Family-CSRF':'TOKEN'},$=id=>document.getElementById(id);let prepared=null,url=null;
function show(blob){if(url)URL.revokeObjectURL(url);url=URL.createObjectURL(blob);$('preview').src=url;$('preview').hidden=false;$('void').hidden=true}
function hide(){$('preview').hidden=true;$('void').hidden=false}
async function status(){const r=await fetch('/face/status',{headers,cache:'no-store'});if(!r.ok)throw Error(await r.text());const s=await r.json();
$('heading').textContent=s.registered?'A subject has been registered.':'We don’t know who you are yet.';
$('dims').textContent=s.registered?s.width+' × '+s.height+' pixels, kept on the device.':'Unknown.';$('remove').hidden=!s.registered;
if(!s.api_key_configured)$('state').textContent='This device has no OpenAI key; households cannot be assigned.';return s}
function ready(){$('save').disabled=!(prepared&&$('consent').checked)}
$('consent').onchange=ready;
$('photo').onchange=async()=>{prepared=null;ready();const f=$('photo').files[0];if(!f)return;
if(f.size>25*1024*1024){$('state').textContent='Choose a photograph smaller than 25 MB.';return}
$('state').textContent='Preparing the photograph here, in your browser…';
try{const b=await createImageBitmap(f,{imageOrientation:'from-image'}),k=Math.min(1,1024/Math.max(b.width,b.height)),c=document.createElement('canvas');
c.width=Math.round(b.width*k);c.height=Math.round(b.height*k);if(Math.min(c.width,c.height)<128){b.close();throw Error('Use a photograph at least 128 pixels on each side.')}
const x=c.getContext('2d');x.fillStyle='#fff';x.fillRect(0,0,c.width,c.height);x.drawImage(b,0,0,c.width,c.height);b.close();
prepared=await new Promise(r=>c.toBlob(r,'image/jpeg',.88));if(prepared&&prepared.size>1048576)prepared=await new Promise(r=>c.toBlob(r,'image/jpeg',.72));
if(!prepared||prepared.size>1048576)throw Error('The photograph is still too large. Try a simpler one.');
show(prepared);$('state').textContent='Nothing has been sent yet. Confirm consent, then register.';ready()}catch(e){prepared=null;$('state').textContent=e.message||'This photograph could not be opened. Try a JPEG.'}};
async function follow(){for(let i=0;i<90;i++){await new Promise(r=>setTimeout(r,2000));try{const s=await(await fetch('/generation-status',{cache:'no-store'})).json();
if(!s.busy){$('state').textContent=s.phase==='complete'?'A new household has been assigned. Look at the device.':'The subject was registered, but no household could be assigned.';return}
$('state').textContent='The subject has changed. A new household is being assigned…'}catch(e){}}}
$('save').onclick=async()=>{if(!prepared||!$('consent').checked)return;$('save').disabled=true;$('photo').disabled=true;$('state').textContent='Registering the subject…';
try{const form=new FormData();form.append('consent','openai');form.append('face',prepared,'reference.jpg');
const r=await fetch('/face/upload',{method:'POST',headers,body:form});if(!r.ok)throw Error(await r.text());const j=await r.json();prepared=null;await status();
if(j.generation_triggered){$('state').textContent='The subject has changed. A new household is being assigned…';follow()}else $('state').textContent='The subject was registered. '+(j.note||'')}
catch(e){$('state').textContent=e.message}finally{$('photo').disabled=false;ready()}};
$('remove').onclick=async()=>{if(!confirm('Forget the subject? Ordinary deletion is not secure erasure.'))return;
const r=await fetch('/face/delete',{method:'POST',headers});$('state').textContent=r.ok?'The subject has been forgotten.':await r.text();if(r.ok){hide();await status()}};
status().then(async s=>{if(s.registered){const r=await fetch('/face/reference.jpg',{headers,cache:'no-store'});if(r.ok)show(await r.blob())}if(s.api_key_configured)$('state').textContent=s.registered?'The subject remains. You may replace them.':'Choose a photograph to begin.'}).catch(e=>$('state').textContent=e.message);
</script></body></html>)HTML";

static const char SETUP_HTML[] PROGMEM = FAMILY_HEAD("Establish contact — The Familly I Never Had") R"HTML(<body><div class="wrap" style="max-width:640px">)HTML"
FAMILY_MAST("Your family is unreachable right now.")
R"HTML(<main style="margin-top:36px"><p class="label">Establish contact</p><h2>Give the household a way home.</h2>
<form method="post" action="/wifi" class="ledger"><input type="hidden" name="csrf" value="TOKEN">
<section><label>Wi-Fi network (2.4 GHz)<input type="text" name="ssid" required maxlength="32" autocomplete="off"></label></section>
<section><label>Wi-Fi password<input type="password" name="password" minlength="8" maxlength="63" required autocomplete="off"></label></section>
<section><button>Establish connection</button><p class="fine">Credentials go only to this device and are stored unencrypted in its memory. After it connects, open the address shown on the display to register the subject.</p></section></form>
<p class="state" id="state" role="status" aria-live="polite">Checking the connection…</p></main>
<footer><span>The subject remains. The household does not.</span></footer></div>
<script>setInterval(async()=>{try{document.getElementById('state').textContent=await(await fetch('/status',{cache:'no-store'})).text()}catch(e){document.getElementById('state').textContent='The setup hotspot has closed. Rejoin your usual Wi-Fi.'}},2000)</script></body></html>)HTML";
