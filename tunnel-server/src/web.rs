use crate::auth::{self, AppState};
use axum::http::header;
use axum::http::HeaderMap;
use axum::response::{IntoResponse, Redirect, Response};

/// 控制台入口：已登录返回页面，否则跳登录。
/// 由访客分发在“主域名且路径为 /”时调用（不能被子域名根路径抢走）。
pub async fn console_or_redirect(state: &AppState, headers: &HeaderMap) -> Response {
    let logged_in = if let Some(raw) = crate::util::parse_bearer(headers) {
        auth::api_token_user(&state.db, &raw).await.is_some()
    } else if let Some(sid) = crate::util::get_cookie(headers, "sid") {
        auth::session_user(&state.db, &sid).await.is_some()
    } else {
        false
    };
    if logged_in {
        (
            [(header::CONTENT_TYPE, "text/html; charset=utf-8")],
            DASHBOARD_HTML,
        )
            .into_response()
    } else {
        Redirect::to("/auth/login").into_response()
    }
}

const DASHBOARD_HTML: &str = r#"<!doctype html>
<html lang="zh"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tunnel 控制台</title>
<style>
:root{--bg:#0b0e14;--panel:#121722;--panel2:#0f141d;--line:#222b38;--text:#e6e9ef;--muted:#8b98a9;--green:#3ba55d;--blue:#4c8dff;--red:#ff5d5d;--amber:#e0a34d}
*{box-sizing:border-box}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:var(--bg);color:var(--text);font-size:13px}
header{display:flex;align-items:center;gap:14px;padding:14px 22px;border-bottom:1px solid var(--line);position:sticky;top:0;background:rgba(11,14,20,.85);backdrop-filter:blur(10px);z-index:9}
.logo{width:28px;height:28px;border-radius:8px;background:linear-gradient(135deg,#3ba55d,#2b7a45);display:flex;align-items:center;justify-content:center;font-weight:700}
header h1{font-size:14px;margin:0;font-weight:600}
header .sp{flex:1}
.badge{font-size:11px;padding:3px 8px;border-radius:20px;background:var(--panel);border:1px solid var(--line);color:var(--muted)}
nav{display:flex;gap:4px;padding:12px 22px 0}
nav button{background:none;border:0;color:var(--muted);padding:8px 13px;border-radius:8px;cursor:pointer;font-size:13px}
nav button.on{background:var(--panel);color:var(--text)}
main{padding:18px 22px 60px;max-width:1100px;margin:0 auto}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px}
.kv{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid rgba(255,255,255,.04)}
.kv:last-child{border:0}
.muted{color:var(--muted)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:8px 6px;border-bottom:1px solid rgba(255,255,255,.05);font-size:12px}
th{color:var(--muted);font-weight:500}
button.act{background:var(--green);color:#04240f;border:0;border-radius:8px;padding:7px 12px;font-weight:600;cursor:pointer}
button.ghost{background:transparent;border:1px solid var(--line);color:var(--text);border-radius:8px;padding:6px 11px;cursor:pointer}
button.danger{background:transparent;border:1px solid rgba(255,93,93,.4);color:var(--red);border-radius:8px;padding:6px 11px;cursor:pointer}
input,select{background:var(--panel2);border:1px solid var(--line);color:var(--text);border-radius:8px;padding:8px 10px;font-size:12px;outline:none}
input:focus{border-color:var(--green)}
.pill{font-size:11px;padding:2px 7px;border-radius:20px}
.ok{background:rgba(59,165,93,.15);color:var(--green)}
.off{background:rgba(139,152,169,.15);color:var(--muted)}
.bad{background:rgba(255,93,93,.15);color:var(--red)}
h3{margin:0 0 10px;font-size:13px;font-weight:600}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.toast{position:fixed;right:18px;bottom:18px;background:var(--panel);border:1px solid var(--line);border-left:3px solid var(--green);padding:10px 14px;border-radius:9px;opacity:0;transition:.25s;font-size:12px}
.toast.show{opacity:1}
code.tok{background:var(--panel2);border:1px solid var(--line);padding:3px 7px;border-radius:6px;font-size:12px;user-select:all}
</style></head>
<body>
<header>
  <div class="logo">T</div><h1>Tunnel</h1>
  <span class="badge" id="mode"></span><span class="badge mono" id="domain"></span>
  <span class="sp"></span>
  <span class="badge" id="me"></span>
  <button class="ghost" onclick="location.href='/auth/logout'">退出</button>
</header>
<nav>
  <button data-tab="tunnels" class="on">隧道</button>
  <button data-tab="tokens">API Token</button>
  <button data-tab="usage">用量</button>
  <button data-tab="admin" id="navAdmin" style="display:none">管理</button>
</nav>
<main>
  <section id="tab-tunnels"></section>
  <section id="tab-tokens" style="display:none"></section>
  <section id="tab-usage" style="display:none"></section>
  <section id="tab-admin" style="display:none">
    <div class="grid" id="adminStats"></div>
    <div class="card" style="margin-top:12px"><h3>用户</h3><div id="adminUsers"></div></div>
    <div class="card" style="margin-top:12px"><h3>全部隧道</h3><div id="adminTunnels"></div></div>
    <div class="card" style="margin-top:12px"><h3>审计日志</h3><div id="adminAudit"></div></div>
  </section>
</main>
<div class="toast" id="toast"></div>

<script>
const $=s=>document.querySelector(s);
let me=null;
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2200)}
async function api(path,opt){const r=await fetch(path,Object.assign({headers:{'content-type':'application/json'}},opt||{}));const j=await r.json().catch(()=>({}));if(!r.ok)throw new Error(j.error||('HTTP '+r.status));return j}
function esc(s){return String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function bytes(b){if(b<1024)return b+'B';if(b<1048576)return (b/1024).toFixed(1)+'KB';if(b<1073741824)return (b/1048576).toFixed(1)+'MB';return (b/1073741824).toFixed(2)+'GB'}

document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('on',x===b));
  ['tunnels','tokens','usage','admin'].forEach(t=>$('#tab-'+t).style.display=t===b.dataset.tab?'':'none');
  load(b.dataset.tab);
});

