/*
 * printer_monitor.cpp - 打印机扫描和监控模块实现
 * 
 * 包含打印机扫描、锁定、监控、看门狗检测等功能
 */

#include <ETH.h>
#include <WiFi.h>
#include "printer_monitor.h"
#include "config.h"
#include "globals.h"
#include "snmp_handler.h"

// --- 开始扫描打印机 ---
// 初始化扫描模式，准备扫描网段内的打印机
void startScan() {
  isScanning = true;  // 进入扫描模式
  scanCurrentIP = 1;  // 从 IP 地址最后一位 1 开始扫描

  // 根据是否配置了目标序列号，设置不同的状态消息
  statusMessage.reserve(50);
  if (cfg_target_serial != "") {
    statusMessage = "Scanning for Serial: ";
    statusMessage += cfg_target_serial;
  } else {
    statusMessage = "正在扫描打印机...";
  }
  Serial.println(statusMessage);
}

// --- 扫描循环处理 ---
// 在扫描模式下，批量检查网段内的 IP 地址
void processScanLoop() {
  // 如果不在扫描模式，直接返回
  if (!isScanning) return;

  // 获取本地 IP (以太网优先，与 networkPoll 一致)
  IPAddress local = (ETH.linkUp() && ETH.hasIP()) ? ETH.localIP() : WiFi.localIP();

  // 如果本地 IP 无效，停止扫描
  if (local[0] == 0) {
    isScanning = false;
    return;
  }

  // 构建子网前缀 (例如: 192.168.1.)
  String subnet = String(local[0]) + "." + String(local[1]) + "." + String(local[2]) + ".";

  // 批量扫描，每次处理 SCAN_BATCH_SIZE 个 IP
  for (int i = 0; i < SCAN_BATCH_SIZE; i++) {
    // 如果扫描到 255，说明整个网段扫描完毕
    if (scanCurrentIP >= 255) {
      isScanning = false;
      statusMessage = "Not Found";
      return;
    }

    // 跳过自己的 IP 地址
    if (scanCurrentIP == local[3]) {
      scanCurrentIP++;
      continue;
    }

    // 构建目标 IP 地址
    String targetIPStr = subnet + String(scanCurrentIP);

    // 步骤 1: 先用 TCP Port 9100 快速过滤 (打印机通常开放此端口)
    // 这样可以快速排除非打印机设备，减少 SNMP 请求
    WiFiClient client;
    if (client.connect(targetIPStr.c_str(), 9100, SCAN_CONNECT_TIMEOUT)) {
      client.stop();  // 关闭连接，我们只需要确认端口开放

      // 步骤 2: 发现 Port 9100 开启 -> 发送 SNMP 查询序列号
      Serial.print("Checking: ");
      Serial.println(targetIPStr);

      IPAddress targetIP;
      targetIP.fromString(targetIPStr);
      sendSNMPRequest(targetIP);  // 发送 SNMP 请求查询序列号
    }

    scanCurrentIP++;  // 移动到下一个 IP
  }
}

// --- 找到打印机后的处理 ---
// 当扫描到匹配的打印机时调用此函数
void foundPrinter(String targetIP) {
  Serial.println("🎉 Printer LOCKED: " + targetIP);

  // 将打印机 IP 保存到非易失性存储，重启后仍有效
  preferences.begin("net_config", false);
  preferences.putString("pip", targetIP);
  preferences.end();

  // 更新配置和状态
  cfg_printer_ip = targetIP;
  statusMessage = "Locked: " + targetIP;
  isScanning = false;  // 停止扫描模式

  // 立即发送一次完整的 SNMP 请求以更新所有数据
  IPAddress target;
  target.fromString(cfg_printer_ip);
  sendSNMPRequest(target);
}

// --- 检查打印机端口 9100 是否开放 ---
// 用于看门狗检测，判断打印机是否仍然在线
bool checkPort9100(String ip) {
  WiFiClient client;
  // 尝试连接端口 9100，超时时间 200 毫秒
  if (client.connect(ip.c_str(), 9100, 200)) {
    client.stop();  // 关闭连接
    return true;    // 端口开放，打印机可能在线
  }
  return false;  // 端口关闭，打印机可能离线
}

// --- 定时 SNMP 请求 ---
void printerSNMPLoop() {
  if (isScanning || cfg_printer_ip == "") return;

  // 如果已经锁定打印机 (非扫描模式)，定时发送 SNMP 请求
  // 每隔 SNMP_INTERVAL 毫秒查询一次打印机数据
  if (millis() - lastRequestTime > SNMP_INTERVAL) {
    IPAddress target;
    target.fromString(cfg_printer_ip);
    sendSNMPRequest(target);
    sendTonerRequest(target);
  }
}

// --- 打印机看门狗检测 ---
void printerWatchdog() {
  if (isScanning || cfg_printer_ip == "") return;

  static unsigned long lastSuccessTime = millis();
  unsigned long currentMillis = millis();

  // 如果状态包含 "Online"，更新成功时间
  if (String(statusMessage).indexOf("Online") >= 0) {
    lastSuccessTime = currentMillis;
  }

  // 如果超过 60 秒没有成功响应，执行看门狗检查
  if (currentMillis - lastSuccessTime > 60000) {
    // 检查打印机端口 9100 是否仍然开放
    if (checkPort9100(cfg_printer_ip)) {
      // 端口开放，但 SNMP 可能有问题
      statusMessage = "Online / SNMP Error";
      lastSuccessTime = currentMillis;
    } else {
      // 端口关闭，打印机可能离线，重新扫描
      statusMessage = "Lost connection, rescanning...";
      cfg_printer_ip = "";  // 清空 IP
      startScan();          // 重新扫描 (此时会根据保存的序列号查找)
    }
  }
}
