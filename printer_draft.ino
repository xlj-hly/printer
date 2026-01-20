/*
 * WT32-ETH01 Printer Node
 * 
 * 功能：
 * - 通过 SNMP 读取 Ricoh 打印机计数器数据
 * - 通过 MQTT 上报数据到服务器
 * - 支持远程 OTA 固件更新
 * - Web 配置界面
 * 
 * 硬件：WT32-ETH01 (ESP32 + LAN8720)
 */

#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <SNMP.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>

// ==========================================
//          固件版本
// ==========================================
#define FIRMWARE_VERSION "2.5.0"

// ==========================================
//          配置区 (硬编码参数)
// ==========================================
// MQTT 服务器配置
const char* MQTT_BROKER = "192.168.14.70";       // MQTT 服务器 IP 地址
const int MQTT_PORT = 1883;                      // MQTT 端口
const char* MQTT_USER = "admin";                 // MQTT 用户名
const char* MQTT_PASS = "admin123";              // MQTT 密码
const char* MQTT_TOPIC_PREFIX = "printer/data";  // MQTT 主题前缀

// --- WT32-ETH01 以太网引脚配置 ---
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN  // 以太网时钟模式
#define ETH_POWER_PIN 16                 // 以太网电源控制引脚
#define ETH_TYPE ETH_PHY_LAN8720         // 以太网 PHY 芯片类型
#define ETH_ADDR 1                       // 以太网 PHY 地址
#define ETH_MDC_PIN 23                   // MDC 引脚 (管理数据时钟)
#define ETH_MDIO_PIN 18                  // MDIO 引脚 (管理数据输入输出)

// --- 系统参数配置 ---
const int SNMP_INTERVAL = 5000;       // SNMP 查询间隔 (毫秒)
const int SCAN_CONNECT_TIMEOUT = 50;  // 扫描连接超时时间 (毫秒)
const int SCAN_BATCH_SIZE = 10;       // 每次扫描的 IP 数量批次大小

// --- 全局对象实例 ---
WebServer server(80);                // Web 服务器，端口 80
Preferences preferences;             // 非易失性存储，用于保存配置
WiFiUDP udp;                         // UDP 套接字，用于 SNMP 通信
SNMP::Manager snmp;                  // SNMP 管理器
WiFiClient espClient;                // WiFi 客户端，用于 MQTT 连接
PubSubClient mqttClient(espClient);  // MQTT 客户端

// --- 配置参数 (从 Preferences 读取) ---
String cfg_ssid = "";           // WiFi SSID
String cfg_pass = "";           // WiFi 密码
String cfg_printer_ip = "";     // 打印机 IP 地址
String cfg_target_serial = "";  // 目标打印机序列号 (用于精确搜索)

// --- 系统状态变量 ---
String statusMessage = "System Booting...";  // 当前状态消息
String deviceMAC = "";                       // 设备 MAC 地址
unsigned long lastRequestTime = 0;           // 上次 SNMP 请求时间
bool isScanning = false;                     // 是否正在扫描模式
int scanCurrentIP = 1;                       // 当前扫描的 IP 地址 (最后一位)

// --- SNMP 读取的原始数值 ---
int val_SysTotal = 0;       // 系统总打印数 (黑白 + 彩色)
int val_ColTotal = 0;       // 彩色总打印数
int val_TotCopies = 0;      // 总复印数 (黑白 + 彩色)
int val_ColCopies = 0;      // 彩色复印数
int val_ColPrints = 0;      // 彩色打印数
String val_PrtSerial = "";  // 打印机序列号

// --- 计算得出的数值 ---
int calc_BWTotal = 0;   // 黑白总打印数 = 系统总数 - 彩色总数
int calc_BWCopies = 0;  // 黑白复印数 = 总复印数 - 彩色复印数
int calc_BWPrints = 0;  // 黑白打印数 = 黑白总数 - 黑白复印数

// --- MQTT 发送控制 ---
int last_sent_SysTotal = -1;  // 上次发送的系统总数，用于检测变化

