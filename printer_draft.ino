#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <SNMP.h>        // Patrick's SNMP Library
#include <Preferences.h>
#include <PubSubClient.h>

// ==========================================
//          🔒 設定區 (Hardcode)
// ==========================================
const char* MQTT_BROKER   = "157.245.203.184";  // VPS IP
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "admin";
const char* MQTT_PASS     = "password";         // <--- ⚠️ 請確保密碼正確
const char* MQTT_TOPIC_PREFIX = "printer/data";

// --- WT32-ETH01 引腳 ---
#define ETH_CLK_MODE    ETH_CLOCK_GPIO0_IN
#define ETH_POWER_PIN   16
#define ETH_TYPE        ETH_PHY_LAN8720
#define ETH_ADDR        1
#define ETH_MDC_PIN     23
#define ETH_MDIO_PIN    18

// --- 參數設定 ---
const int SNMP_INTERVAL = 5000;       
const int SCAN_CONNECT_TIMEOUT = 50; // 稍微增加一點以確保穩定
const int SCAN_BATCH_SIZE = 10;       

// --- 全局變量 ---
WebServer server(80);
Preferences preferences;
WiFiUDP udp;
SNMP::Manager snmp; 
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- 設定 (Config) ---
String cfg_ssid = "";
String cfg_pass = "";
String cfg_printer_ip = "";
String cfg_target_serial = ""; // *** 新增：目標序號 (用於搜尋) ***

// --- 狀態 ---
String statusMessage = "System Booting...";
String deviceMAC = "";
unsigned long lastRequestTime = 0;   
bool isScanning = false;
int  scanCurrentIP = 1; 

// --- 數值 (Current) ---
int val_SysTotal = 0;    
int val_ColTotal = 0;    
int val_TotCopies = 0;   
int val_ColCopies = 0;   
int val_ColPrints = 0;   
String val_PrtSerial = ""; 

int calc_BWTotal = 0;
int calc_BWCopies = 0;
int calc_BWPrints = 0;

int last_sent_SysTotal = -1;

// --- Ricoh OIDs ---
const char* OID_PRT_SERIAL = "1.3.6.1.2.1.43.5.1.1.17.1"; 
const char* OID_SYS_TOTAL  = "1.3.6.1.2.1.43.10.2.1.4.1.1";
const char* OID_COL_TOTAL  = "1.3.6.1.4.1.367.3.2.1.2.19.5.1.4.1";
const char* OID_TOT_COPIES = "1.3.6.1.4.1.367.3.2.1.2.19.4.0";
const char* OID_COL_COPIES = "1.3.6.1.4.1.367.3.2.1.2.16.7.0";
const char* OID_COL_PRINTS = "1.3.6.1.4.1.367.3.2.1.2.16.3.1.2.5";

// --- HTML 頁面 ---
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
    <div class="header-box">即時監控 (Live View)</div>
    <div class="val-box"><span>Printer Serial:</span> <b id="v_serial" class="serial-no">-</b></div>
    <div class="val-box"><span>System Total:</span> <b id="v_st">-</b></div>
    <div class="val-box"><span>B&W Copies:</span> <b id="v_bc">-</b></div>
    <div class="val-box"><span>B&W Prints:</span> <b id="v_bp">-</b></div>
    <div class="val-box"><span>Color Copies:</span> <b id="v_cc">-</b></div>
    <div class="val-box"><span>Color Prints:</span> <b id="v_cp">-</b></div>
    <p class="status" id="sys_status">Connecting...</p>
    <p class="status" id="mqtt_status">MQTT: -</p>
  </div>

  <div class="card">
    <h3>⚙️ 系統設定 (Config)</h3>
    <form action="/save" method="POST">
      <div class="header-box">1. 網絡 (WiFi)</div>
      <label>SSID</label><input type="text" name="ssid" id="ssid">
      <label>Password</label><input type="password" name="pass" id="pass">

      <div class="header-box">2. 打印機識別 (Printer ID)</div>
      <label>Target Serial Number (搜尋用)</label>
      <input type="text" name="t_ser" id="t_ser" placeholder="輸入機身序號以精準搜尋">
      <div class="hint">*若此欄留空，將自動鎖定網段內第一台發現的打印機。</div>

      <div class="header-box">3. IP 設定 (IP Settings)</div>
      <label>Printer IP (自動鎖定)</label><input type="text" name="pip" id="pip">
      <div id="scan_res" style="color:green; font-weight:bold;"></div>
      
      <br><br>
      <button type="submit" class="btn-red">儲存並重啟 (Save & Reboot)</button>
    </form>
  </div>

