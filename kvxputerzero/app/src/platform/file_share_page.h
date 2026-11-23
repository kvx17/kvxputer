#pragma once

// Single-page File Share UI. Served as one HTML response.
static const char FILE_SHARE_PAGE[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>kvxputer File Share</title>
<style>
:root{
  --bg:#0f1115;--panel:#171a21;--panel2:#1e2330;--line:#2a3140;
  --ink:#e8ecf4;--muted:#8b95a8;--accent:#3dd68c;--accent2:#5b8cff;
  --danger:#e85d6a;--warn:#e0b35a;--sel:#243528;--selb:#3dd68c66;
  --radius:10px;--shadow:0 10px 30px #0006;
  font-family:"Segoe UI",system-ui,-apple-system,sans-serif;
}
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:var(--bg);color:var(--ink)}
button,input,select{font:inherit;color:inherit}
button{cursor:pointer;border:1px solid var(--line);background:var(--panel2);border-radius:8px;padding:7px 11px}
button:hover{border-color:#445}
button.primary{background:#1f3d2e;border-color:#2f6a4a;color:#c8f5dc}
button.danger{background:#3a1f24;border-color:#7a3a44;color:#ffc7cd}
button:disabled{opacity:.45;cursor:default}
input[type=text],input[type=password],input[type=search]{
  width:100%;padding:10px 12px;border-radius:8px;border:1px solid var(--line);
  background:var(--panel2);outline:none}
input:focus{border-color:var(--accent2)}
#login{min-height:100%;display:grid;place-items:center;padding:24px;
  background:radial-gradient(900px 400px at 20% -10%,#1b2a22 0%,transparent 60%),var(--bg)}
.card{width:min(400px,100%);background:var(--panel);border:1px solid var(--line);
  border-radius:16px;padding:22px;box-shadow:var(--shadow)}
.card h1{margin:0 0 6px;font-size:1.35rem}
.card p{margin:0 0 16px;color:var(--muted);font-size:.9rem}
.card label{display:block;font-size:.78rem;color:var(--muted);margin:10px 0 4px}
.card .err{color:var(--danger);min-height:1.2em;margin-top:10px;font-size:.85rem}
.card .primary{width:100%;margin-top:14px;padding:10px}
#app{display:none;height:100%;grid-template-rows:auto auto 1fr;grid-template-columns:1fr}
#app.on{display:grid}
header{display:flex;gap:12px;align-items:center;justify-content:space-between;
  padding:12px 16px;border-bottom:1px solid var(--line);background:linear-gradient(180deg,#1a1f2a,#141821)}
header .brand{display:flex;flex-direction:column;gap:2px}
header h1{margin:0;font-size:1.05rem}
header .meta{color:var(--muted);font-size:.78rem}
.toolbar{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:10px 16px;
  border-bottom:1px solid var(--line);background:var(--panel)}
.toolbar .sep{width:1px;height:22px;background:var(--line);margin:0 2px}
#crumbs{display:flex;flex-wrap:wrap;gap:4px;align-items:center;padding:8px 16px;
  border-bottom:1px solid var(--line);background:#12151c;font-size:.86rem}
#crumbs a{color:var(--accent2);text-decoration:none}
#crumbs a:hover{text-decoration:underline}
#crumbs .here{color:var(--muted)}
.main{display:grid;grid-template-columns:1.2fr .9fr;min-height:0}
@media (max-width:900px){.main{grid-template-columns:1fr}}
.pane{min-height:0;overflow:auto}
.files{padding:12px 16px 80px;border-right:1px solid var(--line)}
.preview{padding:12px 16px;background:#10131a}
.drop{border:1.5px dashed #3a4356;border-radius:12px;padding:14px;text-align:center;
  color:var(--muted);margin-bottom:12px;background:#141821}
.drop.over{border-color:var(--accent);background:#16241c;color:#b7efd0}
.viewbar{display:flex;gap:8px;align-items:center;margin-bottom:10px}
.viewbar input{flex:1}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(132px,1fr));gap:10px}
.rowlist{display:flex;flex-direction:column;gap:4px}
.item{position:relative;border:1px solid var(--line);border-radius:12px;background:var(--panel);
  padding:10px;cursor:default;user-select:none;transition:border-color .12s,background .12s}
.item:hover{border-color:#4a556d}
.item.selected{background:var(--sel);border-color:var(--selb)}
.item .check{position:absolute;top:8px;left:8px}
.item .thumb{height:84px;border-radius:8px;background:#0c0e13;display:grid;place-items:center;
  overflow:hidden;margin:4px 0 8px;color:var(--muted);font-size:.75rem}
.item .thumb img{max-width:100%;max-height:100%;object-fit:contain}
.item .name{font-size:.86rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.item .sub{font-size:.72rem;color:var(--muted);margin-top:2px}
.rowlist .item{display:grid;grid-template-columns:24px 28px 1fr auto;gap:10px;align-items:center;padding:8px 10px}
.rowlist .item .thumb{height:28px;width:28px;margin:0;font-size:.7rem}
.rowlist .item .check{position:static}
.preview h2{margin:0 0 8px;font-size:1rem}
.preview .empty{color:var(--muted);padding:24px 8px;text-align:center}
.preview .frame{border:1px solid var(--line);border-radius:12px;background:#0c0e13;min-height:180px;
  display:grid;place-items:center;overflow:auto;padding:10px}
.preview .frame img{max-width:100%;max-height:60vh;object-fit:contain}
.preview pre{margin:0;white-space:pre-wrap;word-break:break-word;font-size:.82rem;line-height:1.4;
  width:100%;text-align:left;color:#d7deea}
#status{position:fixed;left:16px;bottom:16px;right:16px;max-width:520px;padding:10px 12px;
  border-radius:10px;background:#1c2433ee;border:1px solid var(--line);color:var(--ink);
  font-size:.84rem;pointer-events:none;opacity:0;transform:translateY(8px);transition:.18s}
#status.on{opacity:1;transform:none}
.hidden{display:none!important}
.icon{width:28px;height:28px;border-radius:8px;display:grid;place-items:center;background:#222836;font-size:.75rem}
</style>
</head>
<body>
<div id="login">
  <div class="card">
    <h1>File Share</h1>
    <p>Sign in with the credentials shown on the Cardputer.</p>
    <label>Username</label>
    <input id="user" type="text" autocomplete="username" value="kvx">
    <label>Password</label>
    <input id="pwd" type="password" autocomplete="current-password">
    <button class="primary" id="loginBtn">Sign in</button>
    <div class="err" id="loginErr"></div>
  </div>
</div>
<div id="app">
  <header>
    <div class="brand">
      <h1>kvxputer File Share</h1>
      <div class="meta" id="hdrMeta">Home directory</div>
    </div>
    <div style="display:flex;gap:8px">
      <button id="logoutBtn">Log out</button>
    </div>
  </header>
  <div class="toolbar" id="toolbar">
    <button class="primary" id="uploadBtn">Upload</button>
    <input id="fileIn" type="file" multiple class="hidden">
    <button id="mkdirBtn">New folder</button>
    <button id="mkfileBtn">New file</button>
    <span class="sep"></span>
    <button id="cutBtn" disabled>Cut</button>
    <button id="copyBtn" disabled>Copy</button>
    <button id="pasteBtn" disabled>Paste</button>
    <button id="renameBtn" disabled>Rename</button>
    <button class="danger" id="delBtn" disabled>Delete</button>
    <span class="sep"></span>
    <button id="dlBtn" disabled>Download</button>
    <button id="selAllBtn">Select all</button>
    <button id="selNoneBtn">Clear</button>
  </div>
  <div id="crumbs"></div>
  <div class="main">
    <div class="pane files">
      <div class="drop" id="drop">Drop files here to upload · click Upload for multi-select</div>
      <div class="viewbar">
        <input id="search" type="search" placeholder="Filter this folder">
        <select id="sort">
          <option value="name">Name</option>
          <option value="size">Size</option>
          <option value="type">Type</option>
        </select>
        <select id="view">
          <option value="grid">Gallery</option>
          <option value="list">List</option>
        </select>
        <button id="refreshBtn">Refresh</button>
      </div>
      <div id="list" class="grid"></div>
    </div>
    <div class="pane preview">
      <h2>Preview</h2>
      <div class="empty" id="prevEmpty">Select a text or image file to preview.</div>
      <div class="frame hidden" id="prevFrame"></div>
      <div class="meta" id="prevMeta" style="margin-top:8px;color:var(--muted);font-size:.8rem"></div>
    </div>
  </div>
</div>
<div id="status"></div>
<script>
const $=id=>document.getElementById(id);
let path='/', items=[], selected=new Set(), clipboard={mode:null,paths:[]}, writable=true;
let statusTimer=0;

function toast(msg){
  const el=$('status'); el.textContent=msg; el.classList.add('on');
  clearTimeout(statusTimer); statusTimer=setTimeout(()=>el.classList.remove('on'),2600);
}
function fmt(n){n=+n||0; if(n<1024)return n+' B'; if(n<1048576)return (n/1024).toFixed(1)+' KB'; return (n/1048576).toFixed(1)+' MB'}
function join(a,b){ if(!a||a==='/') return '/'+b.replace(/^\/+/,''); return (a.replace(/\/+$/,'')+'/'+b.replace(/^\/+/,'')).replace(/\/+/g,'/')}
function parent(p){ if(!p||p==='/') return '/'; const s=p.replace(/\/+$/,'').split('/'); s.pop(); return s.length<=1?'/':s.join('/') }
function basename(p){ const s=p.replace(/\/+$/,'').split('/'); return s[s.length-1]||'/' }
function extOf(name){ const i=name.lastIndexOf('.'); return i<0?'':name.slice(i+1).toLowerCase() }
function kindOf(name,isDir){
  if(isDir) return 'dir';
  const e=extOf(name);
  if(['png','jpg','jpeg','gif','webp','bmp','svg'].includes(e)) return 'image';
  if(['txt','md','markdown','json','csv','log','css','js','ts','html','htm','xml','yml','yaml','toml','ini','cfg','conf','c','h','cpp','hpp','py','rs','go','sh','bat','env','gitignore'].includes(e)) return 'text';
  return 'file';
}
function isPreviewable(it){ return it && (it.kind==='image'||it.kind==='text') }

async function api(url, opts){
  const r=await fetch(url, opts||{});
  if(r.status===401){ showLogin(); throw new Error('auth'); }
  return r;
}
function showLogin(){ $('login').style.display='grid'; $('app').classList.remove('on') }
function showApp(){ $('login').style.display='none'; $('app').classList.add('on') }

async function doLogin(){
  $('loginErr').textContent='';
  const body=new URLSearchParams({username:$('user').value,password:$('pwd').value});
  const r=await fetch('/login',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  if(!r.ok){ $('loginErr').textContent='Bad credentials'; return }
  showApp(); await boot();
}
async function doLogout(){ await fetch('/logout',{method:'POST'}); showLogin() }

async function boot(){
  const r=await api('/api/status');
  if(!r.ok){ showLogin(); return }
  const s=await r.json();
  writable=!!s.writable;
  $('hdrMeta').textContent=(writable?'Read / write':'Read-only')+' · '+ (s.root||'home');
  ['uploadBtn','mkdirBtn','mkfileBtn','cutBtn','pasteBtn','renameBtn','delBtn','drop'].forEach(id=>{
    const el=$(id); if(!el) return;
    if(id==='drop') el.classList.toggle('hidden', !writable);
    else el.disabled = !writable && id!=='uploadBtn';
  });
  if(!writable){ $('uploadBtn').disabled=true; $('cutBtn').disabled=true; $('pasteBtn').disabled=true; $('renameBtn').disabled=true; $('delBtn').disabled=true; $('mkdirBtn').disabled=true; $('mkfileBtn').disabled=true; }
  await loadList();
}

function crumbs(){
  const parts=path.split('/').filter(Boolean);
  let html='<a href="#" data-p="/">home</a>', cur='';
  parts.forEach((p,i)=>{
    cur+='/'+p;
    if(i===parts.length-1) html+=' <span class="here">/</span> <span class="here">'+esc(p)+'</span>';
    else html+=' <span class="here">/</span> <a href="#" data-p="'+escAttr(cur)+'">'+esc(p)+'</a>';
  });
  $('crumbs').innerHTML=html;
}
function esc(s){ return String(s).replace(/[&<>"']/g,c=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;' }[c])) }
function escAttr(s){ return esc(s).replace(/`/g,'') }

async function loadList(){
  toast('Loading…');
  const r=await api('/api/list?path='+encodeURIComponent(path));
  if(!r.ok){ toast(await r.text()); items=[]; render(); return }
  const j=await r.json();
  path=j.path||path;
  items=(j.items||[]).map(it=>({...it, kind: kindOf(it.name, !!it.dir)}));
  selected.clear();
  crumbs(); render(); updateActions();
  toast(items.length+' items');
}

function render(){
  const q=$('search').value.trim().toLowerCase();
  const sort=$('sort').value;
  const view=$('view').value;
  let list=items.filter(i=>!q || i.name.toLowerCase().includes(q));
  list.sort((a,b)=>{
    if(a.dir!==b.dir) return a.dir?-1:1;
    if(sort==='size') return (a.size||0)-(b.size||0);
    if(sort==='type') return (a.kind||'').localeCompare(b.kind||'') || a.name.localeCompare(b.name);
    return a.name.localeCompare(b.name);
  });
  const box=$('list');
  box.className=view==='grid'?'grid':'rowlist';
  box.innerHTML='';
  if(path!=='/'){
    const up=document.createElement('div');
    up.className='item';
    up.innerHTML=view==='grid'
      ?'<div class="thumb">⬆</div><div class="name">..</div><div class="sub">Parent folder</div>'
      :'<span></span><div class="icon">⬆</div><div><div class="name">..</div><div class="sub">Parent</div></div><span></span>';
    up.ondblclick=()=>{ path=parent(path); loadList(); };
    up.onclick=()=>{ path=parent(path); loadList(); };
    box.appendChild(up);
  }
  for(const it of list){
    const full=join(path,it.name);
    const el=document.createElement('div');
    el.className='item'+(selected.has(full)?' selected':'');
    el.dataset.path=full;
    const icon=it.dir?'DIR':(it.kind==='image'?'IMG':(it.kind==='text'?'TXT':'FILE'));
    const thumb=it.kind==='image'
      ? '<img loading="lazy" src="/api/raw?path='+encodeURIComponent(full)+'" alt="">'
      : icon;
    if(view==='grid'){
      el.innerHTML='<input class="check" type="checkbox" '+(selected.has(full)?'checked':'')+'>'
        +'<div class="thumb">'+thumb+'</div>'
        +'<div class="name" title="'+escAttr(it.name)+'">'+esc(it.name)+'</div>'
        +'<div class="sub">'+(it.dir?'Folder':fmt(it.size))+'</div>';
    } else {
      el.innerHTML='<input class="check" type="checkbox" '+(selected.has(full)?'checked':'')+'>'
        +'<div class="icon">'+icon+'</div>'
        +'<div><div class="name">'+esc(it.name)+'</div><div class="sub">'+(it.dir?'Folder':fmt(it.size))+' · '+it.kind+'</div></div>'
        +'<span></span>';
    }
    const chk=el.querySelector('.check');
    chk.onclick=e=>{ e.stopPropagation(); toggleSelect(full, chk.checked); el.classList.toggle('selected', chk.checked); };
    el.onclick=e=>{
      if(e.target===chk) return;
      if(e.shiftKey || e.metaKey || e.ctrlKey){ toggleSelect(full, !selected.has(full)); render(); return; }
      selected.clear(); selected.add(full); render();
      if(it.dir){ /* single click selects; open on dblclick */ previewSelection(); }
      else previewSelection();
    };
    el.ondblclick=()=>{
      if(it.dir){ path=full; loadList(); }
      else if(isPreviewable(it)) previewSelection();
      else downloadPaths([full]);
    };
    box.appendChild(el);
  }
  updateActions();
}

function toggleSelect(p, on){ if(on) selected.add(p); else selected.delete(p); updateActions(); }
function selectedItems(){ return [...selected].map(p=>{
  const name=basename(p); const it=items.find(x=>join(path,x.name)===p); return {path:p,name, dir:it?!!it.dir:false, kind:it?it.kind:kindOf(name,false), size:it?it.size:0};
})}
function updateActions(){
  const n=selected.size, has=n>0;
  const onlyFiles=[...selected].every(p=>{ const it=items.find(x=>join(path,x.name)===p); return it && !it.dir; });
  $('dlBtn').disabled=!has || !onlyFiles;
  if(writable){
    $('cutBtn').disabled=!has;
    $('copyBtn').disabled=!has;
    $('renameBtn').disabled=n!==1;
    $('delBtn').disabled=!has;
    $('pasteBtn').disabled=!clipboard.mode || !clipboard.paths.length;
  } else {
    $('copyBtn').disabled=!has;
  }
}

async function previewSelection(){
  const sel=selectedItems();
  if(sel.length!==1 || !isPreviewable(sel[0]) || sel[0].dir){
    $('prevEmpty').classList.remove('hidden');
    $('prevFrame').classList.add('hidden');
    $('prevFrame').innerHTML='';
    $('prevMeta').textContent=sel.length? (sel.length+' selected') : '';
    return;
  }
  const it=sel[0];
  $('prevEmpty').classList.add('hidden');
  const frame=$('prevFrame'); frame.classList.remove('hidden'); frame.innerHTML='Loading…';
  $('prevMeta').textContent=it.name+' · '+fmt(it.size)+' · '+it.kind;
  if(it.kind==='image'){
    frame.innerHTML='';
    const img=document.createElement('img');
    img.src='/api/raw?path='+encodeURIComponent(it.path);
    img.alt=it.name;
    frame.appendChild(img);
  } else {
    const r=await api('/api/preview?path='+encodeURIComponent(it.path));
    if(!r.ok){ frame.textContent=await r.text(); return; }
    const j=await r.json();
    const pre=document.createElement('pre');
    pre.textContent=j.text||'';
    frame.innerHTML=''; frame.appendChild(pre);
    if(j.truncated) $('prevMeta').textContent+=' · truncated';
  }
}

async function downloadPaths(paths){
  for(const p of paths){
    const a=document.createElement('a');
    a.href='/api/download?path='+encodeURIComponent(p);
    a.download=basename(p);
    a.rel='noopener';
    document.body.appendChild(a); a.click(); a.remove();
  }
  toast('Downloading…');
}

async function uploadFiles(files){
  if(!writable || !files || !files.length) return;
  for(const f of files){
    toast('Uploading '+f.name+'…');
    const dest=join(path,f.name);
    const r=await api('/api/upload?path='+encodeURIComponent(dest),{method:'PUT',body:f,headers:{'Content-Type':'application/octet-stream'}});
    if(!r.ok){ toast(f.name+': '+await r.text()); return; }
  }
  $('fileIn').value='';
  toast('Upload done');
  await loadList();
}

async function doMkdir(){
  const name=prompt('Folder name'); if(!name) return;
  const r=await api('/api/mkdir',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({path:join(path,name)})});
  toast(r.ok?'Folder created':await r.text()); if(r.ok) loadList();
}
async function doMkfile(){
  const name=prompt('File name'); if(!name) return;
  const r=await api('/api/create',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({path:join(path,name)})});
  toast(r.ok?'File created':await r.text()); if(r.ok) loadList();
}
async function doRename(){
  const sel=selectedItems(); if(sel.length!==1) return;
  const name=prompt('Rename to', sel[0].name); if(!name || name===sel[0].name) return;
  const r=await api('/api/rename',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:new URLSearchParams({path:sel[0].path, name})});
  toast(r.ok?'Renamed':await r.text()); if(r.ok) loadList();
}
async function doDelete(){
  const sel=selectedItems(); if(!sel.length) return;
  if(!confirm('Delete '+sel.length+' item(s)?')) return;
  const r=await api('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({paths:sel.map(s=>s.path)})});
  toast(r.ok?'Deleted':await r.text()); if(r.ok) loadList();
}
function setClipboard(mode){
  clipboard={mode, paths:[...selected]};
  updateActions();
  toast((mode==='cut'?'Cut ':'Copied ')+clipboard.paths.length+' item(s)');
}
async function doPaste(){
  if(!clipboard.mode || !clipboard.paths.length) return;
  for(const src of clipboard.paths){
    const dest=join(path, basename(src));
    const endpoint=clipboard.mode==='cut'?'/api/move':'/api/copy';
    const r=await api(endpoint,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
      body:new URLSearchParams({src, dest})});
    if(!r.ok){ toast(await r.text()); return; }
  }
  if(clipboard.mode==='cut') clipboard={mode:null,paths:[]};
  toast('Paste done');
  await loadList();
}

$('loginBtn').onclick=doLogin;
$('pwd').addEventListener('keydown',e=>{ if(e.key==='Enter') doLogin(); });
$('logoutBtn').onclick=doLogout;
$('refreshBtn').onclick=()=>loadList();
$('search').oninput=()=>render();
$('sort').onchange=()=>render();
$('view').onchange=()=>render();
$('uploadBtn').onclick=()=>$('fileIn').click();
$('fileIn').onchange=e=>uploadFiles(e.target.files);
$('mkdirBtn').onclick=doMkdir;
$('mkfileBtn').onclick=doMkfile;
$('cutBtn').onclick=()=>setClipboard('cut');
$('copyBtn').onclick=()=>setClipboard('copy');
$('pasteBtn').onclick=doPaste;
$('renameBtn').onclick=doRename;
$('delBtn').onclick=doDelete;
$('dlBtn').onclick=()=>downloadPaths([...selected]);
$('selAllBtn').onclick=()=>{ items.forEach(it=>selected.add(join(path,it.name))); render(); updateActions(); };
$('selNoneBtn').onclick=()=>{ selected.clear(); render(); updateActions(); previewSelection(); };
$('crumbs').onclick=e=>{ const a=e.target.closest('a'); if(!a) return; e.preventDefault(); path=a.dataset.p||'/'; loadList(); };

const drop=$('drop');
['dragenter','dragover'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault(); drop.classList.add('over')}));
['dragleave','drop'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault(); drop.classList.remove('over')}));
drop.addEventListener('drop',e=>uploadFiles(e.dataTransfer.files));

fetch('/api/status').then(r=>{ if(r.ok){ showApp(); boot(); } else showLogin(); }).catch(showLogin);
</script>
</body>
</html>
)HTML";