// --- Ricoh 打印机 SNMP OID (对象标识符) ---
// 这些 OID 用于从 Ricoh 打印机获取不同的数据
const char* OID_PRT_SERIAL = "1.3.6.1.2.1.43.5.1.1.17.1";           // 打印机序列号
const char* OID_SYS_TOTAL = "1.3.6.1.2.1.43.10.2.1.4.1.1";          // 系统总打印数
const char* OID_COL_TOTAL = "1.3.6.1.4.1.367.3.2.1.2.19.5.1.4.1";   // 彩色总打印数
const char* OID_TOT_COPIES = "1.3.6.1.4.1.367.3.2.1.2.19.4.0";      // 总复印数
const char* OID_COL_COPIES = "1.3.6.1.4.1.367.3.2.1.2.16.7.0";      // 彩色复印数
const char* OID_COL_PRINTS = "1.3.6.1.4.1.367.3.2.1.2.16.3.1.2.5";  // 彩色打印数

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


// --- 函数前置声明 ---
void startScan();                                                    // 开始扫描打印机
void sendSNMPRequest(IPAddress target);                              // 发送 SNMP 请求
bool checkPort9100(String ip);                                       // 检查打印机端口 9100 是否开放
void foundPrinter(String targetIP);                                  // 找到打印机后的处理
void mqttLoop();                                                     // MQTT 循环处理
void sendDataToMQTT();                                               // 发送数据到 MQTT
void performOTAUpdate(String url);                                   // 远程 OTA 更新函数
void mqttCallback(char* topic, byte* payload, unsigned int length);  // MQTT 消息回调函数
void initWebServer();                                                // 初始化 Web 服务器
void printerSNMPLoop();                                              // 定时 SNMP 请求
void printerWatchdog();                                              // 打印机看门狗检测
void connectMQTT();                                                  // 连接 MQTT

// --- SNMP 消息回调函数 ---
// 当收到 SNMP 响应时，此函数会被调用
void onSNMPMessage(const SNMP::Message* message, const IPAddress remote, const uint16_t port) {
  // 获取 SNMP 响应中的变量绑定列表
  SNMP::VarBindList* varbindlist = message->getVarBindList();
  String currentSerial = "";  // 当前收到的序列号

  // 遍历所有变量绑定，解析每个 OID 的值
  for (unsigned int index = 0; index < varbindlist->count(); ++index) {
    SNMP::VarBind* varbind = (*varbindlist)[index];
    const char* name = varbind->getName();   // OID 名称
    SNMP::BER* value = varbind->getValue();  // OID 的值

    if (value) {
      String oidStr = String(name);

      // 处理字符串类型 (如序列号)
      if (value->getType() == SNMP::Type::OctetString) {
        String val = String(static_cast<SNMP::OctetStringBER*>(value)->getValue());
        if (oidStr.endsWith(OID_PRT_SERIAL)) {
          currentSerial = val;
          // 如果是锁定状态，更新序列号变量
          if (!isScanning) val_PrtSerial = val;
        }
      }
      // 处理整数类型 (如计数器值)
      else if (value->getType() == SNMP::Type::Integer || value->getType() == SNMP::Type::Counter32 || value->getType() == SNMP::Type::Gauge32) {

        int val = 0;
        // 根据不同的数据类型提取整数值
        if (value->getType() == SNMP::Type::Integer) {
          val = static_cast<SNMP::IntegerBER*>(value)->getValue();
        } else if (value->getType() == SNMP::Type::Counter32) {
          val = static_cast<SNMP::Counter32BER*>(value)->getValue();
        } else if (value->getType() == SNMP::Type::Gauge32) {
          val = static_cast<SNMP::Gauge32BER*>(value)->getValue();
        }

        // 只有锁定后才更新计数器 (扫描模式下不更新，避免干扰)
        if (!isScanning) {
          if (oidStr.endsWith(OID_SYS_TOTAL)) val_SysTotal = val;
          if (oidStr.endsWith(OID_COL_TOTAL)) val_ColTotal = val;
          if (oidStr.endsWith(OID_TOT_COPIES)) val_TotCopies = val;
          if (oidStr.endsWith(OID_COL_COPIES)) val_ColCopies = val;
          if (oidStr.endsWith(OID_COL_PRINTS)) val_ColPrints = val;
        }
      }
    }
  }

  // === 关键逻辑：扫描模式下的匹配 ===
  if (isScanning) {
    // 情况 1: 如果用户设定了目标序列号
    if (cfg_target_serial != "") {
      if (currentSerial == cfg_target_serial) {
        // 序列号匹配！锁定这台打印机
        val_PrtSerial = currentSerial;  // 保存序列号
        foundPrinter(remote.toString());
      } else {
        // 序列号不匹配，跳过
        Serial.print("IP ");
        Serial.print(remote);
        Serial.print(" Serial: ");
        Serial.print(currentSerial);
        Serial.println(" (Mismatch, skipping)");
      }
    }
    // 情况 2: 如果用户没设定序列号 (留空) -> 回退方案: 锁定第一台响应的打印机
    else {
      val_PrtSerial = currentSerial;
      foundPrinter(remote.toString());
    }
  } else {
    // 锁定状态：正常计算与上传数据
    // 计算黑白打印数据 (通过总数减去彩色数)
    calc_BWTotal = val_SysTotal - val_ColTotal;
    calc_BWCopies = val_TotCopies - val_ColCopies;
    calc_BWPrints = calc_BWTotal - calc_BWCopies;

    // 防止负数 (数据异常时的保护)
    if (calc_BWTotal < 0) calc_BWTotal = 0;
    if (calc_BWCopies < 0) calc_BWCopies = 0;
    if (calc_BWPrints < 0) calc_BWPrints = 0;

    statusMessage = "Online (SNMP OK)";

    // 只有当系统总数发生变化且大于 0 时才发送 MQTT (避免重复发送)
    if (val_SysTotal != last_sent_SysTotal && val_SysTotal > 0) {
      sendDataToMQTT();
    }
  }
}

