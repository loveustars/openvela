/**
 * @file gui_server.c
 * @brief 嵌入式 HTTP 服务器 - 板子演示模式
 *
 * 设计：板子通过 HTTP POST 报告"到达"和"通过"事件。
 * 控制器内部维护队列，**不**自动放行（板子手推演示必须手动按"通过"键）。
 */

#include "gui_server.h"
#include "signal_controller.h"
#include "simulator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ========================================================================
 * 静态数据
 * ======================================================================== */

static volatile int g_running = 1;
static Simulator *g_sim = NULL;
static TrafficController *g_ctrl = NULL;
static ServerConfig g_config = { .port = 8080, .tick_interval_ms = 500, .max_clients = 8 };

/* ========================================================================
 * HTML 页面（内嵌）
 * ======================================================================== */

static const char *g_html_page =
"<!DOCTYPE html>\n"
"<html lang=\"zh-CN\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>智能路口 · 板子演示</title>\n"
"<style>\n"
"  * { box-sizing: border-box; margin: 0; padding: 0; }\n"
"  :root { --bg:#0f1419; --panel:#161b22; --border:#30363d; --text:#c9d1d9; --dim:#8b949e; --green:#3fb950; --yellow:#d29922; --red:#f85149; --car-left:#ff6b6b; --car-straight:#4ecdc4; --car-right:#ffe66d; }\n"
"  body { background: var(--bg); color: var(--text); font-family: 'SF Mono', monospace; min-height: 100vh; display: flex; flex-direction: column; align-items: center; padding: 12px; font-size: 12px; }\n"
"  h1 { font-size: 1.05rem; font-weight: 500; letter-spacing: .08em; margin-bottom: 8px; color: var(--dim); }\n"
"  .main-row { display: flex; gap: 10px; align-items: flex-start; flex-wrap: wrap; justify-content: center; }\n"
"  canvas { border-radius: 6px; display: block; border: 1px solid var(--border); }\n"
"  .info-panel { width: 260px; background: var(--panel); border-radius: 6px; padding: 10px; border: 1px solid var(--border); display: flex; flex-direction: column; gap: 8px; }\n"
"  .section h3 { font-size: .65rem; text-transform: uppercase; letter-spacing: .12em; color: var(--dim); margin-bottom: 4px; border-bottom: 1px solid var(--border); padding-bottom: 3px; }\n"
"  .stat-row { display: flex; justify-content: space-between; margin-bottom: 2px; font-size: 11px; }\n"
"  .stat-label { color: var(--dim); } .stat-val { font-weight: 600; }\n"
"  .phase-badge { display: inline-block; padding: 2px 6px; border-radius: 3px; font-size: 10px; font-weight: 700; }\n"
"  .phase-ns-straight { background: #0d3310; color: #3fb950; border: 1px solid #3fb950; }\n"
"  .phase-ns-left     { background: #1a1d33; color: #79c0ff; border: 1px solid #79c0ff; }\n"
"  .phase-ew-straight { background: #332a06; color: #d29922; border: 1px solid #d29922; }\n"
"  .phase-ew-left     { background: #2d1d33; color: #d2a8ff; border: 1px solid #d2a8ff; }\n"
"  .phase-yellow      { background: #2d2006; color: #d29922; border: 1px solid #d29922; }\n"
"  .phase-red         { background: #2d0a0a; color: #f85149; border: 1px solid #f85149; }\n"
"  .phase-safe        { background: #1a3300; color: #ffa657; border: 1px solid #ffa657; }\n"
"  .progress-bar { height: 3px; background: #21262d; border-radius: 2px; overflow: hidden; margin-top: 3px; }\n"
"  .progress-fill { height: 100%; background: var(--green); border-radius: 2px; transition: width .3s; }\n"
"  .lights-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 3px; }\n"
"  .light-cell { background: #111; border-radius: 3px; padding: 3px 4px; display: flex; align-items: center; justify-content: space-between; font-size: 10px; }\n"
"  .light-cell-label { color: var(--dim); }\n"
"  .lights { display: flex; gap: 2px; }\n"
"  .light { width: 8px; height: 8px; border-radius: 50%; background: #21262d; }\n"
"  .light.on-red { background: var(--red); }\n"
"  .light.on-yellow { background: var(--yellow); }\n"
"  .light.on-green { background: var(--green); }\n"
"  .queue-row { margin-bottom: 4px; }\n"
"  .queue-label { display: flex; justify-content: space-between; font-size: 10px; color: var(--dim); margin-bottom: 1px; }\n"
"  .queue-bar-bg { background: #21262d; border-radius: 2px; height: 5px; overflow: hidden; }\n"
"  .queue-bar { height: 100%; transition: width .3s; }\n"
"  .queue-bar.left    { background: var(--car-left); }\n"
"  .queue-bar.straight { background: var(--car-straight); }\n"
"  .queue-bar.right   { background: var(--car-right); }\n"
"  .legend { display: flex; flex-direction: column; gap: 3px; font-size: 10px; }\n"
"  .legend-item { display: flex; align-items: center; gap: 5px; }\n"
"  .legend-dot { width: 8px; height: 8px; border-radius: 2px; }\n"
"  .legend-dot.left    { background: var(--car-left); }\n"
"  .legend-dot.straight { background: var(--car-straight); }\n"
"  .legend-dot.right   { background: var(--car-right); }\n"
"  .safe-badge { background: #1a3300; color: #ffa657; border: 1px solid #ffa657; padding: 2px 5px; border-radius: 3px; font-size: 9px; display: none; margin-left: 4px; }\n"
"  .safe-badge.show { display: inline; }\n"
"  .board-panel { background: var(--panel); border-radius: 6px; padding: 10px; border: 1px solid var(--border); width: 260px; }\n"
"  .board-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 4px; margin-top: 6px; }\n"
"  .road-header { text-align: center; font-size: 11px; color: var(--dim); font-weight: 600; }\n"
"  .btn-arrive, .btn-pass { font-size: 9px; padding: 3px 2px; border-radius: 3px; cursor: pointer; border: 1px solid; }\n"
"  .btn-arrive { background: #1a2a3a; color: #79c0ff; border-color: #2f4858; }\n"
"  .btn-arrive:hover { background: #2a3a4a; }\n"
"  .btn-pass { background: #2a1a3a; color: #d2a8ff; border-color: #4a3a58; }\n"
"  .btn-pass:hover { background: #3a2a4a; }\n"
"  .btn-arrive:disabled, .btn-pass:disabled { opacity: 0.4; cursor: not-allowed; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<h1>智能路口 · 4 相位独立 + 板子手动演示</h1>\n"
"<div class=\"main-row\">\n"
"  <canvas id=\"canvas\" width=\"380\" height=\"380\"></canvas>\n"
"  <div class=\"info-panel\">\n"
"    <div class=\"section\"><h3>当前相位</h3>\n"
"      <span id=\"phase-badge\" class=\"phase-badge phase-ns-straight\">NS_STRAIGHT</span>\n"
"      <span id=\"safe-badge\" class=\"safe-badge\">安全模式</span>\n"
"      <div style=\"margin-top:4px;\">\n"
"        <div class=\"stat-row\"><span class=\"stat-label\">已持续</span><span class=\"stat-val\" id=\"elapsed-val\">0s</span></div>\n"
"        <div class=\"stat-row\"><span class=\"stat-label\">计划时长</span><span class=\"stat-val\" id=\"duration-val\">20s</span></div>\n"
"        <div class=\"progress-bar\"><div class=\"progress-fill\" id=\"phase-progress\" style=\"width:0%\"></div></div>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"section\"><h3>12 个独立信号灯</h3><div class=\"lights-grid\" id=\"lights-grid\"></div></div>\n"
"    <div class=\"section\"><h3>各方向队列</h3><div id=\"queues-container\"></div></div>\n"
"    <div class=\"section\"><h3>统计</h3>\n"
"      <div class=\"stat-row\"><span class=\"stat-label\">时间</span><span class=\"stat-val\" id=\"time-val\">0s</span></div>\n"
"      <div class=\"stat-row\"><span class=\"stat-label\">车辆数</span><span class=\"stat-val\" id=\"total-val\">0</span></div>\n"
"      <div class=\"stat-row\"><span class=\"stat-label\">切换</span><span class=\"stat-val\" id=\"switch-val\">0</span></div>\n"
"    </div>\n"
"    <div class=\"section\"><h3>图例</h3>\n"
"      <div class=\"legend\">\n"
"        <div class=\"legend-item\"><div class=\"legend-dot left\"></div>左转</div>\n"
"        <div class=\"legend-item\"><div class=\"legend-dot straight\"></div>直行</div>\n"
"        <div class=\"legend-item\"><div class=\"legend-dot right\"></div>右转</div>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"  <div class=\"board-panel\">\n"
"    <h3 style=\"font-size:.65rem;text-transform:uppercase;letter-spacing:.12em;color:var(--dim);margin-bottom:4px;\">板子演示（手动按键）</h3>\n"
"    <div style=\"font-size:10px;color:var(--dim);line-height:1.4;\">\n"
"      板子到达路口：点 <b style=\"color:#79c0ff\">+N</b>（到）<br>\n"
"      板子完全通过：点 <b style=\"color:#d2a8ff\">过</b>\n"
"    </div>\n"
"    <div class=\"board-grid\" id=\"board-grid\"></div>\n"
"    <div style=\"margin-top:8px;display:flex;gap:4px;\">\n"
"      <button onclick=\"autoDemo()\" style=\"flex:1;background:var(--panel);border:1px solid var(--border);color:var(--text);padding:5px;border-radius:4px;cursor:pointer;font-size:11px;\">▶ 自动演示</button>\n"
"      <button onclick=\"resetAll()\" style=\"flex:1;background:var(--panel);border:1px solid var(--border);color:var(--text);padding:5px;border-radius:4px;cursor:pointer;font-size:11px;\">⟲ 重置</button>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"\n"
"<script>\n"
"const PHASE_NAMES = ['NS_STRAIGHT','NS_LEFT','EW_STRAIGHT','EW_LEFT','YELLOW','ALL_RED'];\n"
"const PHASE_CSS   = ['phase-ns-straight','phase-ns-left','phase-ew-straight','phase-ew-left','phase-yellow','phase-red'];\n"
"const ROAD_NAMES  = ['N','S','E','W'];\n"
"\n"
"let S = {\n"
"  phase: 0, phase_elapsed: 0, stage_duration: 20,\n"
"  allowed_lanes_mask: 0,\n"
"  current_tick: 0, switch_count: 0,\n"
"  lane_queues: [[0,0,0],[0,0,0],[0,0,0],[0,0,0]],\n"
"  safe_mode: 0\n"
"};\n"
"\n"
"async function fetchState() {\n"
"  try {\n"
"    const r = await fetch('/api/state');\n"
"    const d = await r.json();\n"
"    S.phase = d.phase; S.phase_elapsed = d.phase_elapsed;\n"
"    S.stage_duration = d.stage_duration; S.allowed_lanes_mask = d.allowed_lanes_mask;\n"
"    S.current_tick = d.current_tick; S.switch_count = d.switch_count;\n"
"    S.lane_queues = d.lane_queues; S.safe_mode = d.safe_mode;\n"
"  } catch (e) { console.error(e); }\n"
"}\n"
"\n"
"async function sendBoardEvent(road, lane, isPass) {\n"
"  const path = isPass ? '/api/board/' + ROAD_NAMES[road] + '_' + 'LSR'[lane] + '/pass'\n"
"                        : '/api/board/' + ROAD_NAMES[road] + '_' + 'LSR'[lane];\n"
"  try { await fetch(path, { method: 'POST' }); } catch (e) { console.error(e); }\n"
"}\n"
"\n"
"async function resetAll() { try { await fetch('/api/reset'); } catch (e) {} }\n"
"\n"
"const canvas = document.getElementById('canvas');\n"
"const ctx = canvas.getContext('2d');\n"
"const W = 380, H = 380, CX = W/2, CY = H/2;\n"
"const ROAD_W = 40, LANE_W = ROAD_W/3, HALF_INT = 50;\n"
"const C = { road:'#1e2530', center:'#14181f', line:'#2c3544', centerline:'#d4a017', carLeft:'#ff6b6b', carStraight:'#4ecdc4', carRight:'#ffe66d', stopLine:'#ffffff', bg:'#0f1419' };\n"
"\n"
"function draw() {\n"
"  ctx.fillStyle = C.bg; ctx.fillRect(0,0,W,H);\n"
"  ctx.fillStyle = C.center; ctx.fillRect(CX-HALF_INT, CY-HALF_INT, HALF_INT*2, HALF_INT*2);\n"
"  ctx.fillStyle = C.road;\n"
"  ctx.fillRect(CX-ROAD_W, 0, ROAD_W, CY-HALF_INT);\n"
"  ctx.fillRect(CX, CY+HALF_INT, ROAD_W, H-(CY+HALF_INT));\n"
"  ctx.fillRect(0, CY-ROAD_W, CX-HALF_INT, ROAD_W);\n"
"  ctx.fillRect(CX+HALF_INT, CY, W-(CX+HALF_INT), ROAD_W);\n"
"  ctx.strokeStyle = C.centerline; ctx.lineWidth = 1; ctx.setLineDash([3,3]);\n"
"  ctx.beginPath();\n"
"  ctx.moveTo(CX, CY-HALF_INT); ctx.lineTo(CX, CY+HALF_INT);\n"
"  ctx.moveTo(CX-HALF_INT, CY); ctx.lineTo(CX+HALF_INT, CY);\n"
"  ctx.stroke(); ctx.setLineDash([]);\n"
"  ctx.strokeStyle = C.line; ctx.lineWidth = 1;\n"
"  for (let i=0;i<2;i++){const x=CX-ROAD_W+LANE_W*(i+1);ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,CY-HALF_INT);ctx.stroke();}\n"
"  for (let i=0;i<2;i++){const x=CX+LANE_W*i;ctx.beginPath();ctx.moveTo(x,CY+HALF_INT);ctx.lineTo(x,H);ctx.stroke();}\n"
"  for (let i=0;i<2;i++){const y=CY-ROAD_W+LANE_W*(i+1);ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(CX-HALF_INT,y);ctx.stroke();}\n"
"  for (let i=0;i<2;i++){const y=CY+LANE_W*i;ctx.beginPath();ctx.moveTo(CX+HALF_INT,y);ctx.lineTo(W,y);ctx.stroke();}\n"
"  ctx.strokeStyle = C.stopLine; ctx.lineWidth = 2;\n"
"  ctx.beginPath(); ctx.moveTo(CX-ROAD_W, CY-HALF_INT); ctx.lineTo(CX, CY-HALF_INT); ctx.stroke();\n"
"  ctx.beginPath(); ctx.moveTo(CX, CY+HALF_INT); ctx.lineTo(CX+ROAD_W, CY+HALF_INT); ctx.stroke();\n"
"  ctx.beginPath(); ctx.moveTo(CX-HALF_INT, CY-ROAD_W); ctx.lineTo(CX-HALF_INT, CY); ctx.stroke();\n"
"  ctx.beginPath(); ctx.moveTo(CX+HALF_INT, CY); ctx.lineTo(CX+HALF_INT, CY+ROAD_W); ctx.stroke();\n"
"  drawVehicles();\n"
"}\n"
"\n"
"function drawVehicles() {\n"
"  const CAR_H=11, CAR_W=7;\n"
"  const colors=[C.carLeft,C.carStraight,C.carRight];\n"
"  for (let r=0;r<4;r++) for (let l=0;l<3;l++) {\n"
"    const q=S.lane_queues[r][l];\n"
"    const laneOffset=(l-1)*LANE_W;\n"
"    for (let i=0;i<q;i++) {\n"
"      const dist=4+i*7;\n"
"      let x,y,rot;\n"
"      if (r===0){x=CX+laneOffset;y=CY-HALF_INT-dist;rot=Math.PI/2;}\n"
"      else if (r===1){x=CX+laneOffset;y=CY+HALF_INT+dist;rot=-Math.PI/2;}\n"
"      else if (r===2){x=CX-HALF_INT-dist;y=CY+laneOffset;rot=0;}\n"
"      else if (r===3){x=CX+HALF_INT+dist;y=CY+laneOffset;rot=Math.PI;}\n"
"      ctx.save();ctx.translate(x,y);ctx.rotate(rot);\n"
"      ctx.fillStyle=colors[l];\n"
"      ctx.beginPath();ctx.roundRect(-CAR_W/2,-CAR_H/2,CAR_W,CAR_H,2);ctx.fill();\n"
"      ctx.fillStyle='#fff';ctx.fillRect(CAR_W/2-2,-1,2,2);\n"
"      ctx.restore();\n"
"    }\n"
"  }\n"
"}\n"
"\n"
"function renderLights() {\n"
"  const cells=[\n"
"    {label:'N左',bit:1<<0},{label:'N直',bit:1<<1},{label:'N右',bit:1<<2},\n"
"    {label:'S左',bit:1<<3},{label:'S直',bit:1<<4},{label:'S右',bit:1<<5},\n"
"    {label:'E左',bit:1<<6},{label:'E直',bit:1<<7},{label:'E右',bit:1<<8},\n"
"    {label:'W左',bit:1<<9},{label:'W直',bit:1<<10},{label:'W右',bit:1<<11},\n"
"  ];\n"
"  const isY=S.phase===4, isR=S.phase===5;\n"
"  let html='';\n"
"  for (const c of cells) {\n"
"    let cls='light';\n"
"    if (isY) cls='light on-yellow';\n"
"    else if (isR) cls='light on-red';\n"
"    else if ((S.allowed_lanes_mask&c.bit)!==0) cls='light on-green';\n"
"    else cls='light on-red';\n"
"    html += '<div class=\"light-cell\"><span class=\"light-cell-label\">'+c.label+'</span><div class=\"lights\"><div class=\"'+cls+'\"></div></div></div>';\n"
"  }\n"
"  document.getElementById('lights-grid').innerHTML = html;\n"
"}\n"
"\n"
"function renderInfo() {\n"
"  document.getElementById('phase-badge').textContent = PHASE_NAMES[S.phase];\n"
"  document.getElementById('phase-badge').className = 'phase-badge ' + PHASE_CSS[S.phase];\n"
"  document.getElementById('safe-badge').className = 'safe-badge' + (S.safe_mode ? ' show' : '');\n"
"  document.getElementById('elapsed-val').textContent = S.phase_elapsed + 's';\n"
"  document.getElementById('duration-val').textContent = S.stage_duration + 's';\n"
"  const p = S.stage_duration > 0 ? S.phase_elapsed/S.stage_duration*100 : 0;\n"
"  document.getElementById('phase-progress').style.width = p + '%';\n"
"  document.getElementById('time-val').textContent = S.current_tick + 's';\n"
"  document.getElementById('switch-val').textContent = S.switch_count;\n"
"  document.getElementById('total-val').textContent = S.lane_queues.flat().reduce((a,b)=>a+b,0);\n"
"  const laneNames=['左','直','右'], css=['left','straight','right'];\n"
"  let qhtml='';\n"
"  for (let r=0;r<4;r++) {\n"
"    qhtml += '<div style=\"margin-bottom:5px;font-size:10px;color:var(--dim);\">'+ROAD_NAMES[r]+'</div>';\n"
"    for (let l=0;l<3;l++) {\n"
"      const q=S.lane_queues[r][l], w=Math.min(100, q/10*100);\n"
"      qhtml += '<div class=\"queue-row\"><div class=\"queue-label\"><span>'+laneNames[l]+'</span><span>'+q+'</span></div><div class=\"queue-bar-bg\"><div class=\"queue-bar '+css[l]+'\" style=\"width:'+w+'%\"></div></div></div>';\n"
"    }\n"
"  }\n"
"  document.getElementById('queues-container').innerHTML = qhtml;\n"
"}\n"
"\n"
"function renderBoardGrid() {\n"
"  const laneNames=['左','直','右'];\n"
"  let html='<div></div>';\n"
"  for (let l=0;l<3;l++) html += '<div class=\"road-header\">'+laneNames[l]+'</div>';\n"
"  for (let r=0;r<4;r++) {\n"
"    html += '<div class=\"road-header\">'+ROAD_NAMES[r]+'</div>';\n"
"    for (let l=0;l<3;l++) {\n"
"      const q=S.lane_queues[r][l];\n"
"      const passDis = q===0 ? 'disabled' : '';\n"
"      const label = q>0 ? ('+'+q) : '到';\n"
"      html += '<div style=\"display:flex;flex-direction:column;gap:2px;\">'\n"
"           +  '<button class=\"btn-arrive\" onclick=\"sendBoardEvent('+r+','+l+',false)\">'+label+'</button>'\n"
"           +  '<button class=\"btn-pass\" '+passDis+' onclick=\"sendBoardEvent('+r+','+l+',true)\">过</button>'\n"
"           +  '</div>';\n"
"    }\n"
"  }\n"
"  document.getElementById('board-grid').innerHTML = html;\n"
"}\n"
"\n"
"async function tick() {\n"
"  await fetchState();\n"
"  draw(); renderLights(); renderInfo(); renderBoardGrid();\n"
"}\n"
"setInterval(tick, 500);\n"
"\n"
"async function autoDemo() {\n"
"  await resetAll();\n"
"  await new Promise(r=>setTimeout(r,500));\n"
"  const boards=[\n"
"    {road:0,lane:1,arrive:0,    pass:6000},\n"
"    {road:1,lane:1,arrive:3000, pass:10000},\n"
"    {road:2,lane:0,arrive:6000, pass:14000},\n"
"    {road:3,lane:0,arrive:9000, pass:18000}\n"
"  ];\n"
"  for (const b of boards) {\n"
"    setTimeout(()=>sendBoardEvent(b.road,b.lane,false), b.arrive);\n"
"    setTimeout(()=>sendBoardEvent(b.road,b.lane,true),  b.pass);\n"
"  }\n"
"}\n"
"\n"
"tick();\n"
"</script>\n"
"</body>\n"
"</html>";

