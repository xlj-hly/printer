/*
 * mqtt.cpp - MQTT 通信模块实现
 * 
 * 包含 MQTT 连接、消息发送、回调处理等功能
 */

#include <ETH.h>
#include <WiFi.h>
#include <cstring>
#include <ArduinoJson.h>
#include "mqtt.h"
#include "config.h"
#include "globals.h"
#include "ota.h"

// MQTT 主题常量
static const char* MQTT_TOPIC_BROADCAST_UPDATE = "printer/ota/broadcast/update";  // 接收 | 广播更新

// --- 初始化 MQTT 主题 ---
// 在获取 MAC 地址后调用，构建所有 MQTT 主题字符串
void initMQTTTopics() {
  // 构建状态主题: printer/data/{MAC}/status | 发送 | 上报状态
  mqtt_topic_status.reserve(strlen(MQTT_TOPIC_PREFIX) + deviceMAC.length() + 8);
  mqtt_topic_status = MQTT_TOPIC_PREFIX;
  mqtt_topic_status += "/";
  mqtt_topic_status += deviceMAC;
  mqtt_topic_status += "/status";

  // 构建数据主题: printer/data/{MAC} | 发送 | 上报数据
  mqtt_topic_data.reserve(strlen(MQTT_TOPIC_PREFIX) + deviceMAC.length() + 2);
  mqtt_topic_data = MQTT_TOPIC_PREFIX;
  mqtt_topic_data += "/";
  mqtt_topic_data += deviceMAC;

  // 构建 OTA 主题: printer/data/{MAC}/ota/update | 接收 | 个人更新
  mqtt_topic_ota.reserve(strlen(MQTT_TOPIC_PREFIX) + deviceMAC.length() + 12);
  mqtt_topic_ota = MQTT_TOPIC_PREFIX;
  mqtt_topic_ota += "/";
  mqtt_topic_ota += deviceMAC;
  mqtt_topic_ota += "/ota/update";

  // 构建锁定控制主题: printer/data/{MAC}/lock | 接收 | payload: lock / unlock
  mqtt_topic_lock.reserve(strlen(MQTT_TOPIC_PREFIX) + deviceMAC.length() + 7);
  mqtt_topic_lock = MQTT_TOPIC_PREFIX;
  mqtt_topic_lock += "/";
  mqtt_topic_lock += deviceMAC;
  mqtt_topic_lock += "/lock";
}

// --- 连接 MQTT ---
void connectMQTT() {
  // 使用设备 MAC 地址生成唯一的客户端 ID（预分配内存减少碎片）
  String clientId;
  clientId.reserve(deviceMAC.length() + 6);
  clientId = "WT32-";
  clientId += deviceMAC;      // 例如: "WT32-AA:BB:CC:DD:EE:FF"
  clientId.replace(":", "");  // 移除冒号: "WT32-AABBCCDDEEFF"

  const char* willMessage = "offline";  // 遗嘱消息内容

  // 尝试连接 MQTT 服务器，并设置遗嘱
  // 参数：客户端ID, 用户名, 密码, 遗嘱主题, QoS级别, 保留标志, 遗嘱消息
  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                         mqtt_topic_status.c_str(), 1, true, willMessage)) {
    Serial.println("✅ MQTT Connected!");
    // 发送在线状态消息
    mqttClient.publish(mqtt_topic_status.c_str(), "online", true);

    // 订阅 OTA 主题
    mqttClient.subscribe(mqtt_topic_ota.c_str());
    mqttClient.subscribe(MQTT_TOPIC_BROADCAST_UPDATE);
    mqttClient.subscribe(mqtt_topic_lock.c_str());
    Serial.println("已订阅主题:");
    Serial.printf("  - %s\n", mqtt_topic_ota.c_str());
    Serial.printf("  - %s\n", MQTT_TOPIC_BROADCAST_UPDATE);
    Serial.printf("  - %s (payload: lock/unlock)\n", mqtt_topic_lock.c_str());
  }
}