// --- 初始化网络连接 ---
// 优先使用以太网，如果配置了 WiFi 则同时启用 WiFi
void initNetwork() {
  // 注册 WiFi 事件回调，用于监控网络状态
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_ETH_START:
        // 以太网启动时设置主机名
        ETH.setHostname("esp32-device-node");
        break;
      case ARDUINO_EVENT_ETH_GOT_IP:
        // 以太网获取到 IP 地址
        Serial.print("LAN IP: ");
        Serial.println(ETH.localIP());
        statusMessage = "Ethernet Connected: " + ETH.localIP().toString();
        break;
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        // WiFi 获取到 IP 地址
        Serial.print("WiFi IP: ");
        Serial.println(WiFi.localIP());
        statusMessage = "WiFi Connected: " + WiFi.localIP().toString();
        break;
      default:
        break;
    }
  });

  // 初始化以太网 (优先使用)
  ETH.begin(ETH_TYPE, ETH_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_POWER_PIN, ETH_CLK_MODE);

  // 如果配置了 WiFi SSID，则连接 WiFi (作为备用)
  if (cfg_ssid != "") {
    WiFi.begin(cfg_ssid.c_str(), cfg_pass.c_str());
  }
}

// --- 连接 MQTT ---
void connectMQTT() {
  // 使用设备 MAC 地址生成唯一的客户端 ID
  String clientId = "WT32-" + deviceMAC;
  clientId.replace(":", "");  // 移除 MAC 地址中的冒号

  // 准备遗嘱主题和消息
  String willTopic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/status";
  const char* willMessage = "offline";  // 遗嘱消息内容

  // 尝试连接 MQTT 服务器，并设置遗嘱
  // 参数：客户端ID, 用户名, 密码, 遗嘱主题, QoS级别, 保留标志, 遗嘱消息
  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                         willTopic.c_str(), 1, true, willMessage)) {
    Serial.println("✅ MQTT Connected!");
    // 发送在线状态消息
    mqttClient.publish(willTopic.c_str(), "online", true);

    // 订阅 OTA 更新主题
    String otaTopic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/ota/update";
    mqttClient.subscribe(otaTopic.c_str());
    mqttClient.subscribe("printer/ota/broadcast");  // 广播更新
    Serial.println("已订阅 OTA 更新主题: " + otaTopic);
  }
}

