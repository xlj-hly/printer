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
#define FIRMWARE_VERSION "0.0.11"

// ==========================================
//          配置区 (硬编码参数)
// ==========================================
// MQTT 服务器配置
#define MQTT_BROKER "192.168.14.70"       // MQTT 服务器 IP 地址
#define MQTT_PORT 1883                    // MQTT 端口
#define MQTT_USER "admin"                 // MQTT 用户名
#define MQTT_PASS "admin123"              // MQTT 密码
#define MQTT_TOPIC_PREFIX "printer/data"  // MQTT 主题前缀

// --- WT32-ETH01 以太网引脚配置 ---
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN  // 以太网时钟模式
#define ETH_POWER_PIN 16                 // 以太网电源控制引脚
#define ETH_TYPE ETH_PHY_LAN8720         // 以太网 PHY 芯片类型
#define ETH_ADDR 1                       // 以太网 PHY 地址
#define ETH_MDC_PIN 23                   // MDC 引脚 (管理数据时钟)
#define ETH_MDIO_PIN 18                  // MDIO 引脚 (管理数据输入输出)

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
#define PRINTER_LOCK_PIN 22                 // 打印机锁定引脚 (高电平解锁)

#endif  // CONFIG_H
