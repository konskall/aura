#include "aura/transfer_page.hpp"
namespace aura::l10n {
std::string_view transfer_page() {
    return R"AURA(<!doctype html>
<html lang="el"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="referrer" content="no-referrer"><title>AURA</title>
<style>
:root{color-scheme:dark;font:16px system-ui,sans-serif;background:#080b12;color:#f5f7ff}*{box-sizing:border-box}body{max-width:740px;margin:auto;padding:28px 20px}header{display:flex;align-items:center;justify-content:space-between;margin-bottom:34px}.brand{font-size:24px;font-weight:750;letter-spacing:.1em}h1{font-size:30px;letter-spacing:-.04em;margin:0 0 12px}p{color:#aeb9cf;line-height:1.6}.card{padding:26px;border:1px solid #293346;border-radius:24px;background:#121824;margin-bottom:20px}button,select,input{font:inherit;border-radius:14px;border:1px solid #35415a;padding:14px;background:#1c2536;color:inherit}button{cursor:pointer}button.primary{background:#35d5aa;color:#071e19;border:0;font-weight:750}button:disabled{opacity:.4;cursor:default}input#pin{letter-spacing:.3em;font-size:28px;width:100%;text-align:center;margin:12px 0 18px}button.wide{width:100%}#drop{padding:26px;border:1px dashed #52627b;border-radius:18px;text-align:center;cursor:pointer}#drop.over{background:#203a36;border-color:#35d5aa}select{width:100%;margin:12px 0}ul{list-style:none;padding:0;max-height:300px;overflow:auto}li{padding:12px 0;border-bottom:1px solid #293346;overflow-wrap:anywhere}small{color:#9ba8bf}progress{width:100%;height:18px;accent-color:#35d5aa;margin-top:12px}#message{white-space:pre-wrap;overflow-wrap:anywhere;color:#9ce7cc;min-height:1.5em}.error{color:#ff9eac!important}.muted{font-size:13px;color:#8594ae}.hidden{display:none!important}footer{font-size:13px;color:#8594ae;line-height:1.6}
</style>
<header><span class="brand">AURA</span><button id="language" type="button">English</button></header>
<h1 data-text="title"></h1><p data-text="intro"></p>
<section id="login" class="card"><label for="pin" data-text="pin"></label><input id="pin" inputmode="numeric" autocomplete="one-time-code" maxlength="6" pattern="[0-9]{6}"><button id="connect" class="primary wide" data-text="connect"></button></section>
<section id="upload" class="card hidden"><label for="area" data-text="where"></label><select id="area"><option value="auto" data-text="auto"></option><option value="photos" data-text="photos"></option><option value="files" data-text="files"></option></select><div id="drop" role="button" tabindex="0"><strong data-text="choose"></strong><p class="muted" data-text="limits"></p></div><input id="picker" type="file" multiple hidden><ul id="list"></ul><button id="send" class="primary wide" disabled data-text="send"></button><progress id="progress" value="0" max="100" hidden></progress></section>
<p id="message" role="status" aria-live="polite"></p><footer data-text="footer"></footer>
<section id="manage" class="card hidden"><h2 data-text="stored"></h2><label for="stored-area" data-text="where"></label><select id="stored-area"><option value="photos" data-text="photos"></option><option value="files" data-text="files"></option></select><button id="refresh" data-text="refresh"></button><ul id="stored-list"></ul><button id="more" class="hidden" data-text="more"></button></section>
<dialog id="delete-dialog"><h2 data-text="deleteTitle"></h2><p data-text="deleteHint"></p><p id="delete-name"></p><div class="actions"><button id="delete-cancel" data-text="cancel"></button><button id="delete-confirm" class="danger" data-text="remove"></button></div></dialog>
<style>footer{margin-bottom:24px}h2{font-size:22px;margin-top:0}#stored-list li{display:flex;align-items:center;gap:12px}#stored-list .filename{flex:1;min-width:0;overflow-wrap:anywhere}#stored-list button{flex-shrink:0}dialog{width:min(92vw,480px);border:1px solid #35415a;border-radius:24px;background:#121824;color:#f5f7ff;padding:26px}dialog::backdrop{background:#000b}.actions{display:flex;gap:12px;justify-content:flex-end}.danger{background:#542631;color:#ffd5dc;border-color:#86434f}#delete-name{overflow-wrap:anywhere;color:#f5f7ff}</style>
<script>
'use strict';
const copy={el:{title:'Μεταφορά αρχείων',intro:'Στείλε αρχεία απευθείας στην AURA μέσω του ίδιου έμπιστου Wi-Fi.',pin:'Κωδικός από την οθόνη της συσκευής',connect:'Σύνδεση',where:'Αποθήκευση',auto:'Αυτόματα ανάλογα με τον τύπο',photos:'Φωτογραφίες · JPG / PNG',files:'Άλλα αρχεία',choose:'Επίλεξε αρχεία ή σύρε τα εδώ',limits:'Φωτογραφίες έως 4 MB · Άλλα αρχεία έως 16 MB · έως 128 αρχεία κάθε φορά',send:'Αποστολή στη συσκευή',footer:'Οι φωτογραφίες αποθηκεύονται στο aura/photos και τα υπόλοιπα αρχεία στο aura/files. Η πρόσβαση λήγει σε 10 λεπτά ή όταν τη σταματήσεις στη συσκευή. Χρησιμοποίησε έμπιστο τοπικό Wi-Fi: η σύνδεση HTTP δεν είναι κρυπτογραφημένη.',ready:'Συνδέθηκες. Επίλεξε τα αρχεία που θέλεις να στείλεις.',sending:'Αποστολή',done:'Αποθηκεύτηκε',finished:'Η μεταφορά ολοκληρώθηκε.',auth:'Έλεγξε τον κωδικό στην οθόνη ή ενεργοποίησε ξανά τη μεταφορά.',expired:'Η πρόσβαση έληξε. Ενεργοποίησε ξανά τη μεταφορά στη συσκευή.',network:'Δεν υπάρχει σύνδεση. Έλεγξε ότι η μεταφορά είναι ενεργή και είστε στο ίδιο Wi-Fi.',storage:'Η αποθήκευση απέτυχε. Έλεγξε την κάρτα και τον ελεύθερο χώρο.',name:'Μη αποδεκτό όνομα ή τύπος αρχείου. Οι φωτογραφίες πρέπει να είναι JPG/PNG.',size:'Το αρχείο ή η εικόνα είναι πολύ μεγάλη. Αποθήκευσε μικρότερο αντίγραφο.',exists:'Υπάρχει ήδη αρχείο με αυτό το όνομα. Άλλαξε το όνομα και ξαναδοκίμασε.',image:'Η εικόνα δεν υποστηρίζεται ή είναι αλλοιωμένη. Δοκίμασε μικρότερο JPG/PNG.',interrupted:'Η μεταφορά διακόπηκε. Το ημιτελές αρχείο αφαιρέθηκε.',limit:'Επίλεξε έως 128 αρχεία. Η βιβλιοθήκη δέχεται έως 128 φωτογραφίες.'},en:{title:'File transfer',intro:'Send files directly to AURA on the same trusted Wi-Fi network.',pin:'Code shown on the device',connect:'Connect',where:'Destination',auto:'Automatic by file type',photos:'Photos · JPG / PNG',files:'Other files',choose:'Choose files or drop them here',limits:'Photos up to 4 MB · Other files up to 16 MB · up to 128 files at a time',send:'Send to device',footer:'Photos are saved in aura/photos and other files in aura/files. Access ends after 10 minutes or when stopped on the device. Use trusted local Wi-Fi: this HTTP connection is not encrypted.',ready:'Connected. Choose the files you want to send.',sending:'Sending',done:'Saved',finished:'Transfer complete.',auth:'Check the code on the device or enable file transfer again.',expired:'Access expired. Enable file transfer on the device again.',network:'Connection unavailable. Check that transfer is enabled and both devices use the same Wi-Fi.',storage:'Could not save the file. Check the card and available space.',name:'Invalid filename or file type. Photos must be JPG/PNG.',size:'The file or image is too large. Save a smaller copy.',exists:'A file with this name already exists. Rename it and try again.',image:'Unsupported or damaged image. Try a smaller JPG/PNG.',interrupted:'Transfer interrupted. The incomplete file was removed.',limit:'Choose up to 128 files. The library holds up to 128 photos.'}};
Object.assign(copy.el,{limits:'JPG/PNG · Αυτόματη προσαρμογή για την οθόνη · Άλλα αρχεία έως 16 MB',preparing:'Προσαρμογή φωτογραφίας',stored:'Αποθηκευμένα στη συσκευή',refresh:'Ανανέωση',more:'Επόμενη σελίδα',noFiles:'Δεν υπάρχουν αρχεία.',remove:'Διαγραφή',cancel:'Ακύρωση',deleteTitle:'Διαγραφή αρχείου;',deleteHint:'Το παρακάτω αρχείο θα διαγραφεί οριστικά από τη συσκευή.',deleted:'Το αρχείο διαγράφηκε.',missing:'Το αρχείο δεν υπάρχει πλέον. Ανανέωσε τη λίστα.',dimensions:'Η ανάλυση της εικόνας ξεπερνά το όριο. Επίλεξέ τη ξανά για αυτόματη προσαρμογή.',size:'Το αρχείο ξεπερνά το όριο αποστολής. Τα άλλα αρχεία μπορούν να είναι έως 16 MB.'});
Object.assign(copy.en,{limits:'JPG/PNG · Automatically fitted to the display · Other files up to 16 MB',preparing:'Preparing photo',stored:'Saved on the device',refresh:'Refresh',more:'Next page',noFiles:'No files here yet.',remove:'Delete',cancel:'Cancel',deleteTitle:'Delete file?',deleteHint:'This file will be permanently deleted from the device.',deleted:'File deleted.',missing:'This file no longer exists. Refresh the list.',dimensions:'The image resolution exceeds the limit. Select it again for automatic resizing.',size:'The file exceeds the upload limit. Other files can be up to 16 MB.'});
let lang='el',token='',selected=[],busy=false,manageBusy=false,storedAfter='',pendingDelete=null;
const $=id=>document.getElementById(id),t=key=>copy[lang][key]||copy[lang].network;
function message(text,error=false){$('message').textContent=text;$('message').classList.toggle('error',error)}
function translate(){document.documentElement.lang=lang;document.title='AURA · '+t('title');document.querySelectorAll('[data-text]').forEach(el=>el.textContent=t(el.dataset.text));$('language').textContent=lang==='el'?'English':'Ελληνικά';renderFiles()}
function renderFiles(){$('list').replaceChildren();for(const file of selected){const item=document.createElement('li');item.textContent=file.name+' · '+(file.size/1048576).toFixed(2)+' MB';$('list').appendChild(item)}$('send').disabled=busy||manageBusy||!selected.length}
function choose(files){if(busy)return;if(files.length>128){message(t('limit'),true);return}selected=Array.from(files);message('');$('progress').hidden=true;$('progress').value=0;renderFiles()}
$('language').onclick=()=>{lang=lang==='el'?'en':'el';translate()};
$('pin').oninput=()=>{$('pin').value=$('pin').value.replace(/[^0-9]/g,'')};
$('pin').onkeydown=event=>{if(event.key==='Enter')$('connect').click()};
$('connect').onclick=async()=>{if(!/^\d{6}$/.test($('pin').value)){message(t('auth'),true);return}$('connect').disabled=true;try{const response=await fetch('/api/session',{method:'POST',credentials:'omit',cache:'no-store',headers:{'Content-Type':'application/json'},body:JSON.stringify({pin:$('pin').value})});const data=await response.json();if(!response.ok)throw new Error(response.status===410?'expired':'auth');token=data.token;$('pin').value='';$('login').classList.add('hidden');$('upload').classList.remove('hidden');$('manage').classList.remove('hidden');message(t('ready'));loadStored()}catch(error){message(t(error.message),true)}finally{$('connect').disabled=false}};
$('drop').onclick=()=>{if(!busy)$('picker').click()};$('drop').onkeydown=event=>{if(event.key==='Enter'||event.key===' '){event.preventDefault();$('drop').click()}};
$('picker').onchange=()=>choose($('picker').files);
for(const type of ['dragenter','dragover'])$('drop').addEventListener(type,event=>{event.preventDefault();$('drop').classList.add('over')});
for(const type of ['dragleave','drop'])$('drop').addEventListener(type,event=>{event.preventDefault();$('drop').classList.remove('over')});
$('drop').addEventListener('drop',event=>choose(event.dataTransfer.files));
function upload(file,area){return new Promise((resolve,reject)=>{const xhr=new XMLHttpRequest();xhr.open('POST','/api/upload');xhr.timeout=120000;xhr.setRequestHeader('Authorization','Bearer '+token);xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.setRequestHeader('X-Aura-Filename',encodeURIComponent(file.name));xhr.setRequestHeader('X-Aura-Area',area);xhr.upload.onprogress=event=>{if(event.lengthComputable)$('progress').value=event.loaded*100/event.total};xhr.onerror=()=>reject(new Error('network'));xhr.ontimeout=()=>reject(new Error('interrupted'));xhr.onload=()=>{let data={};try{data=JSON.parse(xhr.responseText)}catch{}if(xhr.status===201)resolve(data);else reject(new Error(xhr.status===410?'expired':data.error||'network'))};xhr.send(file)})}
async function preparePhoto(file){if(!/\.(jpe?g|png)$/i.test(file.name))throw new Error('name');if(file.size>33554432)throw new Error('size');let bitmap;try{bitmap=await createImageBitmap(file,{imageOrientation:'from-image'});if(bitmap.width<=480&&bitmap.height<=480&&file.size<=4194304)return file;message(t('preparing')+' · '+file.name);const ratio=Math.min(1,480/Math.max(bitmap.width,bitmap.height));const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(bitmap.width*ratio));canvas.height=Math.max(1,Math.round(bitmap.height*ratio));const ctx=canvas.getContext('2d',{colorSpace:'srgb'});ctx.imageSmoothingEnabled=true;ctx.imageSmoothingQuality='high';ctx.drawImage(bitmap,0,0,canvas.width,canvas.height);const type=/\.png$/i.test(file.name)?'image/png':'image/jpeg';const blob=await new Promise(resolve=>canvas.toBlob(resolve,type,.94));if(!blob)throw new Error('image');return new File([blob],file.name,{type,lastModified:file.lastModified})}catch(error){throw new Error(error.message==='size'?'size':'image')}finally{if(bitmap)bitmap.close()}}
function sessionError(error){if(error.message==='expired'||error.message==='auth'){token='';$('login').classList.remove('hidden');$('upload').classList.add('hidden');$('manage').classList.add('hidden');$('stored-list').replaceChildren()}}
$('send').onclick=async()=>{if(busy||manageBusy||!token)return;busy=true;renderFiles();$('area').disabled=true;$('progress').hidden=false;try{for(const file of [...selected]){const mode=$('area').value;const area=mode==='auto'?(/\.(jpe?g|png)$/i.test(file.name)?'photos':'files'):mode;const payload=area==='photos'?await preparePhoto(file):file;const limit=area==='photos'?4194304:16777216;if(payload.size>limit)throw new Error('size');message(t('sending')+' · '+file.name);$('progress').value=0;await upload(payload,area);selected.shift();renderFiles();message(t('done')+' · '+file.name)}message(t('finished'))}catch(error){message(t(error.message),true);sessionError(error)}finally{busy=false;$('area').disabled=false;renderFiles();if(token)loadStored()}};
async function api(path,payload){const response=await fetch(path,{method:'POST',credentials:'omit',cache:'no-store',headers:{'Content-Type':'application/json',Authorization:'Bearer '+token},body:JSON.stringify(payload)});const data=await response.json();if(!response.ok)throw new Error(response.status===410?'expired':data.error||'network');return data}
async function loadStored(reset=true){if(!token||busy||manageBusy)return;manageBusy=true;renderFiles();$('refresh').disabled=true;$('stored-area').disabled=true;try{if(reset)storedAfter='';const area=$('stored-area').value;const data=await api('/api/files',{area,after:storedAfter});$('stored-list').replaceChildren();for(const file of data.files){const item=document.createElement('li');const name=document.createElement('span');name.className='filename';name.textContent=file.name+' · '+(file.bytes/1048576).toFixed(2)+' MB';const button=document.createElement('button');button.textContent=t('remove');button.className='danger';button.onclick=()=>{if(busy||manageBusy)return;pendingDelete={area,name:file.name};$('delete-name').textContent=file.name;$('delete-dialog').showModal()};item.append(name,button);$('stored-list').appendChild(item)}if(!data.files.length){const empty=document.createElement('li');empty.textContent=t('noFiles');$('stored-list').appendChild(empty)}if(data.files.length)storedAfter=data.files[data.files.length-1].name;$('more').classList.toggle('hidden',!data.more)}catch(error){message(t(error.message),true);sessionError(error)}finally{manageBusy=false;$('refresh').disabled=false;$('stored-area').disabled=false;renderFiles()}}
$('refresh').onclick=()=>loadStored();$('stored-area').onchange=()=>loadStored();$('more').onclick=()=>loadStored(false);
$('delete-cancel').onclick=()=>{pendingDelete=null;$('delete-dialog').close()};$('delete-dialog').oncancel=()=>{pendingDelete=null};
$('delete-confirm').onclick=async()=>{if(!pendingDelete||busy||manageBusy)return;const target=pendingDelete;pendingDelete=null;manageBusy=true;$('delete-confirm').disabled=true;$('delete-cancel').disabled=true;try{await api('/api/delete',{...target,confirm:true});message(t('deleted'))}catch(error){message(t(error.message),true);sessionError(error)}finally{manageBusy=false;$('delete-confirm').disabled=false;$('delete-cancel').disabled=false;$('delete-dialog').close();if(token)loadStored()}};
translate();
</script></html>)AURA";
}
namespace {
std::string attribute(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 32);
    for (const char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += ch; break;
        }
    }
    return out;
}
}  // namespace
std::string phone_sign_in_page(PhoneSignInPage page, std::string_view google_url) {
    const char* title = "Σύνδεση στο Aoraton";
    const char* greek = "";
    const char* english = "";
    switch (page) {
        case PhoneSignInPage::Wait:
            greek = "Η συσκευή ετοιμάζει τη σύνδεση. Σε λίγο η σελίδα συνεχίζει μόνη της στο Google.";
            english = "The device is getting the sign-in ready. In a moment this page moves on to Google by itself.";
            break;
        case PhoneSignInPage::Google:
            greek = "Συνέχεια στο Google, για να συνδεθεί η συσκευή στον λογαριασμό σου στο aoraton.com. Αν δεν "
                    "γίνει τίποτα, πάτησε το κουμπί.";
            english = "Continuing to Google, to sign the device in to your aoraton.com account. If nothing happens, "
                      "tap the button.";
            break;
        case PhoneSignInPage::Done:
            title = "Η συσκευή συνδέεται";
            greek = "Ο λογαριασμός σου επιβεβαιώθηκε και η συσκευή ολοκληρώνει τη σύνδεση. Μπορείς να κλείσεις αυτή "
                    "τη σελίδα.";
            english = "Your account is confirmed and the device is finishing the sign-in. You can close this page.";
            break;
        case PhoneSignInPage::Cancelled:
            title = "Η σύνδεση ακυρώθηκε";
            greek = "Η συσκευή δεν συνδέθηκε. Για να ξαναδοκιμάσεις, ξεκίνα τη σύνδεση από τη συσκευή.";
            english = "The device did not sign in. To try again, start the sign-in on the device.";
            break;
        case PhoneSignInPage::Gone:
            title = "Ο κωδικός QR δεν ισχύει";
            greek = "Έληξε ή χρησιμοποιήθηκε ήδη. Ξεκίνα ξανά τη σύνδεση από τη συσκευή για νέο κωδικό.";
            english = "It has expired or was already used. Start the sign-in on the device again for a new code.";
            break;
    }
    const auto url = attribute(google_url);
    std::string html =
        "<!doctype html>\n<html lang=\"el\"><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<meta name=\"referrer\" content=\"no-referrer\">";
    if (page == PhoneSignInPage::Wait) html += "<meta http-equiv=\"refresh\" content=\"1\">";
    if (page == PhoneSignInPage::Google) html += "<meta http-equiv=\"refresh\" content=\"0;url=" + url + "\">";
    html += "<title>AURA</title>\n<style>"
            ":root{color-scheme:dark;font:17px system-ui,sans-serif;background:#080b12;color:#f5f7ff}"
            "body{max-width:520px;margin:auto;padding:32px 22px}.brand{font-size:22px;font-weight:750;"
            "letter-spacing:.1em;margin:0}h1{font-size:28px;letter-spacing:-.03em;margin:32px 0 12px}"
            "p{color:#aeb9cf;line-height:1.6}p[lang=en]{font-size:14px;color:#8594ae}"
            "a{display:block;margin-top:28px;padding:16px;border-radius:14px;background:#35d5aa;color:#071e19;"
            "font-weight:750;text-align:center;text-decoration:none}"
            "</style>\n<p class=\"brand\">AURA</p><h1>";
    html += title;
    html += "</h1><p>";
    html += greek;
    html += "</p><p lang=\"en\">";
    html += english;
    html += "</p>";
    if (page == PhoneSignInPage::Google) html += "<a href=\"" + url + "\">Συνέχεια στο Google</a>";
    html += "</html>";
    return html;
}
}  // namespace aura::l10n