// --- MQTT 连接管理循环 ---
// 负责维护 MQTT 连接，处理重连和心跳
void mqttLoop() {
  // 如果未配置 MQTT 服务器，直接返回
  if (String(MQTT_BROKER) == "") return;

  // 如果未连接，尝试重连
  if (!mqttClient.connected()) {
    static unsigned long lastMqttRetry = 0;
    // 每 5 秒尝试重连一次
    if (millis() - lastMqttRetry > 5000) {
      lastMqttRetry = millis();

      connectMQTT();
    }
  } else {
    // 已连接，处理 MQTT 消息循环
    mqttClient.loop();

    // 每 30 秒发送一次心跳 (30000 毫秒)
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 30000) {
      lastHeartbeat = millis();
      String topic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/status";
      IPAddress ip = ETH.localIP();
      if (ip == IPAddress(0, 0, 0, 0)) ip = WiFi.localIP();
      String json = "{";
      json += "\"mac\":\"" + deviceMAC + "\",";
      json += "\"status\":\"online\",";
      json += "\"ip\":\"" + ip.toString() + "\",";
      json += "\"cfg_target_serial\":\"" + cfg_target_serial + "\"";
      json += "}";
      mqttClient.publish(topic.c_str(), json.c_str(), true);  // 保留消息
    }
  }
}

// --- 发送数据到 MQTT 服务器 ---
void sendDataToMQTT() {
  // 检查 MQTT 连接状态
  if (!mqttClient.connected()) return;

  // 构建 JSON 数据包
  String json = "{";
  json += "\"mac\":\"" + deviceMAC + "\",";
  json += "\"status\":\"online\"";
  json += "}";

  // 构建 MQTT 主题: printer/data/{MAC地址}
  String topic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC;
  Serial.println("📤 MQTT Sent: " + json);

  // 发送数据
  mqttClient.publish(topic.c_str(), json.c_str());
}

// --- 远程 OTA 更新函数 ---
void performOTAUpdate(String url) {
  Serial.println("======================================");
  Serial.println("开始 OTA 更新: " + url);
  Serial.printf("当前固件版本: %s\n", FIRMWARE_VERSION);

  // 使用 WiFiClient（在 ESP32 中，WiFiClient 也支持以太网连接）
  WiFiClient client;

  // 设置 OTA 更新回调，显示进度
  httpUpdate.onStart([]() {
    Serial.println("OTA 更新开始，请勿断电...");
  });

  httpUpdate.onEnd([]() {
    Serial.println("OTA 更新完成，准备重启...");
  });

  httpUpdate.onProgress([](int cur, int total) {
    Serial.printf("OTA 进度: %d%% (%d/%d bytes)\n", (cur * 100) / total, cur, total);
  });

  httpUpdate.onError([](int err) {
    Serial.printf("OTA 更新错误代码: %d\n", err);
  });

  t_httpUpdate_return ret = httpUpdate.update(client, url);

  if (ret == HTTP_UPDATE_OK) {
    Serial.println("✅ OTA 成功，3秒后重启...");
    delay(3000);
    ESP.restart();
  } else {
    Serial.printf("❌ OTA 失败: %s\n", httpUpdate.getLastErrorString().c_str());
    Serial.printf("错误代码: %d\n", ret);
  }
  Serial.println("======================================");
}

// --- MQTT 消息回调 ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("📨 收到 MQTT 消息 [%s]: %s\n", topic, message.c_str());

  // OTA 主题：直接发送固件 URL
  String otaTopic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/ota/update";
  String broadcastTopic = "printer/ota/broadcast";

  Serial.printf("🔍 检查 OTA 主题匹配:\n");
  Serial.printf("  个人主题: %s\n", otaTopic.c_str());
  Serial.printf("  广播主题: %s\n", broadcastTopic.c_str());
  Serial.printf("  收到主题: %s\n", topic);

  if (String(topic) == otaTopic || String(topic) == broadcastTopic) {
    Serial.println("✅ 主题匹配，开始 OTA 更新...");
    // message 就是固件 URL，如：http://192.168.14.70/firmware.bin
    performOTAUpdate(message);
  } else {
    Serial.println("❌ 主题不匹配，忽略消息");
  }
}

