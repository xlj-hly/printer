/*
 * mqtt.h - MQTT 通信模块
 * 
 * 包含 MQTT 连接、消息发送、回调处理等功能
 */

#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>

// 连接 MQTT 服务器
void connectMQTT();

// MQTT 连接管理循环（重连、心跳）
void mqttLoop();

// 发送数据到 MQTT 服务器
void sendDataToMQTT();

// MQTT 消息回调函数
void mqttCallback(char* topic, byte* payload, unsigned int length);

#endif  // MQTT_H
