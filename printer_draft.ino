#include <Arduino.h>
#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <HTTPUpdate.h>

// ==========================================
//          固件版本
// ==========================================
#define FIRMWARE_VERSION "1.0.0"

// ==========================================
//          配置区 (硬编码参数)
// ==========================================
// MQTT 服务器配置
const char* MQTT_BROKER = "192.168.14.70";      // MQTT 服务器 IP 地址
const int MQTT_PORT = 1883;                     // MQTT 端口
const char* MQTT_USER = "admin";                // MQTT 用户名
const char* MQTT_PASS = "admin123";             // MQTT 密码
const char* MQTT_TOPIC_PREFIX = "device/data";  // MQTT 主题前缀

// --- WT32-ETH01 以太网引脚配置 ---
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN  // 以太网时钟模式
#define ETH_POWER_PIN 16                 // 以太网电源控制引脚
#define ETH_TYPE ETH_PHY_LAN8720         // 以太网 PHY 芯片类型
#define ETH_ADDR 1                       // 以太网 PHY 地址
#define ETH_MDC_PIN 23                   // MDC 引脚 (管理数据时钟)
#define ETH_MDIO_PIN 18                  // MDIO 引脚 (管理数据输入输出)

// --- 全局对象实例 ---
WebServer server(80);                // Web 服务器，端口 80
Preferences preferences;             // 非易失性存储，用于保存配置
WiFiClient espClient;                // WiFi 客户端，用于 MQTT 连接
PubSubClient mqttClient(espClient);  // MQTT 客户端

// --- 配置参数 (从 Preferences 读取) ---
String cfg_ssid = "";  // WiFi SSID
String cfg_pass = "";  // WiFi 密码

// --- 系统状态变量 ---
String statusMessage = "System Booting...";  // 当前状态消息
String deviceMAC = "";                       // 设备 MAC 地址