/* ========================================================================
 * 工具函数
 * ======================================================================== */

static void send_response(int client_fd, const char *status,
                         const char *content_type, const char *body, size_t len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, content_type, len);
    send(client_fd, header, header_len, 0);
    if (body && len > 0) send(client_fd, body, len, 0);
}

static void url_decode(char *dst, const char *src, size_t max_len) {
    size_t i = 0;
    while (*src && i < max_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            unsigned int hex;
            sscanf(src + 1, "%2x", &hex);
            dst[i++] = (char)hex;
            src += 3;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

/* ========================================================================
 * 状态 JSON
 * ======================================================================== */

static void generate_state_json(char *buf, size_t max_len) {
    int (*queues)[3] = g_ctrl->lane_queues;
    int allowed_mask = controller_get_allowed_lanes(g_ctrl, g_ctrl->current_phase);

    int corridor_counts[2] = {0, 0};
    for (int r = 0; r < 4; r++) {
        for (int l = 0; l < 3; l++) {
            corridor_counts[r < 2 ? 0 : 1] += queues[r][l];
        }
    }

    snprintf(buf, max_len,
        "{"
        "\"phase\":%d,"
        "\"phase_name\":\"%s\","
        "\"phase_elapsed\":%d,"
        "\"stage_duration\":%d,"
        "\"switch_count\":%d,"
        "\"current_tick\":%d,"
        "\"allowed_lanes_mask\":%d,"
        "\"safe_mode\":%d,"
        "\"ticks_since_ns_straight\":%d,"
        "\"ticks_since_ns_left\":%d,"
        "\"ticks_since_ew_straight\":%d,"
        "\"ticks_since_ew_left\":%d,"
        "\"lane_queues\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]],"
        "\"corridor_counts\":[%d,%d],"
        "\"waiting_seconds\":[%d,%d],"
        "\"reason\":\"%s\""
        "}",
        g_ctrl->current_phase,
        phase_to_string(g_ctrl->current_phase),
        g_ctrl->current_phase_elapsed,
        g_ctrl->current_stage_duration,
        g_ctrl->switch_count,
        g_ctrl->tick_count,
        allowed_mask,
        g_ctrl->safe_mode,
        g_ctrl->ticks_since_ns_straight,
        g_ctrl->ticks_since_ns_left,
        g_ctrl->ticks_since_ew_straight,
        g_ctrl->ticks_since_ew_left,
        queues[0][0], queues[0][1], queues[0][2],
        queues[1][0], queues[1][1], queues[1][2],
        queues[2][0], queues[2][1], queues[2][2],
        queues[3][0], queues[3][1], queues[3][2],
        corridor_counts[0], corridor_counts[1],
        g_ctrl->waiting_seconds[0], g_ctrl->waiting_seconds[1],
        g_ctrl->last_reason
    );
}

/* ========================================================================
 * HTTP 请求处理
 * ======================================================================== */

static void handle_request(int client_fd, const char *method, const char *path,
                           const char *body, size_t body_len) {
    (void)body;
    (void)body_len;

    /* GET 路由 */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
            send_response(client_fd, "200 OK", "text/html; charset=utf-8",
                          g_html_page, strlen(g_html_page));
            return;
        }
        if (strncmp(path, "/api/state", 10) == 0 ||
            strncmp(path, "/api/step", 9) == 0) {
            char json[2048];
            generate_state_json(json, sizeof(json));
            send_response(client_fd, "200 OK", "application/json", json, strlen(json));
            return;
        }
        if (strncmp(path, "/api/scenario/", 14) == 0) {
            char decoded[64];
            url_decode(decoded, path + 14, sizeof(decoded));
            if (g_sim) simulator_destroy(g_sim);
            g_sim = simulator_create(&(SimulatorConfig){
                .scenario = SCENARIO_BALANCED,
                .duration_seconds = 3600,
                .tick_interval_ms = g_config.tick_interval_ms
            });
            char json[256];
            snprintf(json, sizeof(json), "{\"status\":\"ok\",\"scenario\":\"%s\"}", decoded);
            send_response(client_fd, "200 OK", "application/json", json, strlen(json));
            return;
        }
        if (strncmp(path, "/api/reset", 10) == 0) {
            if (g_sim) simulator_destroy(g_sim);
            g_sim = simulator_create(&(SimulatorConfig){
                .scenario = SCENARIO_BALANCED,
                .duration_seconds = 3600,
                .tick_interval_ms = g_config.tick_interval_ms
            });
            controller_reset(g_ctrl);
            char json[256];
            snprintf(json, sizeof(json), "{\"status\":\"reset\"}");
            send_response(client_fd, "200 OK", "application/json", json, strlen(json));
            return;
        }
    }

    /* POST /api/board/<road>_<lane>          — 板子报告"到达"
     * POST /api/board/<road>_<lane>/pass     — 板子报告"通过" */
    if (strcmp(method, "POST") == 0 && strncmp(path, "/api/board/", 11) == 0) {
        const char *p = path + 11;
        int is_pass = 0;
        char suffix[8] = {0};
        const char *slash = strchr(p, '/');
        if (slash && strcmp(slash, "/pass") == 0) {
            is_pass = 1;
            int len = slash - p;
            if (len >= (int)sizeof(suffix)) len = sizeof(suffix) - 1;
            strncpy(suffix, p, len);
            suffix[len] = '\0';
            p = suffix;
        }

        char decoded[64];
        url_decode(decoded, p, sizeof(decoded));

        int road = -1, lane = -1;
        if (strlen(decoded) >= 3 && decoded[1] == '_') {
            switch (decoded[0]) {
                case 'N': case 'n': road = ROAD_N; break;
                case 'S': case 's': road = ROAD_S; break;
                case 'E': case 'e': road = ROAD_E; break;
                case 'W': case 'w': road = ROAD_W; break;
            }
            switch (decoded[2]) {
                case 'L': case 'l': lane = LANE_LEFT; break;
                case 'S': case 's': lane = LANE_STRAIGHT; break;
                case 'R': case 'r': lane = LANE_RIGHT; break;
            }
        }

        if (road < 0 || lane < 0) {
            char json[256];
            snprintf(json, sizeof(json),
                     "{\"status\":\"error\",\"msg\":\"invalid format, use /api/board/N_S\"}");
            send_response(client_fd, "400 Bad Request", "application/json", json, strlen(json));
            return;
        }

        int q = is_pass
            ? controller_report_pass(g_ctrl, road, lane)
            : controller_report_vehicle(g_ctrl, road, lane);

        char json[256];
        snprintf(json, sizeof(json),
                 "{\"status\":\"ok\",\"event\":\"%s\",\"road\":%d,\"lane\":%d,\"queue\":%d}",
                 is_pass ? "pass" : "arrival", road, lane, q);
        send_response(client_fd, "200 OK", "application/json", json, strlen(json));
        return;
    }

    send_response(client_fd, "404 Not Found", "text/plain", "Not Found", 9);
}

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ========================================================================
 * 主入口
 * ======================================================================== */