<script>
  // 頁面載入時讀取設定
  fetch('/config').then(res => res.json()).then(data => {
    // 1. 修復 MAC 顯示
    document.getElementById("dev_mac").innerText = data.mac;
    // 2. 修復 WiFi 顯示
    document.getElementById("ssid").value = data.ssid;
    document.getElementById("pass").value = data.pass;
    // 3. 填入 Target Serial
    document.getElementById("t_ser").value = data.t_ser;
    document.getElementById("pip").value = data.pip;
  });

  // 定時更新狀態
  setInterval(function() {
    fetch('/status').then(response => response.json()).then(data => {
      document.getElementById("v_serial").innerHTML = data.serial ? data.serial : "(Waiting...)"; 
      document.getElementById("v_st").innerHTML = data.st;
      document.getElementById("v_bc").innerHTML = data.bc;
      document.getElementById("v_bp").innerHTML = data.bp;
      document.getElementById("v_cc").innerHTML = data.cc;
      document.getElementById("v_cp").innerHTML = data.cp;
      document.getElementById("sys_status").innerHTML = data.msg;
      
      var mStatus = document.getElementById("mqtt_status");
      mStatus.innerHTML = "MQTT: " + data.mqtt_state;
      mStatus.style.color = (data.mqtt_state === "Connected") ? "green" : "red";
      
      if(data.detectedIP && data.detectedIP.length > 7 && document.getElementById("pip").value != data.detectedIP) {
          document.getElementById("scan_res").innerHTML = "已鎖定序號，IP: " + data.detectedIP;
          document.getElementById("pip").value = data.detectedIP; 
      }
    });
  }, 2000);
</script>
</body></html>
)rawliteral";

// --- 前置宣告 ---
void startScan();
void sendSNMPRequest(IPAddress target);
bool checkPort9100(String ip);
void foundPrinter(String targetIP);
void mqttLoop();
void sendDataToMQTT();

