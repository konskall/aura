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

// The phone sign-in pages look like aoraton.com's relay page (public/aura-phone/
// in its repository): its palette and card, aoraton's mark and Aura's wordmark
// joined by a line. Both marks are in the page itself: the device's CSP
// allows data: images and nothing from elsewhere.
//
// aoraton's mark: its public/splash/aoraton-mark-256.png at 112 px, as WebP
// (2,344 bytes).
constexpr std::string_view kAoratonMark =
    "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";
// Aura's wordmark: shared/assets/branding/source/Aura_Selas.svg, unaltered but
// for its outer attributes (no fixed size, a label for its title) and SVG 2's
// href for xlink:href. aura_file_transfer_tests compares it with the source.
constexpr std::string_view kAuraWordmark = R"AURA(<svg class="aura" viewBox="64 400 1136 420" role="img" aria-label="Aura"><defs>
<linearGradient id="color" x1="108" y1="560" x2="1158" y2="700" gradientUnits="userSpaceOnUse"><stop stop-color="#1ce8ed"/><stop offset=".36" stop-color="#20aef7"/><stop offset=".60" stop-color="#6385ff"/><stop offset=".83" stop-color="#c49aff"/><stop offset="1" stop-color="#9c64ee"/></linearGradient>
<linearGradient id="light" x1="0" y1="0" x2=".8" y2="1"><stop stop-color="#c2ffff" stop-opacity=".68"/><stop offset=".54" stop-color="#99edff" stop-opacity=".08"/><stop offset="1" stop-color="#e5caff" stop-opacity=".42"/></linearGradient>
<linearGradient id="fold" x1=".1" y1="0" x2=".8" y2="1"><stop stop-color="#0087e9"/><stop offset=".48" stop-color="#32dbed"/><stop offset="1" stop-color="#b0efff"/></linearGradient>
<linearGradient id="violet" x1="0" y1="1" x2="1" y2="0"><stop stop-color="#4070ff"/><stop offset=".53" stop-color="#9f7df4"/><stop offset="1" stop-color="#e0b8ff"/></linearGradient>
<linearGradient id="blue" x1="0" y1="0" x2="1" y2="1"><stop stop-color="#67e5ed"/><stop offset=".6" stop-color="#3baff6"/><stop offset="1" stop-color="#3577f9"/></linearGradient>
<path id="A" fill-rule="evenodd" d="M108 768 274 484C289 453 303 435 330 435 351 435 365 451 379 478L510 719C522 743 541 763 559 774 511 777 468 772 429 750L318 689C286 671 265 661 243 670 216 681 203 705 185 730 168 754 143 764 108 768ZM336 510 393 623C339 602 301 594 246 628Z"/>
<path id="u" d="M506 531H532C549 531 564 549 564 568V661C564 698 584 718 620 718 656 718 674 699 674 661V531H700C717 531 732 549 732 568V665C732 735 688 777 620 777 549 777 506 735 506 665Z"/>
<path id="r" d="M748 773V633C748 570 781 531 840 531 873 531 906 542 966 527L913 577C895 595 881 602 859 598 822 591 805 611 805 650V728C805 753 786 773 761 773Z"/>
<path id="a" fill-rule="evenodd" d="M1013 529C1091 529 1125 581 1131 648L1143 734C1147 752 1152 764 1158 774H1112C1091 774 1081 761 1073 746 1053 766 1028 775 999 775 930 775 882 726 882 655 882 583 936 529 1013 529ZM1012 586C971 586 942 615 942 655 942 694 969 718 1006 718 1044 718 1073 692 1073 651 1073 612 1050 586 1012 586Z"/>
<clipPath id="clipA"><use href="#A"/></clipPath>
<clipPath id="clipU"><use href="#u"/></clipPath>
<clipPath id="clipR"><use href="#r"/></clipPath>
<clipPath id="clipa"><use href="#a"/></clipPath>
</defs>
<g fill="url(#color)"><use href="#A"/><use href="#u"/><use href="#r"/><use href="#a"/></g>
<g clip-path="url(#clipA)">
<path fill="url(#light)" d="M270 482C305 418 353 418 379 478L559 774C507 764 463 726 434 676L336 510C315 469 299 466 270 482Z"/>
<path fill="url(#fold)" d="M108 768 274 484C297 461 320 473 336 510L246 628C310 588 359 613 425 664L559 774C500 772 473 759 429 750L318 689C286 671 265 661 243 670 216 681 203 705 185 730 168 754 143 764 108 768Z"/></g>
<g clip-path="url(#clipU)">
<path fill="url(#blue)" d="M506 531H564V661C564 698 584 718 620 718 656 718 674 699 674 661 673 711 654 745 615 748 556 750 506 701 506 643Z"/>
<path fill="url(#light)" d="M506 643C519 709 574 747 615 748 673 750 716 718 733 663V791H500Z"/></g>
<g clip-path="url(#clipR)">
<path fill="url(#violet)" d="M738 646C777 578 838 550 966 527L913 577C895 595 881 602 859 598 822 591 805 611 805 650V728C790 758 770 770 748 773Z"/>
<path fill="url(#light)" d="M742 639C793 556 854 554 966 527 904 546 870 551 831 563 790 576 764 601 742 639Z"/></g>
<g clip-path="url(#clipa)">
<path fill="url(#violet)" d="M882 655C882 583 936 529 1013 529 1069 529 1101 554 1119 596 1074 554 1010 557 972 580 932 603 921 643 948 693 974 746 1046 749 1073 746 1004 813 882 774 882 655Z"/>
<path fill="url(#light)" d="M955 550C1030 522 1080 543 1107 576 1138 615 1131 730 1158 774H1112C1091 774 1081 761 1073 746 1094 706 1093 645 1073 610 1045 565 1000 555 955 550Z"/></g></svg>)AURA";
constexpr std::string_view kPhoneCheck =
    "<span class=\"check\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#60a5fa\" "
    "stroke-width=\"3\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"
    "<path d=\"M5 12.5l4.5 4.5L19 7.5\"/></svg></span>";
