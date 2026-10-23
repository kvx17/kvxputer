#pragma once

#include <Arduino.h>

// Single mobile page for File Share. Served with send_P; keep small.
// Keep in flash as a plain const (ESP32 maps flash); avoid PROGMEM send quirks.
static const char FILE_SHARE_PAGE[] = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>kvxputer File Share</title>
<style>
*{box-sizing:border-box}
body{margin:0;font-family:system-ui,sans-serif;background:#111;color:#eee}
header{padding:12px 14px;background:#1a1a1a;border-bottom:1px solid #333;position:sticky;top:0;z-index:2}
h1{margin:0;font-size:1.1rem}
.meta{font-size:.75rem;color:#aaa;margin-top:4px}
#login{max-width:360px;margin:40px auto;padding:16px}
#login input{width:100%;padding:10px;margin:6px 0;border:1px solid #444;border-radius:6px;background:#222;color:#eee}
button,select{padding:8px 12px;border:1px solid #555;border-radius:6px;background:#2a2a2a;color:#eee;cursor:pointer}
button.primary{background:#3a5a3a;border-color:#4a7a4a}
button.danger{background:#5a3a3a;border-color:#7a4a4a}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0}
#app{display:none;padding:10px 14px 80px}
#crumbs{font-size:.85rem;word-break:break-all;margin:6px 0}
#crumbs a{color:#8cf;text-decoration:none}
#search{flex:1;min-width:120px;padding:8px;border:1px solid #444;border-radius:6px;background:#222;color:#eee}
#list{list-style:none;padding:0;margin:0}
#list li{display:flex;align-items:center;gap:8px;padding:10px 8px;border-bottom:1px solid #2a2a2a}
#list li:active{background:#222}
.name{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.dir .name{color:#8cf;font-weight:600}
.size{font-size:.75rem;color:#888;white-space:nowrap}
.ops button{padding:4px 8px;font-size:.75rem}
#status{font-size:.8rem;color:#8a8;min-height:1.2em;margin-top:6px}
#drop{border:2px dashed #444;border-radius:8px;padding:16px;text-align:center;color:#888;margin:10px 0}
#drop.over{border-color:#6a6;background:#1a2a1a;color:#aea}
.hidden{display:none!important}
</style>
</head>
<body>
<div id="login">
  <h1>File Share</h1>
  <p class="meta">Portal login (shown on the device)</p>
  <input id="user" type="text" placeholder="Username" autocomplete="username" value="kvx">
  <input id="pwd" type="password" placeholder="Portal password" autocomplete="current-password">
  <button class="primary" style="width:100%;margin-top:8px" onclick="doLogin()">Login</button>
  <div id="loginErr" class="meta" style="color:#f88"></div>
</div>
<div id="app">
  <header>
    <h1>kvxputer File Share</h1>
    <div class="meta" id="hdrMeta"></div>
    <div class="row">
      <select id="vol" onchange="chgVol()"></select>
      <button onclick="doLogout()">Logout</button>
    </div>
  </header>
  <div id="crumbs"></div>
  <div class="row">
    <input id="search" placeholder="Filter this folder" oninput="render()">
    <select id="sort" onchange="render()">
      <option value="name">Name</option>
      <option value="size">Size</option>
    </select>
    <button onclick="loadList()">Refresh</button>
  </div>
  <div id="writes" class="row">
    <button class="primary" onclick="document.getElementById('fileIn').click()">Upload</button>
    <input id="fileIn" type="file" multiple class="hidden" onchange="uploadFiles(this.files)">
    <button onclick="doMkdir()">New folder</button>
  </div>
  <div id="drop">Drop files here to upload</div>
  <div id="status"></div>
  <ul id="list"></ul>
</div>
<script>
let path='/', vol='sd', items=[], readOnly=false, q='';
const $=id=>document.getElementById(id);
function fmt(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' kB';return (n/1048576).toFixed(1)+' MB'}
async function api(url,opts){
  const r=await fetch(url,opts||{});
  if(r.status===401){showLogin();throw new Error('auth');}
  return r;
}
function showLogin(){$('login').style.display='block';$('app').style.display='none'}
function showApp(){$('login').style.display='none';$('app').style.display='block'}
async function doLogin(){
  $('loginErr').textContent='';
  const body=new URLSearchParams({username:$('user').value,password:$('pwd').value});
  const r=await fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!r.ok){$('loginErr').textContent='Login failed';return}
  showApp();await boot();
}
async function doLogout(){await fetch('/logout',{method:'POST'});showLogin()}
async function boot(){
  const r=await api('/api/status');
  if(!r.ok){showLogin();return}
  const s=await r.json();
  readOnly=!!s.readOnly;
  $('writes').classList.toggle('hidden',readOnly);
  $('drop').classList.toggle('hidden',readOnly);
  const sel=$('vol');sel.innerHTML='';
  if(s.sd){const o=document.createElement('option');o.value='sd';o.textContent='SD ('+s.sd.free+' free)';sel.appendChild(o)}
  {const o=document.createElement('option');o.value='lfs';o.textContent='LittleFS ('+s.lfs.free+' free)';sel.appendChild(o)}
  if(!s.sd&&vol==='sd')vol='lfs';
  sel.value=vol;
  $('hdrMeta').textContent=(readOnly?'Read-only · ':'')+'clients '+s.clients;
  await loadList();
}
function chgVol(){vol=$('vol').value;path='/';loadList()}
function crumbs(){
  const parts=path.split('/').filter(Boolean);
  let html='<a href="#" data-p="/">/</a>',cur='';
  for(const p of parts){cur+='/'+p;html+=' / <a href="#" data-p="'+cur+'">'+p+'</a>'}
  $('crumbs').innerHTML=html;
  $('crumbs').onclick=e=>{const a=e.target.closest('a');if(!a)return;e.preventDefault();path=a.dataset.p;loadList()}
}
async function loadList(){
  $('status').textContent='Loading…';
  const r=await api('/api/list?vol='+encodeURIComponent(vol)+'&path='+encodeURIComponent(path));
  if(!r.ok){$('status').textContent=await r.text();items=[];render();return}
  const j=await r.json();
  items=j.items||[];
  path=j.path||path;
  crumbs();
  $('status').textContent=items.length+' items';
  render();
}
function render(){
  q=$('search').value.toLowerCase();
  const sort=$('sort').value;
  let list=items.filter(i=>!q||i.name.toLowerCase().includes(q));
  list.sort((a,b)=>{
    if(a.dir!==b.dir)return a.dir?-1:1;
    if(sort==='size')return (a.size||0)-(b.size||0);
    return a.name.localeCompare(b.name);
  });
  const ul=$('list');ul.innerHTML='';
  for(const it of list){
    const li=document.createElement('li');
    if(it.dir)li.className='dir';
    const nm=document.createElement('span');nm.className='name';nm.textContent=(it.dir?'📁 ':'')+it.name;
    nm.onclick=()=>{if(it.dir){path=(path==='/'?'':path)+'/'+it.name;loadList()}else download(it.name)};
    li.appendChild(nm);
    if(!it.dir){const sz=document.createElement('span');sz.className='size';sz.textContent=fmt(it.size||0);li.appendChild(sz)}
    if(!readOnly){
      const ops=document.createElement('span');ops.className='ops';
      const rb=document.createElement('button');rb.textContent='✎';rb.onclick=e=>{e.stopPropagation();doRename(it.name)};
      const db=document.createElement('button');db.className='danger';db.textContent='✕';db.onclick=e=>{e.stopPropagation();doDelete(it.name,it.dir)};
      ops.appendChild(rb);ops.appendChild(db);li.appendChild(ops);
    }
    ul.appendChild(li);
  }
}
function download(name){
  const u='/api/download?vol='+encodeURIComponent(vol)+'&path='+encodeURIComponent((path==='/'?'':path)+'/'+name);
  location.href=u;
}
async function doMkdir(){
  const name=prompt('Folder name');if(!name)return;
  const body=new URLSearchParams({vol,path,name});
  const r=await api('/api/mkdir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  $('status').textContent=r.ok?'Created':await r.text();
  if(r.ok)loadList();
}
async function doRename(old){
  const name=prompt('Rename to',old);if(!name||name===old)return;
  const body=new URLSearchParams({vol,path,old,name});
  const r=await api('/api/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  $('status').textContent=r.ok?'Renamed':await r.text();
  if(r.ok)loadList();
}
async function doDelete(name,isDir){
  if(!confirm('Delete '+(isDir?'folder ':'')+name+'?'))return;
  const body=new URLSearchParams({vol,path,name});
  const r=await api('/api/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  $('status').textContent=r.ok?'Deleted':await r.text();
  if(r.ok)loadList();
}
async function uploadFiles(files){
  if(readOnly||!files||!files.length)return;
  for(const f of files){
    $('status').textContent='Uploading '+f.name+'…';
    const fd=new FormData();fd.append('file',f,f.name);
    const r=await api('/api/upload?vol='+encodeURIComponent(vol)+'&path='+encodeURIComponent(path),{method:'POST',body:fd});
    if(!r.ok){$('status').textContent=f.name+': '+await r.text();return}
  }
  $('status').textContent='Upload done';
  $('fileIn').value='';
  loadList();
}
const drop=$('drop');
['dragenter','dragover'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.add('over')}));
['dragleave','drop'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.remove('over')}));
drop.addEventListener('drop',e=>uploadFiles(e.dataTransfer.files));
$('pwd').addEventListener('keydown',e=>{if(e.key==='Enter')doLogin()});
fetch('/api/status').then(r=>{if(r.ok){showApp();boot()}else showLogin()}).catch(showLogin);
</script>
</body>
</html>
)=====";