// --- SNMP 回調 ---
void onSNMPMessage(const SNMP::Message *message, const IPAddress remote, const uint16_t port) {
  SNMP::VarBindList *varbindlist = message->getVarBindList();
  String currentSerial = "";

  for (unsigned int index = 0; index < varbindlist->count(); ++index) {
    SNMP::VarBind *varbind = (*varbindlist)[index];
    const char *name = varbind->getName();
    SNMP::BER *value = varbind->getValue();

    if (value) {
        String oidStr = String(name);

        if (value->getType() == SNMP::Type::OctetString) {
            String val = String(static_cast<SNMP::OctetStringBER*>(value)->getValue());
            if (oidStr.endsWith(OID_PRT_SERIAL)) {
                currentSerial = val;
                // 如果是鎖定狀態，更新變數
                if (!isScanning) val_PrtSerial = val; 
            }
        }
        else if (value->getType() == SNMP::Type::Integer || 
                 value->getType() == SNMP::Type::Counter32 || 
                 value->getType() == SNMP::Type::Gauge32) {
            
            int val = 0;
            if (value->getType() == SNMP::Type::Integer) {
                val = static_cast<SNMP::IntegerBER*>(value)->getValue();
            } else if (value->getType() == SNMP::Type::Counter32) {
                val = static_cast<SNMP::Counter32BER*>(value)->getValue();
            } else if (value->getType() == SNMP::Type::Gauge32) {
                val = static_cast<SNMP::Gauge32BER*>(value)->getValue();
            }

            if (!isScanning) { // 只有鎖定後才更新計數器
                if (oidStr.endsWith(OID_SYS_TOTAL)) val_SysTotal = val;
                if (oidStr.endsWith(OID_COL_TOTAL)) val_ColTotal = val;
                if (oidStr.endsWith(OID_TOT_COPIES)) val_TotCopies = val;
                if (oidStr.endsWith(OID_COL_COPIES)) val_ColCopies = val;
                if (oidStr.endsWith(OID_COL_PRINTS)) val_ColPrints = val;
            }
        }
    }
  }

  // === 關鍵邏輯：掃描模式下的匹配 ===
  if (isScanning) {
      // 1. 如果使用者設定了 Target Serial
      if (cfg_target_serial != "") {
          if (currentSerial == cfg_target_serial) {
              // 序號匹配！鎖定這台
              val_PrtSerial = currentSerial; // 存下來
              foundPrinter(remote.toString());
          } else {
              Serial.print("IP "); Serial.print(remote);
              Serial.print(" Serial: "); Serial.print(currentSerial);
              Serial.println(" (Mismatch, skipping)");
          }
      } 
      // 2. 如果使用者沒設定 Serial (留空) -> Fallback: 鎖定第一台回傳的
      else {
          val_PrtSerial = currentSerial;
          foundPrinter(remote.toString());
      }
  } else {
      // 鎖定狀態：正常計算與上傳
      calc_BWTotal = val_SysTotal - val_ColTotal;
      calc_BWCopies = val_TotCopies - val_ColCopies;
      calc_BWPrints = calc_BWTotal - calc_BWCopies;
      
      if(calc_BWTotal < 0) calc_BWTotal = 0;
      if(calc_BWCopies < 0) calc_BWCopies = 0;
      if(calc_BWPrints < 0) calc_BWPrints = 0;
      
      statusMessage = "Online (SNMP OK)";
      
      if (val_SysTotal != last_sent_SysTotal && val_SysTotal > 0) {
          sendDataToMQTT();
      }
  }
}

// --- 初始化網絡 ---
void initNetwork() {
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_ETH_START: ETH.setHostname("esp32-printer-node"); break;
      case ARDUINO_EVENT_ETH_GOT_IP: Serial.print("LAN IP: "); Serial.println(ETH.localIP()); break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP: Serial.print("WiFi IP: "); Serial.println(WiFi.localIP()); break;
      default: break;
    }
  });
  ETH.begin(ETH_TYPE, ETH_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_POWER_PIN, ETH_CLK_MODE);
  if (cfg_ssid != "") WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
}

// --- MQTT 邏輯 ---
void mqttLoop() {
  if (String(MQTT_BROKER) == "") return; 

  if (!mqttClient.connected()) {
    static unsigned long lastMqttRetry = 0;
    if (millis() - lastMqttRetry > 5000) {
      lastMqttRetry = millis();
      String clientId = "WT32-" + deviceMAC;
      clientId.replace(":", "");
      
      if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        Serial.println("✅ MQTT Connected!");
        String topic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/status";
        mqttClient.publish(topic.c_str(), "online");

        if (val_SysTotal > 0) {
            sendDataToMQTT();
        }
      }
    }
  } else {
    mqttClient.loop();
    
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 300000) {
        lastHeartbeat = millis();
        String topic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/status";
        mqttClient.publish(topic.c_str(), "online");
    }
  }
}

void sendDataToMQTT() {
    if (!mqttClient.connected()) return;

    String json = "{";
    json += "\"mac\":\"" + deviceMAC + "\",";
    json += "\"serial\":\"" + val_PrtSerial + "\","; 
    json += "\"ip\":\"" + cfg_printer_ip + "\",";
    json += "\"sys_total\":" + String(val_SysTotal) + ",";
    json += "\"bw_total\":" + String(calc_BWTotal) + ",";
    json += "\"bw_copy\":" + String(calc_BWCopies) + ",";
    json += "\"bw_print\":" + String(calc_BWPrints) + ",";
    json += "\"col_total\":" + String(val_ColTotal) + ",";
    json += "\"col_copy\":" + String(val_ColCopies) + ",";
    json += "\"col_print\":" + String(val_ColPrints);
    json += "}";

    String topic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC;
    Serial.println("📤 MQTT Sent: " + json);
    if(mqttClient.publish(topic.c_str(), json.c_str())) {
        last_sent_SysTotal = val_SysTotal;
    }
}

