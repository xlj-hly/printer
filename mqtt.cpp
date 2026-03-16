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
#include "snmp_handler.h"

// MQTT 主题常量
static const char* MQTT_TOPIC_BROADCAST_UPDATE = "server/ota/broadcast/update";  // 接收 | 广播更新

// --- 初始化 MQTT 主题 ---
// 在获取 MAC 地址后调用，构建所有 MQTT 主题字符串
void initMQTTTopics() {
  // 构建状态主题: printer/{MAC}/status | 发送 | 上报状态
  mqtt_topic_status.reserve(8 + deviceMAC.length() + 8);
  mqtt_topic_status = "printer/";
  mqtt_topic_status += deviceMAC;
  mqtt_topic_status += "/status";

  // 构建初始化主题: printer/{MAC}/init | 发送 | 初始化发一次
  mqtt_topic_init.reserve(8 + deviceMAC.length() + 6);
  mqtt_topic_init = "printer/";
  mqtt_topic_init += deviceMAC;
  mqtt_topic_init += "/init";

  // 构建数据主题: printer/{MAC}/data | 发送 | 上报数据
  mqtt_topic_data.reserve(8 + deviceMAC.length() + 7);
  mqtt_topic_data = "printer/";
  mqtt_topic_data += deviceMAC;
  mqtt_topic_data += "/data";

  // 构建 OTA 主题: server/{MAC}/ota/update | 接收 | 个人更新
  mqtt_topic_ota.reserve(8 + deviceMAC.length() + 12);
  mqtt_topic_ota = "server/";
  mqtt_topic_ota += deviceMAC;
  mqtt_topic_ota += "/ota/update";

  // 构建锁定控制主题: server/{MAC}/lock | 接收 | payload: lock / unlock
  mqtt_topic_lock.reserve(8 + deviceMAC.length() + 6);
  mqtt_topic_lock = "server/";
  mqtt_topic_lock += deviceMAC;
  mqtt_topic_lock += "/lock";

  // 构建锁定状态主题: printer/{MAC}/lock | 发送 | payload: lock/unlock
  mqtt_topic_lock_state.reserve(8 + deviceMAC.length() + 6);
  mqtt_topic_lock_state = "printer/";
  mqtt_topic_lock_state += deviceMAC;
  mqtt_topic_lock_state += "/lock";

  // 接收 OID 请求: server/oid/{MAC}
  mqtt_topic_oid_mac.reserve(11 + deviceMAC.length());
  mqtt_topic_oid_mac = "server/oid/";
  mqtt_topic_oid_mac += deviceMAC;

  // 发送 OID 结果: printer/oid/{MAC}
  mqtt_topic_server_oid_mac.reserve(11 + deviceMAC.length());
  mqtt_topic_server_oid_mac = "printer/oid/";
  mqtt_topic_server_oid_mac += deviceMAC;

  mqtt_topic_web.reserve(8 + deviceMAC.length() + 5);
  mqtt_topic_web = "printer/";
  mqtt_topic_web += deviceMAC;
  mqtt_topic_web += "/web";
}

// --- 连接 MQTT ---
void connectMQTT() {
  // 使用设备 MAC 地址生成唯一的客户端 ID（预分配内存减少碎片）
  String clientId;
  clientId.reserve(deviceMAC.length() + 6);
  clientId = "c-";
  clientId += deviceMAC;  // 例如: "c-AA:BB:CC:DD:EE:FF"
  // clientId.replace(":", "");  // 移除冒号: "c-AABBCCDDEEFF"

  const char* willMessage = "offline";  // 遗嘱消息内容

  // 尝试连接 MQTT 服务器，并设置遗嘱
  // 参数：客户端ID, 用户名, 密码, 遗嘱主题, QoS级别, 保留标志, 遗嘱消息
  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                         mqtt_topic_status.c_str(), 1, true, willMessage)) {
    Serial.println("✅ MQTT Connected!");
    mqttClient.publish(mqtt_topic_status.c_str(), "online", true);

    String webUrl = (ETH.linkUp() && ETH.hasIP()) ? "http://" + ETH.localIP().toString()
                                                  : "http://" + WiFi.localIP().toString();
    if (webUrl.length() > 7) mqttClient.publish(mqtt_topic_web.c_str(), webUrl.c_str(), true);

    // 订阅 OTA 主题
    mqttClient.subscribe(mqtt_topic_ota.c_str());
    mqttClient.subscribe(MQTT_TOPIC_BROADCAST_UPDATE);
    mqttClient.subscribe(mqtt_topic_lock.c_str());
    mqttClient.subscribe(MQTT_TOPIC_OID);
    mqttClient.subscribe(mqtt_topic_oid_mac.c_str());
  }
}

