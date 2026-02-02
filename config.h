/*
 * config.h - 系统配置常量
 * 
 * 包含固件版本、MQTT 配置、以太网引脚、系统参数、SNMP OID 等常量定义
 * 使用 #define 宏避免多重定义问题
 */

#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
//          固件版本
// ==========================================
#define FIRMWARE_VERSION "0.0.19"

// ==========================================
//          配置区
// ==========================================
// MQTT 服务器配置
#define MQTT_BROKER "192.168.14.70"       // MQTT 服务器 IP 地址
#define MQTT_PORT 1883                    // MQTT 端口
#define MQTT_USER "admin"                 // MQTT 用户名
#define MQTT_PASS "admin123"              // MQTT 密码
#define MQTT_TOPIC_PREFIX "printer/data"  // MQTT 主题前缀

// --- NodeMCU-32S + W5500 (SPI) 以太网引脚配置 ---
#define ETH_PHY_TYPE ETH_PHY_W5500  // 以太网 PHY 芯片类型 (W5500)
#define ETH_PHY_ADDR 1              // W5500 片选地址
#define ETH_PHY_CS 5                // SPI 片选引脚
#define ETH_PHY_IRQ -1              // 中断引脚 (-1 表示未接)
#define ETH_PHY_RST -1              // 复位引脚 (-1 表示未接)
#define ETH_SPI_SCK 18              // SPI 时钟
#define ETH_SPI_MISO 19             // SPI 主机入从机出
#define ETH_SPI_MOSI 23             // SPI 主机出从机入

// --- Network 双网优先级轮询 ---
#define NET_POLL_MS 2000    // 轮询间隔 (毫秒)，用于切换默认出口
#define NET_DEFAULT_ETH 0   // 当前出口：以太网
#define NET_DEFAULT_WIFI 1  // 当前出口：WiFi
#define NET_DEFAULT_NONE 2  // 当前无网

// --- 系统参数配置 ---
#define SNMP_INTERVAL 5000       // SNMP 查询间隔 (毫秒)
#define SCAN_CONNECT_TIMEOUT 50  // 扫描连接超时时间 (毫秒)
#define SCAN_BATCH_SIZE 10       // 每次扫描的 IP 数量批次大小

// --- Ricoh 打印机 SNMP OID (对象标识符) ---
// 这些 OID 用于从 Ricoh 打印机获取不同的数据
#define OID_PRT_SERIAL "1.3.6.1.2.1.43.5.1.1.17.1"             // 打印机序列号
#define OID_SYS_TOTAL "1.3.6.1.2.1.43.10.2.1.4.1.1"            // 系统总打印数
#define OID_COL_COPIES "1.3.6.1.4.1.367.3.2.1.2.19.5.1.9.138"  // 彩色复印数
#define OID_BW_COPIES "1.3.6.1.4.1.367.3.2.1.2.19.5.1.9.139"   // 黑白复印数
#define OID_COL_PRINTS "1.3.6.1.4.1.367.3.2.1.2.19.5.1.9.142"  // 彩色打印数
#define OID_BW_PRINTS "1.3.6.1.4.1.367.3.2.1.2.19.5.1.9.143"   // 黑白打印数

// --- 打印机锁定 ---
#define PRINTER_LOCK_PIN 22  // 打印机锁定引脚 (高电平解锁)

#endif  // CONFIG_H
