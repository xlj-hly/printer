/*
 * mqtt.cpp - MQTT 通信模块实现
 * 
 * 包含 MQTT 连接、消息发送、回调处理等功能
 */

#include <ETH.h>
#include <WiFi.h>
#include "mqtt.h"
#include "config.h"
#include "globals.h"
#include "ota.h"

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
