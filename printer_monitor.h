/*
 * printer_monitor.h - 打印机扫描和监控模块
 * 
 * 包含打印机扫描、锁定、监控、看门狗检测等功能
 */

#ifndef PRINTER_MONITOR_H
#define PRINTER_MONITOR_H

#include <Arduino.h>
#include <WiFi.h>

// 开始扫描打印机
void startScan();

// 扫描循环处理
void processScanLoop();

// 找到打印机后的处理
void foundPrinter(String targetIP);

// 检查打印机端口 9100 是否开放
bool checkPort9100(String ip);

// 定时 SNMP 请求
void printerSNMPLoop();

// 打印机看门狗检测
void printerWatchdog();

#endif  // PRINTER_MONITOR_H
