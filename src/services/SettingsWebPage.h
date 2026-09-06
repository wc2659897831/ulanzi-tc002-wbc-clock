#ifndef SERVICES_SETTINGS_WEB_PAGE_H_
#define SERVICES_SETTINGS_WEB_PAGE_H_

#include <string>

namespace settings_web_page {

inline std::string html() {
    return R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#f5f5f7">
<title>TC002 设置</title>
<style>
:root{font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display","PingFang SC","Microsoft YaHei",sans-serif;color:#1d1d1f;background:#f5f5f7;line-height:1.45}
*{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 10% 0%,#fff 0,#f5f5f7 42%),#f5f5f7}
.wrap{width:min(760px,100%);margin:auto;padding:34px 20px 56px}.eyebrow{font-size:13px;color:#6e6e73;letter-spacing:.08em;text-transform:uppercase;margin:0 0 8px}
h1{font-size:clamp(32px,7vw,52px);line-height:1.05;letter-spacing:-.045em;margin:0 0 12px;font-weight:700}.lead{color:#6e6e73;font-size:17px;margin:0 0 28px}
.card{background:rgba(255,255,255,.82);backdrop-filter:blur(18px);border:1px solid rgba(0,0,0,.06);border-radius:24px;padding:24px;margin:14px 0;box-shadow:0 14px 40px rgba(0,0,0,.06)}
.card h2{font-size:21px;letter-spacing:-.02em;margin:0 0 4px}.hint{font-size:13px;color:#86868b;margin:0 0 20px}.row{display:grid;grid-template-columns:1fr auto;gap:14px;align-items:center;padding:15px 0;border-top:1px solid #e6e6e8}.row:first-of-type{border-top:0}.label{font-size:16px}.value{font-variant-numeric:tabular-nums;color:#6e6e73;min-width:42px;text-align:right}
input[type=range]{width:100%;accent-color:#0071e3;display:block;margin-top:10px}input[type=text],input[type=password]{width:100%;font:inherit;border:1px solid #d2d2d7;border-radius:12px;background:#fff;padding:12px 13px;outline:0;transition:border .2s,box-shadow .2s}input:focus{border-color:#0071e3;box-shadow:0 0 0 4px rgba(0,113,227,.14)}.field{margin:14px 0}.field label{display:block;color:#6e6e73;font-size:13px;margin-bottom:7px}
.switch{display:flex;justify-content:space-between;align-items:center;padding-top:8px}.switch input{width:20px;height:20px;accent-color:#0071e3}.actions{display:flex;align-items:center;gap:14px;margin-top:22px}.button{border:0;border-radius:999px;background:#0071e3;color:white;font:600 15px inherit;padding:12px 21px;cursor:pointer;box-shadow:0 7px 16px rgba(0,113,227,.2)}.button:active{transform:scale(.98)}.status{font-size:14px;color:#6e6e73}.status.ok{color:#16803c}.status.err{color:#c62828}.footer{font-size:12px;color:#86868b;text-align:center;margin-top:22px}@media(max-width:520px){.wrap{padding:26px 15px 42px}.card{padding:20px;border-radius:20px}}
</style>
</head>
<body>
<main class="wrap">
<p class="eyebrow">Ulanzi TC002</p>
<h1>设备设置</h1>
<p class="lead">在同一 Wi‑Fi 下，轻松调整时钟体验。</p>
<section class="card">
<h2>显示与声音</h2><p class="hint">改动会立即应用到设备，并保存为下次启动的默认值。</p>
<div class="row"><div><div class="label">屏幕亮度</div><input id="brightness" type="range" min="0" max="100" step="1"></div><div class="value"><span id="brightnessValue">—</span>%</div></div>
<div class="row"><div><div class="label">音量</div><input id="volume" type="range" min="0" max="6" step="1"></div><div class="value"><span id="volumeValue">—</span>/6</div></div>
</section>
<section class="card">
<h2>Wi‑Fi</h2><p class="hint">保存后设备会尝试连接新网络；请保持当前连接直到页面提示完成。</p>
<div class="field"><label for="ssid">网络名称</label><input id="ssid" type="text" autocomplete="off" placeholder="输入 Wi‑Fi SSID"></div>
<div class="field"><label for="password">网络密码</label><input id="password" type="password" autocomplete="new-password" placeholder="输入新密码；不修改可留空"></div>
<div class="switch"><span>启动时自动连接</span><input id="wifiConnect" type="checkbox"></div>
<div class="actions"><button class="button" id="save">保存设置</button><span class="status" id="status">正在读取…</span></div>
</section>
<p class="footer">TC002 本地设置 · 仅在局域网内提供服务</p>
</main>
<script>
const $=id=>document.getElementById(id);const status=$('status');
function showStatus(text,ok=false){status.textContent=text;status.className='status '+(ok?'ok':'err')}
function render(c){const b=Number(c.brightness&&c.brightness.value!=null?c.brightness.value:(c.brightness&&c.brightness.mid||100));$('brightness').value=b;$('brightnessValue').textContent=b;$('volume').value=Number(c.volume||0);$('volumeValue').textContent=$('volume').value;$('ssid').value=c.wifi&&c.wifi.ssid||'';$('wifiConnect').checked=!!(c.wifi&&c.wifi.isConnect)}
async function load(){try{const r=await fetch('/getConfig?ts='+Date.now(),{cache:'no-store'});const raw=await r.text();if(!r.ok)throw Error('HTTP '+r.status+' '+raw);let config;try{config=JSON.parse(raw)}catch(e){throw Error('配置格式错误: '+raw.slice(0,120))}render(config);showStatus('已加载',true)}catch(e){console.error('TC002 配置读取失败',e);showStatus('读取失败：'+(e.message||'请刷新重试'))}}
$('brightness').oninput=()=>$('brightnessValue').textContent=$('brightness').value;$('volume').oninput=()=>$('volumeValue').textContent=$('volume').value;
$('save').onclick=async()=>{const payload={brightness:{value:Number($('brightness').value)},volume:Number($('volume').value),wifi:{ssid:$('ssid').value,password:$('password').value,isConnect:$('wifiConnect').checked}};$('save').disabled=true;showStatus('正在保存…',true);try{const r=await fetch('/setConfig?ts='+Date.now(),{method:'POST',headers:{'Content-Type':'application/json'},cache:'no-store',body:JSON.stringify(payload)});const raw=await r.text();if(!r.ok)throw Error('HTTP '+r.status+' '+raw);$('password').value='';showStatus('已保存并应用',true)}catch(e){console.error('TC002 配置保存失败',e);showStatus('保存失败：'+(e.message||'请检查连接'))}finally{$('save').disabled=false}};load();
</script>
</body></html>)HTML";
}

}

#endif