// --- SNMP 發送 ---
void sendSNMPRequest(IPAddress target) {
    SNMP::Message *message = new SNMP::Message(SNMP::Version::V1, "public", SNMP::Type::GetRequest);
    
    // 總是讀取 Serial，以便進行匹配
    message->add(OID_PRT_SERIAL, new SNMP::NullBER()); 
    
    // 如果不在掃描模式，才讀取計數器 (減少掃描時的封包量)
    if (!isScanning) {
        message->add(OID_SYS_TOTAL, new SNMP::NullBER());
        message->add(OID_COL_TOTAL, new SNMP::NullBER());
        message->add(OID_TOT_COPIES, new SNMP::NullBER());
        message->add(OID_COL_COPIES, new SNMP::NullBER());
        message->add(OID_COL_PRINTS, new SNMP::NullBER());
    }
    
    if(snmp.send(message, target, 161)) {
       lastRequestTime = millis();
    }
    delete message;
}

// --- 找到打印機 ---
void foundPrinter(String targetIP) {
    Serial.println("🎉 Printer LOCKED: " + targetIP);
    preferences.begin("net_config", false);
    preferences.putString("pip", targetIP);
    preferences.end();
    cfg_printer_ip = targetIP;
    statusMessage = "Locked: " + targetIP;
    isScanning = false; // 停止掃描
    
    // 立即發送一次完整請求以更新數據
    IPAddress target;
    target.fromString(cfg_printer_ip);
    sendSNMPRequest(target); 
}

void startScan() {
    isScanning = true; scanCurrentIP = 1;
    if (cfg_target_serial != "") {
        statusMessage = "Scanning for Serial: " + cfg_target_serial;
    } else {
        statusMessage = "Scanning for ANY Printer...";
    }
    Serial.println(statusMessage);
}

void processScanLoop() {
    if (!isScanning) return;
    IPAddress local = (ETH.linkUp()) ? ETH.localIP() : WiFi.localIP();
    if (local[0] == 0) { isScanning = false; return; }
    String subnet = String(local[0]) + "." + String(local[1]) + "." + String(local[2]) + ".";

    for (int i = 0; i < SCAN_BATCH_SIZE; i++) {
        if (scanCurrentIP >= 255) { isScanning = false; statusMessage = "Not Found"; return; }
        if (scanCurrentIP == local[3]) { scanCurrentIP++; continue; }
        
        String targetIPStr = subnet + String(scanCurrentIP);
        
        // 1. 先用 TCP Port 9100 快速過濾
        WiFiClient client;
        if (client.connect(targetIPStr.c_str(), 9100, SCAN_CONNECT_TIMEOUT)) {
            client.stop(); 
            // 2. 發現 Port 9100 開啟 -> 發送 SNMP 詢問 Serial
            Serial.print("Checking: "); Serial.println(targetIPStr);
            IPAddress targetIP;
            targetIP.fromString(targetIPStr);
            sendSNMPRequest(targetIP);
        }
        scanCurrentIP++;
    }
}

bool checkPort9100(String ip) {
  WiFiClient client;
  if (client.connect(ip.c_str(), 9100, 200)) { client.stop(); return true; }
  return false;
}