// --- Web 配置页面 HTML (存储在程序存储器中) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <meta charset="utf-8">
  <title>WT32-ETH01 Device Node</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 20px; background-color: #eef2f3; }
    .card { background: #fff; padding: 20px; margin: 15px auto; max-width: 500px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
    input { width: 95%; padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; }
    button { padding: 10px 20px; background: #28a745; color: white; border: none; cursor: pointer; border-radius: 4px; font-size: 16px; }
    .btn-red { background: #dc3545; }
    .header-box { background: #444; color: #fff; font-weight: bold; padding: 8px; margin-top: 15px; border-radius: 4px; }
    .mac-addr { font-size: 1.2em; color: #0056b3; font-weight: bold; font-family: monospace; letter-spacing: 1px; }
    .status { color: #666; font-style: italic; }
    h2 { color: #333; }
    label { font-weight: bold; display: block; text-align: left; margin-top: 10px; }
  </style>
</head><body>
  <h2>⚙️ Device Node Config OTA Update</h2>
  
  <div class="card">
    <div style="text-align:center; padding-bottom:10px; border-bottom:2px solid #eee;">
      <div>Device MAC (WT32)</div>
      <div id="dev_mac" class="mac-addr">Loading...</div>
    </div>
  </div>

  <div class="card">
    <div class="header-box">系统状态 (System Status)</div>
    <p class="status" id="sys_status">Connecting...</p>
    <p class="status" id="mqtt_status">MQTT: -</p>
  </div>

  <div class="card">
    <h3>⚙️ 系统设置 (Config)</h3>
    <form action="/save" method="POST">
      <div class="header-box">网络 (WiFi)</div>
      <label>SSID</label><input type="text" name="ssid" id="ssid">
      <label>Password</label><input type="password" name="pass" id="pass">
      
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
  });

  // 定时更新状态 (每 2 秒)
  setInterval(function() {
    fetch('/status').then(response => response.json()).then(data => {
      document.getElementById("sys_status").innerHTML = data.msg;
      
      // 更新 MQTT 状态
      var mStatus = document.getElementById("mqtt_status");
      mStatus.innerHTML = "MQTT: " + data.mqtt_state;
      mStatus.style.color = (data.mqtt_state === "Connected") ? "green" : "red";
    });
  }, 2000);
</script>
</body></html>
)rawliteral";

// --- 函数前置声明 ---
void mqttLoop();        // MQTT 循环处理
void sendDataToMQTT();  // 发送数据到 MQTT

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
        mqttClient.subscribe("device/ota/broadcast");  // 广播更新
        Serial.println("已订阅 OTA 更新主题: " + otaTopic);
      }
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
      json += "\"ip\":\"" + ip.toString() + "\"";
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

  // 构建 MQTT 主题: device/data/{MAC地址}
  String topic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC;
  Serial.println("📤 MQTT Sent: " + json);

  // 发送数据
  mqttClient.publish(topic.c_str(), json.c_str());
}

// --- 远程 OTA 更新函数 ---
void performOTAUpdate(String url) {
  Serial.println("开始 OTA 更新: " + url);

  WiFiClient client;
  t_httpUpdate_return ret = httpUpdate.update(client, url);

  if (ret == HTTP_UPDATE_OK) {
    Serial.println("OTA 成功，重启...");
    ESP.restart();
  } else {
    Serial.printf("OTA 失败: %s\n", httpUpdate.getLastErrorString().c_str());
  }
}

// --- MQTT 消息回调 ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("收到消息 [%s]: %s\n", topic, message.c_str());

  // OTA 主题：直接发送固件 URL
  String otaTopic = String(MQTT_TOPIC_PREFIX) + "/" + deviceMAC + "/ota/update";
  if (String(topic) == otaTopic || String(topic) == "device/ota/broadcast") {
    // message 就是固件 URL，如：http://192.168.14.70/firmware.bin
    performOTAUpdate(message);
  }
}

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
    json += "\"pass\":\"" + cfg_pass + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // 保存配置 API：保存配置并重启
  server.on("/save", HTTP_POST, []() {
    preferences.begin("net_config", false);
    preferences.putString("ssid", server.arg("ssid"));
    preferences.putString("pass", server.arg("pass"));
    preferences.end();

    server.send(200, "text/html; charset=utf-8", "Saved! Rebooting...");
    delay(500);
    ESP.restart();  // 重启设备以应用新配置
  });

  // 状态 API：返回实时状态数据
  server.on("/status", HTTP_GET, []() {
    String mqttState = mqttClient.connected() ? "Connected" : "Disconnected";
    String json = "{";
    json += "\"msg\":\"" + statusMessage + "\",";
    json += "\"mqtt_state\":\"" + mqttState + "\",";
    json += "\"firmware_version\":\"" FIRMWARE_VERSION "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  // 启动 Web 服务器
  server.begin();
  Serial.println("Web 服务器已启动");
}

// --- Arduino 初始化函数 ---
void setup() {
  // 初始化串口，用于调试输出
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n======================================");
  Serial.printf("固件版本: %s\n", FIRMWARE_VERSION);
  Serial.println("======================================");

  // 步骤 1: 从非易失性存储读取配置
  preferences.begin("net_config", false);
  cfg_ssid = preferences.getString("ssid", "");  // WiFi SSID
  cfg_pass = preferences.getString("pass", "");  // WiFi 密码
  preferences.end();

  // 步骤 2: 初始化网络 (以太网和 WiFi)
  initNetwork();

  // 步骤 3: 获取设备 MAC 地址
  // 注意：MAC 地址获取放在 initNetwork 之后
  // 如果以太网未连接，尝试获取 WiFi MAC，确保有值
  deviceMAC = ETH.macAddress();
  if (deviceMAC == "00:00:00:00:00:00") {
    deviceMAC = WiFi.macAddress();
  }
  Serial.println("Device MAC: " + deviceMAC);

  // 步骤 4: 配置 MQTT 服务器
  if (String(MQTT_BROKER) != "") {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
  }

  // 步骤 5: 初始化 Web 服务器
  initWebServer();

  // 步骤 6: 等待网络连接 (最多等待 5 秒)
  unsigned long startWait = millis();
  while (!ETH.linkUp() && WiFi.status() != WL_CONNECTED && millis() - startWait < 5000) {
    delay(100);
  }

  statusMessage = "System Ready";
}

// --- 测试代码 ---
void testCode() {
  static unsigned long lastTestTime = 0;
  const unsigned long testInterval = 1000;  // 每 1 秒执行一次

  if (millis() - lastTestTime >= testInterval) {
    lastTestTime = millis();
    Serial.println("Test Code");
    Serial.println("版本2.0");
  }
}

// --- Arduino 主循环函数 ---
// 此函数会不断循环执行，处理各种任务
void loop() {
  // 处理 Web 服务器请求
  server.handleClient();

  // 处理 MQTT 连接和消息
  mqttLoop();

  testCode();
}