async function boot(){
  try{me=await api('/auth/me')}catch(e){location.href='/auth/login';return}
  $('#me').textContent=me.email+(me.role==='admin'?' · admin':'');
  const cfg=await api('/auth/config');$('#mode').textContent=cfg.auth_mode;$('#domain').textContent=cfg.base_domain||'';
  if(me.role==='admin')$('#navAdmin').style.display='';
  load('tunnels');
}
function load(tab){if(tab==='tunnels')loadTunnels();if(tab==='tokens')loadTokens();if(tab==='usage')loadUsage();if(tab==='admin')loadAdmin()}

async function loadTunnels(){
  const r=await api('/api/tunnels');
  let h='<div class="card"><h3>我的隧道</h3>';
  if(!r.tunnels.length)h+='<div class="muted">还没有隧道。在客户端配置并连接后会出现在这里。</div>';
  else{h+='<table><tr><th>ID</th><th>类型</th><th>公网地址</th><th>状态</th><th>访客鉴权</th><th></th></tr>';
    r.tunnels.forEach(t=>{
      let va='—';try{const v=JSON.parse(t.visitor_auth||'{}');const p=[];if(v.basic)p.push('Basic');if(v.ips&&v.ips.length)p.push('IP白名单');va=p.join(' + ')||'—'}catch(e){}
      h+=`<tr><td class="mono">${esc(t.tunnel_id)}</td><td>${esc(t.proto)}</td>
      <td class="mono"><a href="${esc(t.public_url)}" target="_blank" style="color:var(--blue)">${esc(t.public_url)}</a></td>
      <td>${t.online?'<span class="pill ok">在线</span>':'<span class="pill off">离线</span>'}</td>
      <td>${esc(va)}</td>
      <td class="row">
        <button class="ghost" onclick="editAuth('${esc(t.tunnel_id)}')">访客鉴权</button>
        <button class="ghost" onclick="toggleT('${esc(t.tunnel_id)}',${!t.disabled})">${t.disabled?'启用':'停用'}</button>
        <button class="danger" onclick="delT('${esc(t.tunnel_id)}')">删除</button>
      </td></tr>`});
    h+='</table>';}
  h+='</div>';$('#tab-tunnels').innerHTML=h;
}
async function toggleT(id,disabled){await api('/api/tunnels/'+encodeURIComponent(id),{method:'PATCH',body:JSON.stringify({disabled})});toast('已更新，重连客户端后生效');loadTunnels()}
async function delT(id){if(!confirm('删除隧道 '+id+'?'))return;await api('/api/tunnels/'+encodeURIComponent(id),{method:'DELETE'});toast('已删除');loadTunnels()}
async function editAuth(id){
  const user=prompt('子路由访问的用户名（留空则不加 Basic 认证）');if(user===null)return;
  let va={};
  if(user){const pass=prompt('密码')||'';va.basic={user,pass}}
  const ips=prompt('IP 白名单（逗号分隔，可含 CIDR，留空不限制）')||'';
  va.ips=ips.split(',').map(s=>s.trim()).filter(Boolean);
  await api('/api/tunnels/'+encodeURIComponent(id),{method:'PATCH',body:JSON.stringify({visitor_auth:va})});
  toast('已保存，重连客户端后生效');loadTunnels();
}