void setup() {
  Serial.begin(115200);
  
  // 1. 讀取設定
  preferences.begin("net_config", false);
  cfg_ssid = preferences.getString("ssid", "");
  cfg_pass = preferences.getString("pass", "");
  cfg_printer_ip = preferences.getString("pip", "");
  cfg_target_serial = preferences.getString("t_ser", ""); // 讀取 Target Serial
  preferences.end();

  initNetwork();
  snmp.begin(udp);
  snmp.onMessage(onSNMPMessage);
  
  // 修正：MAC Address 獲取放在 initNetwork 之後
  // 如果 ETH 沒插，嘗試拿 WiFi MAC，確保有值
  deviceMAC = ETH.macAddress();
  if (deviceMAC == "00:00:00:00:00:00") {
      deviceMAC = WiFi.macAddress();
  }
  Serial.println("Device MAC: " + deviceMAC);

  if (String(MQTT_BROKER) != "") {
      mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  }

  server.on("/", HTTP_GET, []() { server.send(200, "text/html; charset=utf-8", index_html); });
  
  server.on("/config", HTTP_GET, []() {
    String json = "{";
    json += "\"mac\":\"" + deviceMAC + "\",";
    json += "\"ssid\":\"" + cfg_ssid + "\",";  // 修復 WiFi 顯示
    json += "\"pass\":\"" + cfg_pass + "\",";  // 修復 WiFi 顯示
    json += "\"t_ser\":\"" + cfg_target_serial + "\","; // 回傳 Target Serial
    json += "\"pip\":\"" + cfg_printer_ip + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/save", HTTP_POST, []() {
    preferences.begin("net_config", false);
    preferences.putString("ssid", server.arg("ssid"));
    preferences.putString("pass", server.arg("pass"));
    preferences.putString("t_ser", server.arg("t_ser")); // 儲存 Target Serial
    preferences.putString("pip", server.arg("pip"));
    preferences.end();
    
    server.send(200, "text/html; charset=utf-8", "Saved! Rebooting...");
    delay(500); ESP.restart(); 
  });
  
  server.on("/status", HTTP_GET, []() {
    String mqttState = mqttClient.connected() ? "Connected" : "Disconnected";
    String json = "{";
    json += "\"serial\":\"" + val_PrtSerial + "\",";
    json += "\"cc\":" + String(val_ColCopies) + ",";
    json += "\"cp\":" + String(val_ColPrints) + ",";
    json += "\"ct\":" + String(val_ColTotal) + ",";
    json += "\"bc\":" + String(calc_BWCopies) + ",";
    json += "\"bp\":" + String(calc_BWPrints) + ",";
    json += "\"bt\":" + String(calc_BWTotal) + ",";
    json += "\"st\":" + String(val_SysTotal) + ",";
    json += "\"msg\":\"" + statusMessage + "\",";
    json += "\"mqtt_state\":\"" + mqttState + "\",";
    json += "\"detectedIP\":\"" + cfg_printer_ip + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.begin();

  // 等待網路連接
  unsigned long startWait = millis();
  while(!ETH.linkUp() && WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) { delay(100); }

  // 判斷邏輯：
  // 1. 如果 IP 為空 -> 進入掃描模式
  // 2. 掃描模式下會檢查 cfg_target_serial (如果有填就只找那台，沒填就找第一台)
  if (cfg_printer_ip == "") {
      startScan(); 
  } else {
      IPAddress target;
      target.fromString(cfg_printer_ip);
      sendSNMPRequest(target); 
  }
}

void loop() {
  server.handleClient();
  snmp.loop();
  mqttLoop(); 

  if (isScanning) processScanLoop(); 

  unsigned long currentMillis = millis();

  // 如果已經鎖定 (非掃描模式)，定時發送請求
  if (!isScanning && cfg_printer_ip != "" && (currentMillis - lastRequestTime > SNMP_INTERVAL)) {
      IPAddress target;
      target.fromString(cfg_printer_ip);
      sendSNMPRequest(target);
  }
 
  // Watchdog: 如果長時間沒反應，檢查 9100 Port，若不通則重新搜尋
  static unsigned long lastSuccessTime = millis();
  if (String(statusMessage).indexOf("Online") >= 0) lastSuccessTime = currentMillis;

  if (!isScanning && cfg_printer_ip != "" && (currentMillis - lastSuccessTime > 60000)) {
      if (checkPort9100(cfg_printer_ip)) {
          statusMessage = "Online / SNMP Error";
          lastSuccessTime = currentMillis; 
      } else {
          statusMessage = "Lost connection, rescanning...";
          cfg_printer_ip = ""; // 清空 IP
          startScan(); // 重新掃描 (這時會根據存好的 Serial 找)
      }
  }
}