// --- MQTT 连接管理循环 ---
// 负责维护 MQTT 连接、重连；用状态变化检测掉线
void mqttLoop() {
  static bool wasConnected = false;
  bool nowConnected = mqttClient.connected();

  if (wasConnected && !nowConnected) {
    // TODO: 掉线逻辑
    Serial.println("⚠️ MQTT 已断开");
    setPrinterLockPin(LOW);
    wasConnected = false;
  }
  wasConnected = nowConnected;  // 保存当前状态

  if (!nowConnected) {
    static unsigned long lastMqttRetry = 0;
    if (millis() - lastMqttRetry > 5000) {
      lastMqttRetry = millis();
      connectMQTT();
    }
  } else {
    mqttClient.loop();
  }
}

// --- 发送 init 到 MQTT（获取到 val_PrtSerial 后调用）---
void sendInitToMQTT() {
  if (!mqttClient.connected() || val_PrtSerial.length() == 0) return;

  StaticJsonDocument<160> doc;
  doc["version"] = FIRMWARE_VERSION;
  doc["mac"] = deviceMAC;
  doc["ip"] = deviceIP;
  doc["serial"] = val_PrtSerial;
  String json;
  serializeJson(doc, json);
  mqttClient.publish(mqtt_topic_init.c_str(), json.c_str());
}

// --- 发送数据到 MQTT 服务器 ---
void sendDataToMQTT() {
  // 检查 MQTT 连接状态
  if (!mqttClient.connected()) return;

  StaticJsonDocument<180> doc;
  doc["mac"] = deviceMAC;
  doc["st"] = val_SysTotal;           // 系统总打印数
  doc["serial"] = val_PrtSerial;      // 打印机序列号
  doc["col_copies"] = val_ColCopies;  // 彩色复印数
  doc["bw_copies"] = val_BWCopies;    // 黑白复印数
  doc["col_prints"] = val_ColPrints;  // 彩色打印数
  doc["bw_prints"] = val_BWPrints;    // 黑白打印数

  String json;
  serializeJson(doc, json);
  Serial.printf("📤 MQTT Sent: %s\n", json.c_str());

  // 发送数据
  mqttClient.publish(mqtt_topic_data.c_str(), json.c_str());
}

// --- 发送锁定状态到 MQTT ---
void sendLockStateToMQTT() {
  if (!mqttClient.connected()) return;
  mqttClient.publish(mqtt_topic_lock_state.c_str(), printerLockPinState.c_str());
}

// --- 发送 OID 查询结果到 printer/oid/{MAC} ---
void publishOidResult(const String& json) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(mqtt_topic_server_oid_mac.c_str(), json.c_str());
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
  setPrinterLockPin(lock ? LOW : HIGH);  // 高电平解锁，低电平锁定
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
  } else if (strcmp(topic, MQTT_TOPIC_OID) == 0 || strcmp(topic, mqtt_topic_oid_mac.c_str()) == 0) {
    if (cfg_printer_ip.length() > 0) {
      IPAddress target;
      target.fromString(cfg_printer_ip);
      sendSNMPOidRequest(target, message);
    }
  } else {
    Serial.println("❌ 主题不匹配，忽略消息");
  }
}