int gui_server_start(const ServerConfig *config) {
    if (config) {
        g_config.port = config->port;
        g_config.tick_interval_ms = config->tick_interval_ms;
        g_config.max_clients = config->max_clients;
    }

    g_sim = simulator_create(&(SimulatorConfig){
        .scenario = SCENARIO_BALANCED,
        .duration_seconds = 3600,
        .tick_interval_ms = g_config.tick_interval_ms
    });
    g_ctrl = (TrafficController*)malloc(sizeof(TrafficController));
    controller_init(g_ctrl, NULL, PHASE_NS_STRAIGHT);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_config.port);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, g_config.max_clients) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("GUI Server started on http://localhost:%d/\n", g_config.port);
    printf("API endpoints:\n");
    printf("  GET /              - HTML page\n");
    printf("  GET /api/state     - Current signal state JSON\n");
    printf("  GET /api/reset     - Reset\n");
    printf("  POST /api/board/<road>_<lane>       - Report ARRIVAL (e.g. N_S, E_L, W_R)\n");
    printf("  POST /api/board/<road>_<lane>/pass - Report PASS-THROUGH (operator button)\n");
    fflush(stdout);

    struct timeval tv;
    tv.tv_sec = g_config.tick_interval_ms / 1000;
    tv.tv_usec = (g_config.tick_interval_ms % 1000) * 1000;

    while (g_running) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        int activity = select(server_fd + 1, &read_fds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

            if (client_fd >= 0) {
                char buffer[4096];
                int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

                if (n > 0) {
                    buffer[n] = '\0';
                    char method[16], path[256], version[16];
                    int parsed = sscanf(buffer, "%15s %255s %15s", method, path, version);
                    if (parsed == 3) {
                        const char *body = strstr(buffer, "\r\n\r\n");
                        size_t body_len = 0;
                        if (body) {
                            body += 4;
                            body_len = n - (body - buffer);
                        }
                        handle_request(client_fd, method, path, body, body_len);
                    } else {
                        send_response(client_fd, "400 Bad Request", "text/plain",
                                     "Bad Request", 11);
                    }
                }
                close(client_fd);
            }
        }

        if (g_ctrl) controller_step_internal(g_ctrl);
    }

    close(server_fd);
    if (g_sim) simulator_destroy(g_sim);
    if (g_ctrl) free(g_ctrl);

    printf("Server stopped.\n");
    return 0;
}

void gui_server_stop(void) {
    g_running = 0;
}