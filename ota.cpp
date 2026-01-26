/*
 * ota.cpp - OTA 更新模块实现
 * 
 * 包含远程固件更新、回滚检查、自检等功能
 */

#include <ETH.h>
#include <WiFi.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include "ota.h"
#include "config.h"
#include "globals.h"

// --- 设置 OTA 验证标志位 ---
// verified: true 表示已验证，false 表示需要验证
static void setOTAVerified(bool verified) {
  preferences.begin("ota_config", false);
  preferences.putBool("ota_verified", verified);
  preferences.end();
}

// --- 硬件自检 ---
static bool hardwareSelfCheck() {
  // 检查1: 基本硬件初始化（Serial 已初始化）
  // 检查2: 网络接口可用性
  // 不将网络未连接视为致命错误
  if (ETH.linkUp()) {
    Serial.println("✅ 以太网连接正常");
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi 连接正常");
  } else {
    // 网络未连接，但这不是致命错误（可能还在初始化中）
    Serial.println("⚠️ 网络未连接（可能正在初始化）");
  }

  // 检查3: 内存检查（简单检查）
  if (ESP.getFreeHeap() < 50000) {
    Serial.printf("⚠️  可用内存较低: %d bytes\n", ESP.getFreeHeap());
  } else {
    Serial.printf("✅ 内存正常: %d bytes\n", ESP.getFreeHeap());
  }
  // 检查4: 分区表有效性
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    Serial.println("❌ 无法获取运行分区信息");
    return false;
  }
  Serial.printf("✅ 分区信息正常: %s\n", running->label);

  // 如果所有基本检查通过，返回 true
  Serial.println("✅ 固件自检通过");
  return true;
}

// --- 执行固件自检 ---
// 检查新固件是否正常工作，用于自动回滚机制
bool performSelfCheck() {
  Serial.println("🔍 执行固件自检...");

  // 检查1: 硬件自检
  if (!hardwareSelfCheck()) {
    return false;  // 硬件自检失败，直接返回
  }

  // 检查2: 其他检查（可扩展）
  // TODO: 后续可在此添加其他检查项
  // if (!otherCheck()) {
  //   return false;
  // }

  // 所有检查通过
  return true;
}

// --- 检查并处理 OTA 自动回滚 ---
// 逻辑：标志位控制是否需要自检，分区状态用于回滚机制
// - 标志位false → 强制自检（无论分区状态如何）
// - 标志位true → 跳过自检
// - 自检通过 → 如果分区状态是PENDING_VERIFY/NEW，调用esp_ota_mark_app_valid_cancel_rollback() + 设置标志位true
// - 自检失败 → 调用esp_ota_mark_app_invalid_rollback_and_reboot() + 设置标志位true
void checkAndHandleOTARollback() {
  Serial.println("🔄 OTA 回滚检查");

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    Serial.println("❌ 无法获取分区信息");
    return;
  }
  Serial.printf("当前分区: %s\n", running->label);
  Serial.println("======================================");

  // 读取 OTA 验证标志位
  preferences.begin("ota_config", false);
  bool ota_verified = preferences.getBool("ota_verified", true);
  preferences.end();

  if (ota_verified) {
    Serial.println("✅ 标志位: 已验证，跳过");
    return;
  }

  Serial.println("🕒 标志位: 需要验证");

  esp_ota_img_states_t ota_state;
  esp_err_t err = esp_ota_get_state_partition(running, &ota_state);

  if (err != ESP_OK) {
    Serial.println("❌ 无法获取分区状态");
    // 无法获取状态，可能是旧版本固件或未启用回滚
    return;
  }

  switch (ota_state) {
    case ESP_OTA_IMG_VALID:
      Serial.println("✅ 分区状态: VALID");
      Serial.println("✅ 固件已验证");
      break;
    case ESP_OTA_IMG_INVALID:
      Serial.println("❌ 分区状态: INVALID");
      Serial.println("❌ 固件已验证失败");
      break;

    case ESP_OTA_IMG_ABORTED:
      Serial.println("⏹️ 分区状态: ABORTED");
      Serial.println("⏹️ 固件已中止");
      break;

    case ESP_OTA_IMG_NEW:
      Serial.println("ℹ️ 分区状态: NEW");
      Serial.println("ℹ️ 固件是新固件");
      break;

    case ESP_OTA_IMG_PENDING_VERIFY:
      Serial.println("🔔 分区状态: PENDING_VERIFY");
      Serial.println("🔔 这里为系统标志位");
      break;
    default:
      Serial.println("❓分区状态: UNKNOWN");
      Serial.println("❓ 固件状态未知");
      break;
  }

  Serial.println("▶️ 开始自检...");
  if (performSelfCheck()) {
    esp_ota_mark_app_valid_cancel_rollback();  // 确认应用运行成功
    Serial.println("🔄 自检通过, 清除标志位");
    setOTAVerified(true);  // 设置标志位为 true，下次启动跳过检查
  } else {
    Serial.println("❌ 自检失败，触发回滚");
    esp_ota_mark_app_invalid_rollback_and_reboot();  // 回滚到上一个固件
    Serial.println("⏳ 回滚成功，准备清除标志位, 重启设备...");
    setOTAVerified(true);  // 设置标志位为 true，避免无限循环
    delay(2000);
    ESP.restart();  // 重启设备，加载回滚后的固件
  }
}

// --- 打印分区信息 ---
void printPartitionInfo() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);

  Serial.println("--- 分区信息 ---");
  if (running) {
    Serial.printf("当前运行分区: %s (偏移: 0x%08X, 大小: %d KB)\n",
                  running->label, running->address, running->size / 1024);
  }
  if (update_partition) {
    Serial.printf("目标更新分区: %s (偏移: 0x%08X, 大小: %d KB)\n",
                  update_partition->label, update_partition->address, update_partition->size / 1024);
    Serial.printf("可用空间: %d KB\n", update_partition->size / 1024);
  } else {
    Serial.println("⚠️ 警告: 找不到可用的 OTA 分区！");
  }
  Serial.println("---------------");
}

// --- 远程 OTA 更新函数 ---
void performOTAUpdate(const String& url) {
  Serial.println("🚀 开始 OTA 更新");
  Serial.println("======================================");
  Serial.printf("固件 URL: %s\n", url.c_str());
  Serial.printf("当前固件版本: %s\n", FIRMWARE_VERSION);

  // 打印分区信息
  printPartitionInfo();

  // 使用 WiFiClient（在 ESP32 中，WiFiClient 也支持以太网连接）
  WiFiClient client;

  // 设置 OTA 更新回调，显示详细进度
  httpUpdate.onStart([]() {
    Serial.println("\n📥 OTA 更新开始");
  });

  httpUpdate.onProgress([](int cur, int total) {
    Serial.printf("📊 OTA 进度: %d%% (%d/%d bytes)\n", (cur * 100) / total, cur, total);
  });

  httpUpdate.onEnd([]() {
    Serial.println("✅ 固件下载完成, 准备重启设备...");

    // 清除 OTA 验证标志位，确保新固件首次启动时执行回滚检查
    setOTAVerified(false);
    Serial.println("🔄 已清除 OTA 验证标志位，新固件启动时将执行验证");
  });

  httpUpdate.onError([](int err) {
    Serial.printf("❌ OTA 更新错误代码: %d\n", err);
  });

  // 执行 OTA 更新
  Serial.println("\n📡 正在连接服务器...");
  httpUpdate.update(client, url);
}