// --- MQTT 连接管理循环 ---
// 负责维护 MQTT 连接，处理重连和心跳
void mqttLoop() {

  // 如果未连接，尝试重连
  if (!mqttClient.connected()) {
    static unsigned long lastMqttRetry = 0;
    // 每 5 秒尝试重连一次
    if (millis() - lastMqttRetry > 5000) {
      Serial.println("❌ MQTT 连接失败，尝试重连...");
      lastMqttRetry = millis();
      connectMQTT();
    }
  } else {
    // 已连接，处理 MQTT 消息循环
    mqttClient.loop();

    // 每 30 秒发送一次系统信息
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat > 30000) {
      lastHeartbeat = millis();

      // 获取 IP 地址
      IPAddress ip = ETH.localIP();
      if (ip == IPAddress(0, 0, 0, 0)) ip = WiFi.localIP();

      StaticJsonDocument<200> doc;  // 栈上分配, 预分配 200 字节
      doc["mac"] = deviceMAC;
      doc["version"] = FIRMWARE_VERSION;
      doc["status"] = "online";
      doc["ip"] = ip.toString();
      doc["cfg_target_serial"] = cfg_target_serial;

      String json;
      serializeJson(doc, json);
      mqttClient.publish(mqtt_topic_status.c_str(), json.c_str(), true);  // 保留消息
    }
  }
}

// --- 发送数据到 MQTT 服务器 ---
void sendDataToMQTT() {
  // 检查 MQTT 连接状态
  if (!mqttClient.connected()) return;

  StaticJsonDocument<150> doc;
  doc["mac"] = deviceMAC;
  doc["status"] = "online";
  doc["msg"] = "打印了";
  doc["st"] = val_SysTotal;

  String json;
  serializeJson(doc, json);
  Serial.printf("📤 MQTT Sent: %s\n", json.c_str());

  // 发送数据
  mqttClient.publish(mqtt_topic_data.c_str(), json.c_str());
}

// --- 更新固件 ---
void updateFirmware(const String& jsonMessage) {
  // 使用 ArduinoJson 解析 JSON（预分配 256 字节足够）
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonMessage);

  if (error) {
    Serial.printf("❌ JSON 解析失败: %s\n", error.c_str());
    Serial.printf("收到的消息: %s\n", jsonMessage.c_str());
    return;
  }

  if (!doc.containsKey("url")) {
    Serial.println("❌ JSON 中缺少 url 字段");
    return;
  }

  String url = doc["url"].as<String>();
  if (url.length() == 0) {
    Serial.println("❌ url 字段为空");
    return;
  }

  Serial.printf("📥 提取到固件 URL: %s\n", url.c_str());
  performOTAUpdate(url);
}

// --- 锁定打印机 ---
void printerLock(bool lock) {
  digitalWrite(PRINTER_LOCK_PIN, lock ? LOW : HIGH);  // 高电平解锁，低电平锁定
  Serial.println(lock ? "✅ 锁定打印机..." : "✅ 解锁打印机...");
}

// --- MQTT 消息回调 ---
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 构建消息（预分配内存减少碎片）
  String message;
  message.reserve(length);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.printf("📨 收到 MQTT 消息 [%s]: %s\n", topic, message.c_str());

  // 判断主题类型并处理
  if (strcmp(topic, mqtt_topic_ota.c_str()) == 0) {
    Serial.println("✅ OTA 个人更新主题，开始个人更新...");
    // message 是 JSON 格式: {"url":"http://192.168.14.70/firmware.bin"}
    updateFirmware(message);
  } else if (strcmp(topic, MQTT_TOPIC_BROADCAST_UPDATE) == 0) {
    Serial.println("✅ 广播更新主题，开始广播更新...");
    updateFirmware(message);
  } else if (strcmp(topic, mqtt_topic_lock.c_str()) == 0) {
    message.trim();
    if (message == "lock") {
      printerLock(true);
    } else if (message == "unlock") {
      printerLock(false);
    }
  } else {
    Serial.println("❌ 主题不匹配，忽略消息");
  }
}
