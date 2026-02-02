/*
 * globals.cpp - 全局变量定义
 * 
 * 实际定义全局变量和对象实例
 */

#include "globals.h"

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
int val_ColCopies = 0;      // 彩色复印数
int val_BWCopies = 0;       // 黑白复印数
int val_ColPrints = 0;      // 彩色打印数
int val_BWPrints = 0;       // 黑白打印数
String val_PrtSerial = "";  // 打印机序列号

// --- 计算得出的数值 ---
int calc_ColTotal = 0;   // 彩色总打印数 = 彩色打印 + 彩色复印
int calc_BWTotal = 0;    // 黑白总打印数 = 黑白打印 + 黑白复印
int calc_TotCopies = 0;  // 总复印数 = 彩色复印 + 黑白复印
int calc_BWCopies = 0;   // 黑白复印数 (从SNMP直接读取)
int calc_BWPrints = 0;   // 黑白打印数 (从SNMP直接读取)

// --- MQTT 发送控制 ---
int last_sent_SysTotal = -1;  // 上次发送的系统总数，用于检测变化

// --- MQTT 主题字符串（运行时不变，连接时构建） ---
String mqtt_topic_status = "";  // printer/data/{MAC}/status
String mqtt_topic_data = "";    // printer/data/{MAC}
String mqtt_topic_ota = "";     // printer/data/{MAC}/ota/update
String mqtt_topic_lock = "";    // printer/data/{MAC}/lock，payload: lock/unlock