constexpr std::string_view kPhoneStyle =
    ":root{color-scheme:dark}*{box-sizing:border-box}html{background:#020617}body{margin:0;"
    "min-height:100vh;min-height:100dvh;display:grid;place-items:center;"
    "padding:max(24px,env(safe-area-inset-top)) 16px max(24px,env(safe-area-inset-bottom));"
    "background:radial-gradient(44rem 28rem at 12% -12%,rgba(59,130,246,.22),transparent 62%),radial-gradient(38rem 26rem at 105% -4%,rgba(99,102,241,.18),transparent 60%),#020617;"
    "color:#94a3b8;font:400 15.5px/1.6 system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;"
    "-webkit-font-smoothing:antialiased;text-align:center}main{width:min(100%,440px);"
    "padding:36px 28px 30px;border:1px solid rgba(51,65,85,.55);border-radius:24px;"
    "background:rgba(15,23,42,.64);box-shadow:0 1px 2px rgba(0,0,0,.3),0 32px 64px -32px rgba(0,0,0,.8)}"
    ".handoff{display:flex;align-items:center;justify-content:center;gap:14px;margin-bottom:30px}.mark{"
    "width:56px;height:56px;flex:none}.aura{height:44px;width:auto;flex:none}.wire{position:relative;"
    "flex:0 1 112px;min-width:44px;height:2px;border-radius:2px;background:rgba(148,163,184,.22)}.beam{"
    "position:absolute;inset:-3px 0;opacity:0;"
    "background:radial-gradient(circle at 4px 50%,#fff 0 1.5px,#6385ff 2.5px 4px,transparent 4.5px),linear-gradient(270deg,transparent 35%,rgba(32,174,247,.55) 92%,transparent 96%) center/100% 2px no-repeat;"
    "animation:travel .95s cubic-bezier(.45,0,.25,1) infinite}@keyframes travel{0%{"
    "transform:translateX(60%);opacity:0}18%,82%{opacity:1}100%{transform:none;opacity:0}}"
    "[data-state=done] .wire{background:linear-gradient(90deg,#2563eb,#6385ff)}.check{position:absolute;"
    "left:50%;top:50%;width:26px;height:26px;margin:-13px 0 0 -13px;border-radius:50%;display:grid;"
    "place-items:center;background:#0b1224;border:1.5px solid #60a5fa}.check svg{width:13px;height:13px}"
    "[data-state=cancelled] .wire,[data-state=gone] .wire{"
    "background:repeating-linear-gradient(90deg,rgba(148,163,184,.3) 0 6px,transparent 6px 12px)}h1{"
    "margin:0 0 10px;color:#f8fafc;font-size:24px;line-height:1.25;font-weight:680;"
    "letter-spacing:-.015em;text-wrap:balance}p{margin:0 auto;max-width:34ch;text-wrap:pretty}p+p{"
    "margin-top:10px}[lang=en]{color:#7c8ba1;font-size:13.5px;line-height:1.55}a{display:inline-block;"
    "margin-top:24px;padding:13px 24px;border-radius:999px;"
    "background:linear-gradient(100deg,#2563eb,#4f46e5 60%,#6366f1);color:#fff;font-weight:600;"
    "text-decoration:none;box-shadow:0 10px 24px -12px rgba(79,70,229,.7)}a:focus-visible{"
    "outline:2px solid #60a5fa;outline-offset:3px}@media (prefers-reduced-motion:reduce){.beam{"
    "display:none}}";
}  // namespace
std::string phone_sign_in_page(PhoneSignInPage page, std::string_view google_url) {
    const char* step = "wait";
    const char* title = "Σύνδεση στο Aoraton";
    const char* greek = "";
    const char* english = "";
    switch (page) {
        case PhoneSignInPage::Wait:
            greek = "Η συσκευή ετοιμάζει τη σύνδεση. Σε λίγο η σελίδα συνεχίζει μόνη της στο Google.";
            english = "The device is getting the sign-in ready. In a moment this page moves on to Google by itself.";
            break;
        case PhoneSignInPage::Google:
            step = "google";
            greek = "Συνέχεια στο Google, για να συνδεθεί η συσκευή στον λογαριασμό σου στο aoraton.com. Αν δεν "
                    "γίνει τίποτα, πάτησε το κουμπί.";
            english = "Continuing to Google, to sign the device in to your aoraton.com account. If nothing happens, "
                      "tap the button.";
            break;
        case PhoneSignInPage::Done:
            step = "done";
            title = "Η συσκευή συνδέεται";
            greek = "Ο λογαριασμός σου επιβεβαιώθηκε και η συσκευή ολοκληρώνει τη σύνδεση. Μπορείς να κλείσεις αυτή "
                    "τη σελίδα.";
            english = "Your account is confirmed and the device is finishing the sign-in. You can close this page.";
            break;
        case PhoneSignInPage::Cancelled:
            step = "cancelled";
            title = "Η σύνδεση ακυρώθηκε";
            greek = "Η συσκευή δεν συνδέθηκε. Για να ξαναδοκιμάσεις, ξεκίνα τη σύνδεση από τη συσκευή.";
            english = "The device did not sign in. To try again, start the sign-in on the device.";
            break;
        case PhoneSignInPage::Gone:
            step = "gone";
            title = "Ο κωδικός QR δεν ισχύει";
            greek = "Έληξε ή χρησιμοποιήθηκε ήδη. Ξεκίνα ξανά τη σύνδεση από τη συσκευή για νέο κωδικό.";
            english = "It has expired or was already used. Start the sign-in on the device again for a new code.";
            break;
    }
    const auto url = attribute(google_url);
    std::string html = "<!doctype html>\n<html lang=\"el\" data-state=\"";
    html += step;
    html += "\"><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
            "<meta name=\"referrer\" content=\"no-referrer\">";
    if (page == PhoneSignInPage::Wait) html += "<meta http-equiv=\"refresh\" content=\"1\">";
    if (page == PhoneSignInPage::Google) html += "<meta http-equiv=\"refresh\" content=\"0;url=" + url + "\">";
    html += "<title>Σύνδεση της Aura</title>\n<style>";
    html += kPhoneStyle;
    html += "</style>\n<main><div class=\"handoff\"><img class=\"mark\" src=\"data:image/webp;base64,";
    html += kAoratonMark;
    html += "\" alt=\"aoraton\" width=\"56\" height=\"56\"><span class=\"wire\" aria-hidden=\"true\">";
    // On the way to aoraton a light travels from Aura to its mark; signed in,
    // the two are joined; refused or expired, the line is broken.
    if (page == PhoneSignInPage::Wait || page == PhoneSignInPage::Google) html += "<span class=\"beam\"></span>";
    if (page == PhoneSignInPage::Done) html += kPhoneCheck;
    html += "</span>";
    html += kAuraWordmark;
    html += "</div><h1>";
    html += title;
    html += "</h1><p>";
    html += greek;
    html += "</p><p lang=\"en\">";
    html += english;
    html += "</p>";
    if (page == PhoneSignInPage::Google) html += "<a href=\"" + url + "\">Συνέχεια στο Google</a>";
    html += "</main></html>";
    return html;
}
}  // namespace aura::l10n