async function loadTokens(){
  const r=await api('/api/tokens');
  let h='<div class="card"><div class="row" style="margin-bottom:10px"><h3 style="margin:0">API Token</h3><button class="act" onclick="newToken()">新建 Token</button></div>';
  if(!r.tokens.length)h+='<div class="muted">还没有 Token。客户端用 Token 连接控制通道。</div>';
  else{h+='<table><tr><th>名称</th><th>前缀</th><th>创建</th><th>最近使用</th><th></th></tr>';
    r.tokens.forEach(t=>h+=`<tr><td>${esc(t.name)}</td><td class="mono">${esc(t.prefix)}…</td><td class="muted">${esc((t.created_at||'').slice(0,19))}</td><td class="muted">${esc((t.last_used_at||'—').slice(0,19))}</td><td><button class="danger" onclick="revoke(${t.id})">吊销</button></td></tr>`);
    h+='</table>';}
  h+='</div>';$('#tab-tokens').innerHTML=h;
}
async function newToken(){const name=prompt('Token 名称','macbook');if(name===null)return;const r=await api('/api/tokens',{method:'POST',body:JSON.stringify({name})});
  $('#tab-tokens').insertAdjacentHTML('afterbegin',`<div class="card" style="border-color:var(--green)"><h3>新 Token（只显示这一次）</h3><code class="tok">${esc(r.token)}</code><div class="muted" style="margin-top:8px">复制到客户端保存，关掉就看不到了。</div></div>`);loadTokens()}
async function revoke(id){if(!confirm('吊销这个 Token？'))return;await api('/api/tokens/'+id,{method:'DELETE'});toast('已吊销');loadTokens()}

async function loadUsage(){
  const r=await api('/api/usage');const pct=Math.min(100,Math.round(r.bytes/(r.daily_bytes_limit||1)*100));
  $('#tab-usage').innerHTML=`<div class="card"><h3>今日用量 · ${esc(r.day)}</h3>
    <div class="kv"><span class="muted">流量</span><span class="mono">${bytes(r.bytes)} / ${bytes(r.daily_bytes_limit)}（${pct}%）</span></div>
    <div class="kv"><span class="muted">请求数</span><span class="mono">${r.requests}</span></div>
    <div style="margin-top:10px;height:8px;background:var(--panel2);border-radius:6px;overflow:hidden"><div style="height:100%;width:${pct}%;background:${pct>90?'var(--red)':'var(--green)'}"></div></div></div>`;
}

