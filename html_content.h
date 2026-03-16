/*
 * html_content.h - Web 配置页面 HTML 内容
 * 
 * 存储在程序存储器 (PROGMEM) 中以节省 RAM
 */

#ifndef HTML_CONTENT_H
#define HTML_CONTENT_H

#include <Arduino.h>

// --- Web 配置页面 HTML (存储在程序存储器中) ---
const char index_html[] PROGMEM = R"rawliteral(
  <!DOCTYPE HTML><html><head>
    <meta charset="utf-8">
    <title>WT32-ETH01 Printer Node</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial, sans-serif; text-align: center; margin: 20px; background-color: #eef2f3; }
      .card { background: #fff; padding: 20px; margin: 15px auto; max-width: 500px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
      input { width: 95%; padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; }
      button { padding: 10px 20px; background: #28a745; color: white; border: none; cursor: pointer; border-radius: 4px; font-size: 16px; }
      .btn-red { background: #dc3545; }
      .val-box { display: flex; justify-content: space-between; border-bottom: 1px solid #eee; padding: 8px 0; }
      .header-box { background: #444; color: #fff; font-weight: bold; padding: 8px; margin-top: 15px; border-radius: 4px; }
      .mac-addr { font-size: 1.2em; color: #0056b3; font-weight: bold; font-family: monospace; letter-spacing: 1px; }
      .serial-no { color: #d32f2f; font-weight: bold; }
      .status { color: #666; font-style: italic; }
      h2 { color: #333; }
      label { font-weight: bold; display: block; text-align: left; margin-top: 10px; }
      .hint { font-size: 0.8em; color: #888; }
    </style>
  </head><body>
    <h2>🖨️ Printer Node Config</h2>
    
    <div class="card">
      <div style="text-align:center; padding-bottom:10px; border-bottom:2px solid #eee;">
        <div>Device MAC (WT32)</div>
        <div id="dev_mac" class="mac-addr">Loading...</div>
      </div>
    </div>
  
    <div class="card">
      <div class="header-box">实时监控 (Live View)</div>
      <div class="val-box"><span>Printer Serial:</span> <b id="v_serial" class="serial-no">-</b></div>
      <div class="val-box"><span>System Total:</span> <b id="v_st">-</b></div>
      <div class="val-box"><span>B&W Copies:</span> <b id="v_bc">-</b></div>
      <div class="val-box"><span>B&W Prints:</span> <b id="v_bp">-</b></div>
      <div class="val-box"><span>Color Copies:</span> <b id="v_cc">-</b></div>
      <div class="val-box"><span>Color Prints:</span> <b id="v_cp">-</b></div>
      <div class="val-box"><span>碳粉 黑:</span> <b id="v_tb">-</b></div>
      <div class="val-box"><span>碳粉 青:</span> <b id="v_tc">-</b></div>
      <div class="val-box"><span>碳粉 红:</span> <b id="v_tr">-</b></div>
      <div class="val-box"><span>碳粉 黄:</span> <b id="v_ty">-</b></div>
      <p class="status" id="sys_status">Connecting...</p>
      <p class="status" id="mqtt_status">MQTT: -</p>
    </div>
  
    <div class="card">
      <h3>⚙️ 系统设置 (Config)</h3>
      <form action="/save" method="POST">
        <div class="header-box">1. 网络 (WiFi)</div>
        <label>SSID</label><input type="text" name="ssid" id="ssid">
        <label>Password</label><input type="password" name="pass" id="pass">
  
        <div class="header-box">2. 打印机识别 (Printer ID)</div>
        <label>Target Serial Number (搜索用)</label>
        <input type="text" name="t_ser" id="t_ser" placeholder="输入机身序号以精准搜索">
        <div class="hint">*若此栏留空，将自动锁定网段内第一台发现的打印机。</div>
  
        <div class="header-box">3. IP 设置 (IP Settings)</div>
        <label>Printer IP (自动锁定)</label><input type="text" name="pip" id="pip">
        <div id="scan_res" style="color:green; font-weight:bold;"></div>
        
        <br><br>
        <button type="submit" class="btn-red">保存并重启 (Save & Reboot)</button>
      </form>
    </div>
  
  <script>
    // 页面加载时读取配置
    fetch('/config').then(res => res.json()).then(data => {
      // 显示设备 MAC 地址
      document.getElementById("dev_mac").innerText = data.mac;
      // 填充 WiFi 配置
      document.getElementById("ssid").value = data.ssid;
      document.getElementById("pass").value = data.pass;
      // 填充目标序列号
      document.getElementById("t_ser").value = data.t_ser;
      // 填充打印机 IP
      document.getElementById("pip").value = data.pip;
    });
  
    // 定时更新状态 (每 2 秒)
    setInterval(function() {
      fetch('/status').then(response => response.json()).then(data => {
        // 更新打印机数据
        document.getElementById("v_serial").innerHTML = data.serial ? data.serial : "(Waiting...)"; 
        document.getElementById("v_st").innerHTML = data.st;
        document.getElementById("v_bc").innerHTML = data.bc;
        document.getElementById("v_bp").innerHTML = data.bp;
        document.getElementById("v_cc").innerHTML = data.cc;
        document.getElementById("v_cp").innerHTML = data.cp;
        document.getElementById("v_tb").innerHTML = data.toner_black >= 0 ? data.toner_black + '%' : '-';
        document.getElementById("v_tc").innerHTML = data.toner_cyan >= 0 ? data.toner_cyan + '%' : '-';
        document.getElementById("v_tr").innerHTML = data.toner_red >= 0 ? data.toner_red + '%' : '-';
        document.getElementById("v_ty").innerHTML = data.toner_yellow >= 0 ? data.toner_yellow + '%' : '-';
        document.getElementById("sys_status").innerHTML = data.msg;
        
        // 更新 MQTT 状态
        var mStatus = document.getElementById("mqtt_status");
        mStatus.innerHTML = "MQTT: " + data.mqtt_state;
        mStatus.style.color = (data.mqtt_state === "Connected") ? "green" : "red";
        
        // 如果检测到新的 IP，自动更新显示
        if(data.detectedIP && data.detectedIP.length > 7 && document.getElementById("pip").value != data.detectedIP) {
            document.getElementById("scan_res").innerHTML = "已锁定序号，IP: " + data.detectedIP;
            document.getElementById("pip").value = data.detectedIP; 
        }
      });
    }, 2000);
  </script>
  </body></html>
  )rawliteral";

#endif  // HTML_CONTENT_H