// --- 发送 SNMP 请求 ---
// 向目标 IP 发送 SNMP GetRequest 查询打印机数据
void sendSNMPRequest(IPAddress target) {
  // 创建 SNMP V1 GetRequest 消息，使用 "public" 作为社区字符串
  SNMP::Message* message = new SNMP::Message(SNMP::Version::V1, "public", SNMP::Type::GetRequest);

  // 总是读取序列号，以便进行匹配 (扫描和锁定模式都需要)
  message->add(OID_PRT_SERIAL, new SNMP::NullBER());

  // 如果不在扫描模式，才读取计数器 (减少扫描时的数据包大小，提高扫描速度)
  if (!isScanning) {
    message->add(OID_SYS_TOTAL, new SNMP::NullBER());   // 系统总打印数
    message->add(OID_COL_TOTAL, new SNMP::NullBER());   // 彩色总打印数
    message->add(OID_TOT_COPIES, new SNMP::NullBER());  // 总复印数
    message->add(OID_COL_COPIES, new SNMP::NullBER());  // 彩色复印数
    message->add(OID_COL_PRINTS, new SNMP::NullBER());  // 彩色打印数
  }

  // 发送 SNMP 请求到目标 IP 的 161 端口 (SNMP 标准端口)
  if (snmp.send(message, target, 161)) {
    lastRequestTime = millis();  // 更新最后请求时间
  }

  // 释放消息内存
  delete message;
}

// --- 找到打印机后的处理 ---
// 当扫描到匹配的打印机时调用此函数
void foundPrinter(String targetIP) {
  Serial.println("🎉 Printer LOCKED: " + targetIP);

  // 将打印机 IP 保存到非易失性存储，重启后仍有效
  preferences.begin("net_config", false);
  preferences.putString("pip", targetIP);
  preferences.end();

  // 更新配置和状态
  cfg_printer_ip = targetIP;
  statusMessage = "Locked: " + targetIP;
  isScanning = false;  // 停止扫描模式

  // 立即发送一次完整的 SNMP 请求以更新所有数据
  IPAddress target;
  target.fromString(cfg_printer_ip);
  sendSNMPRequest(target);
}

// --- 开始扫描打印机 ---
// 初始化扫描模式，准备扫描网段内的打印机
void startScan() {
  isScanning = true;  // 进入扫描模式
  scanCurrentIP = 1;  // 从 IP 地址最后一位 1 开始扫描

  // 根据是否配置了目标序列号，设置不同的状态消息
  if (cfg_target_serial != "") {
    statusMessage = "Scanning for Serial: " + cfg_target_serial;
  } else {
    statusMessage = "Scanning for ANY Printer...";
  }
  Serial.println(statusMessage);
}

// --- 扫描循环处理 ---
// 在扫描模式下，批量检查网段内的 IP 地址
void processScanLoop() {
  // 如果不在扫描模式，直接返回
  if (!isScanning) return;

  // 获取本地 IP 地址 (优先使用以太网，否则使用 WiFi)
  IPAddress local = (ETH.linkUp()) ? ETH.localIP() : WiFi.localIP();

  // 如果本地 IP 无效，停止扫描
  if (local[0] == 0) {
    isScanning = false;
    return;
  }

  // 构建子网前缀 (例如: 192.168.1.)
  String subnet = String(local[0]) + "." + String(local[1]) + "." + String(local[2]) + ".";

  // 批量扫描，每次处理 SCAN_BATCH_SIZE 个 IP
  for (int i = 0; i < SCAN_BATCH_SIZE; i++) {
    // 如果扫描到 255，说明整个网段扫描完毕
    if (scanCurrentIP >= 255) {
      isScanning = false;
      statusMessage = "Not Found";
      return;
    }

    // 跳过自己的 IP 地址
    if (scanCurrentIP == local[3]) {
      scanCurrentIP++;
      continue;
    }

    // 构建目标 IP 地址
    String targetIPStr = subnet + String(scanCurrentIP);

    // 步骤 1: 先用 TCP Port 9100 快速过滤 (打印机通常开放此端口)
    // 这样可以快速排除非打印机设备，减少 SNMP 请求
    WiFiClient client;
    if (client.connect(targetIPStr.c_str(), 9100, SCAN_CONNECT_TIMEOUT)) {
      client.stop();  // 关闭连接，我们只需要确认端口开放

      // 步骤 2: 发现 Port 9100 开启 -> 发送 SNMP 查询序列号
      Serial.print("Checking: ");
      Serial.println(targetIPStr);

      IPAddress targetIP;
      targetIP.fromString(targetIPStr);
      sendSNMPRequest(targetIP);  // 发送 SNMP 请求查询序列号
    }

    scanCurrentIP++;  // 移动到下一个 IP
  }
}

