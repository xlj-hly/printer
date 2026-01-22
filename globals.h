/*
 * globals.h - 全局变量声明
 * 
 * 使用 extern 声明全局变量，实际定义在 globals.cpp 中
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <SNMP.h>
#include <PubSubClient.h>

// --- 全局对象实例 ---
extern WebServer server;         // Web 服务器，端口 80
extern Preferences preferences;  // 非易失性存储，用于保存配置
extern WiFiUDP udp;              // UDP 套接字，用于 SNMP 通信
extern SNMP::Manager snmp;       // SNMP 管理器
extern WiFiClient espClient;     // WiFi 客户端，用于 MQTT 连接
extern PubSubClient mqttClient;  // MQTT 客户端

// --- 配置参数 (从 Preferences 读取) ---
extern String cfg_ssid;           // WiFi SSID
extern String cfg_pass;           // WiFi 密码
extern String cfg_printer_ip;     // 打印机 IP 地址
extern String cfg_target_serial;  // 目标打印机序列号 (用于精确搜索)

// --- 系统状态变量 ---
extern String statusMessage;           // 当前状态消息
extern String deviceMAC;               // 设备 MAC 地址
extern unsigned long lastRequestTime;  // 上次 SNMP 请求时间
extern bool isScanning;                // 是否正在扫描模式
extern int scanCurrentIP;              // 当前扫描的 IP 地址 (最后一位)

// --- SNMP 读取的原始数值 ---
extern int val_SysTotal;      // 系统总打印数 (黑白 + 彩色)
extern int val_ColCopies;     // 彩色复印数
extern int val_BWCopies;      // 黑白复印数
extern int val_ColPrints;     // 彩色打印数
extern int val_BWPrints;      // 黑白打印数
extern String val_PrtSerial;  // 打印机序列号

// --- 计算得出的数值 ---
extern int calc_ColTotal;   // 彩色总打印数 = 彩色打印 + 彩色复印
extern int calc_BWTotal;    // 黑白总打印数 = 黑白打印 + 黑白复印
extern int calc_TotCopies;  // 总复印数 = 彩色复印 + 黑白复印
extern int calc_BWCopies;   // 黑白复印数 (从SNMP直接读取)
extern int calc_BWPrints;   // 黑白打印数 (从SNMP直接读取)

// --- MQTT 发送控制 ---
extern int last_sent_SysTotal;  // 上次发送的系统总数，用于检测变化

#endif  // GLOBALS_H