async function loadAdmin(){
  const o=await api('/api/admin/overview');
  $('#adminStats').innerHTML=card('用户',o.users)+card('隧道',o.tunnels)+card('在线客户端',o.online_clients)+card('今日请求',o.requests_today);
  const u=await api('/api/admin/users');
  $('#adminUsers').innerHTML='<table><tr><th>邮箱</th><th>命名空间</th><th>角色</th><th>隧道配额</th><th>状态</th><th></th></tr>'+u.users.map(x=>
    `<tr><td>${esc(x.email)}</td><td class="mono">${esc(x.slug)}</td><td>${esc(x.role)}</td><td>${x.max_tunnels}</td>
     <td>${x.online?'<span class="pill ok">在线</span>':'<span class="pill off">离线</span>'} ${x.disabled?'<span class="pill bad">已停用</span>':''}</td>
     <td class="row"><button class="ghost" onclick="setQuota(${x.id},${x.max_tunnels})">配额</button>
     <button class="ghost" onclick="setRole(${x.id},'${x.role==='admin'?'user':'admin'}')">${x.role==='admin'?'降为用户':'设为管理员'}</button>
     <button class="danger" onclick="setDisabled(${x.id},${!x.disabled})">${x.disabled?'启用':'停用'}</button></td></tr>`).join('')+'</table>';
  const t=await api('/api/admin/tunnels');
  $('#adminTunnels').innerHTML='<table><tr><th>用户</th><th>隧道</th><th>公网地址</th><th>状态</th><th></th></tr>'+t.tunnels.map(x=>
    `<tr><td>${x.user_id}</td><td class="mono">${esc(x.tunnel_id)}</td><td class="mono">${esc(x.public_url)}</td>
     <td>${x.online?'<span class="pill ok">在线</span>':'<span class="pill off">离线</span>'} ${x.disabled?'<span class="pill bad">停用</span>':''}</td>
     <td><button class="ghost" onclick="adminToggle(${x.id},${!x.disabled})">${x.disabled?'启用':'停用'}</button></td></tr>`).join('')+'</table>';
  const a=await api('/api/admin/audit?limit=100');
  $('#adminAudit').innerHTML='<table><tr><th>时间</th><th>操作者</th><th>动作</th><th>目标</th><th>详情</th></tr>'+a.audit.map(x=>
    `<tr><td class="muted mono">${esc((x.at||'').slice(0,19))}</td><td>${esc(x.actor||'—')}</td><td class="mono">${esc(x.action)}</td><td class="mono">${esc(x.target||'')}</td><td class="muted">${esc(x.detail||'')}</td></tr>`).join('')+'</table>';
}
function card(l,v){return `<div class="card"><div class="muted">${l}</div><div style="font-size:24px;font-weight:600;margin-top:4px" class="mono">${v}</div></div>`}
async function setRole(id,role){await api('/api/admin/users/'+id,{method:'PATCH',body:JSON.stringify({role})});toast('已更新');loadAdmin()}
async function setDisabled(id,d){await api('/api/admin/users/'+id,{method:'PATCH',body:JSON.stringify({disabled:d})});toast('已更新');loadAdmin()}
async function setQuota(id,m){const v=prompt('最大隧道数',m);if(v===null)return;await api('/api/admin/users/'+id,{method:'PATCH',body:JSON.stringify({max_tunnels:parseInt(v,10)})});toast('已更新');loadAdmin()}
async function adminToggle(id,d){await api('/api/admin/tunnels/'+id,{method:'PATCH',body:JSON.stringify({disabled:d})});toast('已更新');loadAdmin()}

boot();
</script></body></html>"#;