// --- 检查打印机端口 9100 是否开放 ---
// 用于看门狗检测，判断打印机是否仍然在线
bool checkPort9100(String ip) {
  WiFiClient client;
  // 尝试连接端口 9100，超时时间 200 毫秒
  if (client.connect(ip.c_str(), 9100, 200)) {
    client.stop();  // 关闭连接
    return true;    // 端口开放，打印机可能在线
  }
  return false;  // 端口关闭，打印机可能离线
}

// --- 初始化 Web 服务器 ---
void initWebServer() {
  // 主页：返回配置页面 HTML
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", index_html);
  });

  // 配置 API：返回当前配置的 JSON
  server.on("/config", HTTP_GET, []() {
    String json = "{";
    json += "\"mac\":\"" + deviceMAC + "\",";
    json += "\"ssid\":\"" + cfg_ssid + "\",";
    json += "\"pass\":\"" + cfg_pass + "\",";
    json += "\"t_ser\":\"" + cfg_target_serial + "\",";  // 返回目标序列号
    json += "\"pip\":\"" + cfg_printer_ip + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // 保存配置 API：保存配置并重启
  server.on("/save", HTTP_POST, []() {
    preferences.begin("net_config", false);
    preferences.putString("ssid", server.arg("ssid"));    // 保存 WiFi SSID
    preferences.putString("pass", server.arg("pass"));    // 保存 WiFi 密码
    preferences.putString("t_ser", server.arg("t_ser"));  // 保存目标打印机序列号
    preferences.putString("pip", server.arg("pip"));      // 保存打印机 IP 地址
    preferences.end();

    server.send(200, "text/html; charset=utf-8", "Saved! Rebooting...");  // 保存成功，重启设备
    delay(500);
    ESP.restart();  // 重启设备以应用新配置
  });

  // 状态 API：返回实时状态数据
  server.on("/status", HTTP_GET, []() {
    String mqttState = mqttClient.connected() ? "Connected" : "Disconnected";
    String json = "{";
    json += "\"serial\":\"" + val_PrtSerial + "\",";      // 打印机序列号
    json += "\"cc\":" + String(val_ColCopies) + ",";      // 彩色复印数
    json += "\"cp\":" + String(val_ColPrints) + ",";      // 彩色打印数
    json += "\"ct\":" + String(val_ColTotal) + ",";       // 彩色总数
    json += "\"bc\":" + String(calc_BWCopies) + ",";      // 黑白复印数
    json += "\"bp\":" + String(calc_BWPrints) + ",";      // 黑白打印数
    json += "\"bt\":" + String(calc_BWTotal) + ",";       // 黑白总数
    json += "\"st\":" + String(val_SysTotal) + ",";       // 系统总数
    json += "\"msg\":\"" + statusMessage + "\",";         // 状态消息
    json += "\"mqtt_state\":\"" + mqttState + "\",";      // MQTT 连接状态
    json += "\"detectedIP\":\"" + cfg_printer_ip + "\"";  // 检测到的打印机 IP
    json += "}";
    server.send(200, "application/json", json);
  });

  // 启动 Web 服务器
  server.begin();
  Serial.println("Web 服务器已启动");
}

// --- 定时 SNMP 请求 ---
void printerSNMPLoop() {
  if (isScanning || cfg_printer_ip == "") return;

  // 如果已经锁定打印机 (非扫描模式)，定时发送 SNMP 请求
  // 每隔 SNMP_INTERVAL 毫秒查询一次打印机数据
  if (millis() - lastRequestTime > SNMP_INTERVAL) {
    IPAddress target;
    target.fromString(cfg_printer_ip);
    sendSNMPRequest(target);
  }
}

// --- 打印机看门狗检测 ---
void printerWatchdog() {
  if (isScanning || cfg_printer_ip == "") return;

  static unsigned long lastSuccessTime = millis();
  unsigned long currentMillis = millis();

  // 如果状态包含 "Online"，更新成功时间
  if (String(statusMessage).indexOf("Online") >= 0) {
    lastSuccessTime = currentMillis;
  }

  // 如果超过 60 秒没有成功响应，执行看门狗检查
  if (currentMillis - lastSuccessTime > 60000) {
    // 检查打印机端口 9100 是否仍然开放
    if (checkPort9100(cfg_printer_ip)) {
      // 端口开放，但 SNMP 可能有问题
      statusMessage = "Online / SNMP Error";
      lastSuccessTime = currentMillis;
    } else {
      // 端口关闭，打印机可能离线，重新扫描
      statusMessage = "Lost connection, rescanning...";
      cfg_printer_ip = "";  // 清空 IP
      startScan();          // 重新扫描 (此时会根据保存的序列号查找)
    }
  }
}

// --- Arduino 初始化函数 ---
void setup() {
  Serial.begin(115200);

  Serial.println("\n======================================");
  Serial.printf("固件版本: %s\n", FIRMWARE_VERSION);
  Serial.println("======================================");

  // 步骤 1: 从非易失性存储读取配置
  preferences.begin("net_config", false);
  cfg_ssid = preferences.getString("ssid", "");
  cfg_pass = preferences.getString("pass", "");
  cfg_printer_ip = preferences.getString("pip", "");
  cfg_target_serial = preferences.getString("t_ser", "");
  preferences.end();

  // 步骤 2: 初始化网络
  initNetwork();

  // 步骤 3: 初始化 SNMP
  snmp.begin(udp);                // 启动 SNMP 管理器
  snmp.onMessage(onSNMPMessage);  // 注册 SNMP 消息回调函数

  // 步骤 4: 获取设备 MAC 地址
  deviceMAC = ETH.macAddress();
  if (deviceMAC == "00:00:00:00:00:00") {
    deviceMAC = WiFi.macAddress();
  }
  Serial.println("Device MAC: " + deviceMAC);

  // 步骤 5: 配置 MQTT 服务器
  if (String(MQTT_BROKER) != "") {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
  }

  // 步骤 6: 初始化 Web 服务器
  initWebServer();

  // 步骤 7: 等待网络连接
  unsigned long startWait = millis();
  while (!ETH.linkUp() && WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) {
    delay(100);
  }

  // 步骤 8: 判断启动模式
  // 情况 1: 如果打印机 IP 为空 -> 进入扫描模式
  // 情况 2: 如果已配置打印机 IP -> 直接连接
  // 扫描模式下会检查 cfg_target_serial (如果配置了就只找那台，没配置就找第一台)
  if (cfg_printer_ip == "") {
    startScan();
  } else {
    IPAddress target;
    target.fromString(cfg_printer_ip);
    sendSNMPRequest(target);
  }
}


// --- 测试代码 ---
void testCode() {
  static unsigned long lastTestTime = 0;
  const unsigned long testInterval = 1000;  // 每 1 秒执行一次

  if (millis() - lastTestTime >= testInterval) {
    lastTestTime = millis();
    Serial.println("Test Code");
    Serial.println("版本2.5.0");
  }
}

// --- Arduino 主循环函数 ---
void loop() {
  server.handleClient();  // 处理 Web 请求
  snmp.loop();            // 处理 SNMP 消息
  mqttLoop();             // 处理 MQTT 连接

  // 如果正在扫描模式，执行扫描循环
  if (isScanning) {
    processScanLoop();  // 扫描模式处理
  }

  printerSNMPLoop();  // 定时 SNMP 请求
  printerWatchdog();  // 打印机看门狗检测

  testCode();  // 测试代码
}
