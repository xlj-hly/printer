/*
 * mqtt.h - MQTT 通信模块
 * 
 * 包含 MQTT 连接、消息发送、回调处理等功能
 */

#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>

// 初始化 MQTT 主题字符串（在获取 MAC 地址后调用）
void initMQTTTopics();

// 连接 MQTT 服务器
void connectMQTT();

// MQTT 连接管理循环（重连、心跳）
void mqttLoop();

// 发送 init 到 MQTT（获取到 val_PrtSerial 后调用）
void sendInitToMQTT();

// 发送数据到 MQTT 服务器
void sendDataToMQTT();

// 发送锁定状态到 MQTT（内部由 setPrinterLockPin 调用）
void sendLockStateToMQTT();

// MQTT 消息回调函数
void mqttCallback(char* topic, byte* payload, unsigned int length);

#endif  // MQTT_H
