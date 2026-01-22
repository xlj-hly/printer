/*
 * snmp_handler.h - SNMP 处理模块
 * 
 * 包含 SNMP 请求发送和响应处理功能
 */

#ifndef SNMP_HANDLER_H
#define SNMP_HANDLER_H

#include <Arduino.h>
#include <SNMP.h>

// SNMP 消息回调函数
void onSNMPMessage(const SNMP::Message* message, const IPAddress remote, const uint16_t port);

// 发送 SNMP 请求
void sendSNMPRequest(IPAddress target);

// 找到打印机后的处理
void foundPrinter(String targetIP);

#endif  // SNMP_HANDLER_H
