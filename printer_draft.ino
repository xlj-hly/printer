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
#include <Update.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include <ArduinoJson.h>

#include "config.h"
#include "globals.h"
#include "html_content.h"
#include "ota.h"
#include "mqtt.h"
#include "snmp_handler.h"
#include "printer_monitor.h"

// --- 函数前置声明 ---
void initNetwork();    // 初始化网络连接
void initWebServer();  // 初始化 Web 服务器

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
        {
          // 以太网获取到 IP 地址
          IPAddress ip = ETH.localIP();
          Serial.printf("LAN IP: %s\n", ip.toString().c_str());
          statusMessage = "Ethernet Connected: " + ip.toString();
          break;
        }
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        {
          // WiFi 获取到 IP 地址
          IPAddress ip = WiFi.localIP();
          Serial.printf("WiFi IP: %s\n", ip.toString().c_str());
          statusMessage = "WiFi Connected: " + ip.toString();
          break;
        }
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

// --- 初始化 Web 服务器 ---
void initWebServer() {
  // 主页：返回配置页面 HTML
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", index_html);
  });

  // 配置 API：返回当前配置的 JSON
  server.on("/config", HTTP_GET, []() {
    StaticJsonDocument<200> doc;  // 栈上分配, 预分配 200 字节
    doc["mac"] = deviceMAC;
    doc["ssid"] = cfg_ssid;
    doc["pass"] = cfg_pass;
    doc["t_ser"] = cfg_target_serial;  // 目标打印机序列号
    doc["pip"] = cfg_printer_ip;

    String json;
    serializeJson(doc, json);
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
    const char* mqttState = mqttClient.connected() ? "Connected" : "Disconnected";

    StaticJsonDocument<300> doc;
    doc["serial"] = val_PrtSerial;       // 打印机序列号
    doc["cc"] = val_ColCopies;           // 彩色复印数
    doc["cp"] = val_ColPrints;           // 彩色打印数
    doc["ct"] = calc_ColTotal;           // 彩色总数
    doc["bc"] = calc_BWCopies;           // 黑白复印数
    doc["bp"] = calc_BWPrints;           // 黑白打印数
    doc["bt"] = calc_BWTotal;            // 黑白总数
    doc["st"] = val_SysTotal;            // 系统总打印数
    doc["msg"] = statusMessage;          // 状态消息
    doc["mqtt_state"] = mqttState;       // MQTT 状态
    doc["detectedIP"] = cfg_printer_ip;  // 打印机 IP 地址

    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  });

  // 启动 Web 服务器
  server.begin();
  Serial.println("Web 服务器已启动");
}

// --- Arduino 初始化函数 ---
void setup() {
  Serial.begin(115200);

  Serial.println("\n======================================");
  Serial.printf("固件版本: %s\n", FIRMWARE_VERSION);
  Serial.println("======================================");

  // 步骤 0: 检查并处理 OTA 自动回滚（必须在其他初始化之前）
  checkAndHandleOTARollback();

  // 步骤 1: 从非易失性存储读取配置
  preferences.begin("net_config", false);
  // 只在配置存在时才读取，避免不必要的 String 对象创建
  if (preferences.isKey("ssid")) {
    cfg_ssid = preferences.getString("ssid", "");  // WiFi SSID
  }
  if (preferences.isKey("pass")) {
    cfg_pass = preferences.getString("pass", "");  // WiFi 密码
  }
  if (preferences.isKey("pip")) {
    cfg_printer_ip = preferences.getString("pip", "");  // 打印机 IP 地址
  }
  if (preferences.isKey("t_ser")) {
    cfg_target_serial = preferences.getString("t_ser", "");  // 目标打印机序列号
  }
  preferences.end();

  // 步骤 2: 初始化网络
  initNetwork();

  // 步骤 3: 初始化 SNMP
  snmp.begin(udp);                // 启动 SNMP 管理器
  snmp.onMessage(onSNMPMessage);  // 注册 SNMP 消息回调函数

  // 步骤 4: 获取设备 MAC 地址
  deviceMAC = ETH.macAddress();
  // 检查 MAC 地址是否有效（空字符串或全零地址视为无效）
  if (deviceMAC.length() == 0 || deviceMAC == "00:00:00:00:00:00") {
    deviceMAC = WiFi.macAddress();
  }
  Serial.printf("Device MAC: %s\n", deviceMAC.c_str());

  // 步骤 5: 初始化 MQTT 主题字符串（MAC 地址确定后）
  initMQTTTopics();

  // 步骤 6: 配置 MQTT 服务器
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // 步骤 7: 初始化 Web 服务器
  initWebServer();

  // 步骤 8: 等待网络连接（最多等待10秒）
  unsigned long startWait = millis();
  while (
    !ETH.linkUp()                     // 以太网链路未建立
    && WiFi.status() != WL_CONNECTED  // WiFi 未连接
    && millis() - startWait < 10000   // 等待时间超过10秒
  ) {
    delay(100);
  }

  // 步骤 9: 判断启动模式
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
}
