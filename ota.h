/*
 * ota.h - OTA 更新模块
 * 
 * 包含远程固件更新、回滚检查、自检等功能
 */

#ifndef OTA_H
#define OTA_H

#include <Arduino.h>

// 执行远程 OTA 更新
void performOTAUpdate(const String& url);

// 打印分区信息
void printPartitionInfo();

// 检查并处理 OTA 自动回滚
void checkAndHandleOTARollback();

// 执行固件自检
bool performSelfCheck();

#endif  // OTA_H
