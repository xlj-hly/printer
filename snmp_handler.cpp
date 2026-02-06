/*
 * snmp_handler.cpp - SNMP 处理模块实现
 * 
 * 包含 SNMP 请求发送和响应处理功能
 */

#include "snmp_handler.h"
#include "config.h"
#include "globals.h"
#include "mqtt.h"
#include <cstring>

// --- SNMP 消息回调函数 ---
// 当收到 SNMP 响应时，此函数会被调用
void onSNMPMessage(const SNMP::Message* message, const IPAddress remote, const uint16_t port) {
  static String lastInitSerial;  // 与 mqtt 侧“序列号变化才发 init”共用，扫描锁定后也更新避免 else 里再发一次
  // 获取 SNMP 响应中的变量绑定列表
  SNMP::VarBindList* varbindlist = message->getVarBindList();
  String currentSerial = "";  // 当前收到的序列号
  currentSerial.reserve(32);  // 预分配内存

  // 遍历所有变量绑定，解析每个 OID 的值
  for (unsigned int index = 0; index < varbindlist->count(); ++index) {
    SNMP::VarBind* varbind = (*varbindlist)[index];
    const char* name = varbind->getName();   // OID 名称
    SNMP::BER* value = varbind->getValue();  // OID 的值

    if (value) {
      // 处理字符串类型 (如序列号)
      if (value->getType() == SNMP::Type::OctetString) {
        const char* octetValue = static_cast<SNMP::OctetStringBER*>(value)->getValue();
        // 使用 strstr 检查 OID 是否以目标 OID 结尾（避免创建 String 对象）
        size_t nameLen = strlen(name);
        size_t oidLen = strlen(OID_PRT_SERIAL);
        if (nameLen >= oidLen && strcmp(name + nameLen - oidLen, OID_PRT_SERIAL) == 0) {
          currentSerial = octetValue;
          // 如果是锁定状态，更新序列号变量
          if (!isScanning) {
            val_PrtSerial.reserve(32);
            val_PrtSerial = octetValue;
          }
        }
      }
      // 处理整数类型 (如计数器值)
      else if (value->getType() == SNMP::Type::Integer || value->getType() == SNMP::Type::Counter32 || value->getType() == SNMP::Type::Gauge32) {

        int val = 0;
        // 根据不同的数据类型提取整数值
        if (value->getType() == SNMP::Type::Integer) {
          val = static_cast<SNMP::IntegerBER*>(value)->getValue();
        } else if (value->getType() == SNMP::Type::Counter32) {
          val = static_cast<SNMP::Counter32BER*>(value)->getValue();
        } else if (value->getType() == SNMP::Type::Gauge32) {
          val = static_cast<SNMP::Gauge32BER*>(value)->getValue();
        }

        // 只有锁定后才更新计数器 (扫描模式下不更新，避免干扰)
        if (!isScanning) {
          size_t nameLen = strlen(name);
          size_t oidLen;
          // 使用 strcmp 直接比较 OID 后缀（避免创建 String 对象）
          if ((oidLen = strlen(OID_SYS_TOTAL)) <= nameLen && strcmp(name + nameLen - oidLen, OID_SYS_TOTAL) == 0) {
            val_SysTotal = val;
          } else if ((oidLen = strlen(OID_COL_COPIES)) <= nameLen && strcmp(name + nameLen - oidLen, OID_COL_COPIES) == 0) {
            val_ColCopies = val;
          } else if ((oidLen = strlen(OID_BW_COPIES)) <= nameLen && strcmp(name + nameLen - oidLen, OID_BW_COPIES) == 0) {
            val_BWCopies = val;
          } else if ((oidLen = strlen(OID_COL_PRINTS)) <= nameLen && strcmp(name + nameLen - oidLen, OID_COL_PRINTS) == 0) {
            val_ColPrints = val;
          } else if ((oidLen = strlen(OID_BW_PRINTS)) <= nameLen && strcmp(name + nameLen - oidLen, OID_BW_PRINTS) == 0) {
            val_BWPrints = val;
          }
        }
      }
    }
  }

  // === 关键逻辑：扫描模式下的匹配 ===
  if (isScanning) {
    // 情况 1: 如果用户设定了目标序列号
    if (cfg_target_serial != "") {
      if (currentSerial == cfg_target_serial) {
        // 序列号匹配！锁定这台打印机
        val_PrtSerial = currentSerial;  // 保存序列号
        String remoteIP;
        remoteIP.reserve(16);
        remoteIP = remote.toString();
        foundPrinter(remoteIP);
        sendInitToMQTT();
        lastInitSerial = val_PrtSerial;
      } else {
        // 序列号不匹配，跳过
        IPAddress remoteIP = remote;
        Serial.printf("IP %s Serial: %s (Mismatch, skipping)\n", remoteIP.toString().c_str(), currentSerial.c_str());
      }
    }
    // 情况 2: 如果用户没设定序列号 (留空) -> 回退方案: 锁定第一台响应的打印机
    else {
      val_PrtSerial = currentSerial;
      String remoteIP;
      remoteIP.reserve(16);
      remoteIP = remote.toString();
      foundPrinter(remoteIP);
      sendInitToMQTT();
      lastInitSerial = val_PrtSerial;
    }
  } else {
    // 锁定状态：正常计算与上传数据
    // 使用直接从SNMP读取的值
    calc_BWCopies = val_BWCopies;
    calc_BWPrints = val_BWPrints;

    // 通过求和计算总数
    calc_ColTotal = val_ColPrints + val_ColCopies;  // 彩色总数 = 彩色打印 + 彩色复印
    calc_BWTotal = val_BWPrints + val_BWCopies;     // 黑白总数 = 黑白打印 + 黑白复印
    calc_TotCopies = val_ColCopies + val_BWCopies;  // 总复印数 = 彩色复印 + 黑白复印

    // 防止负数 (数据异常时的保护)
    if (calc_ColTotal < 0) calc_ColTotal = 0;
    if (calc_BWTotal < 0) calc_BWTotal = 0;
    if (calc_TotCopies < 0) calc_TotCopies = 0;
    if (calc_BWCopies < 0) calc_BWCopies = 0;
    if (calc_BWPrints < 0) calc_BWPrints = 0;

    statusMessage = "Online (SNMP OK)";

    // 只有当系统总数发生变化且大于 0 时才发送 MQTT (避免重复发送)
    if (val_SysTotal != last_sent_SysTotal && val_SysTotal > 0) {
      sendDataToMQTT();                   // 发送数据到 MQTT
      last_sent_SysTotal = val_SysTotal;  // 更新已发送的系统总数，避免重复上报
    }
    if (lastInitSerial != val_PrtSerial) {
      sendInitToMQTT();
      lastInitSerial = val_PrtSerial;
    }
  }
}

// --- 发送 SNMP 请求 ---
// 向目标 IP 发送 SNMP GetRequest 查询打印机数据
void sendSNMPRequest(IPAddress target) {
  // 创建 SNMP V1 GetRequest 消息，使用 "public" 作为社区字符串
  SNMP::Message* message = new SNMP::Message(SNMP::Version::V1, "public", SNMP::Type::GetRequest);

  // 总是读取序列号，以便进行匹配 (扫描和锁定模式都需要)
  message->add(OID_PRT_SERIAL, new SNMP::NullBER());

  // 如果不在扫描模式，才读取计数器 (减少扫描时的数据包大小，提高扫描速度)
  if (!isScanning) {
    message->add(OID_SYS_TOTAL, new SNMP::NullBER());   // 系统总打印数
    message->add(OID_COL_COPIES, new SNMP::NullBER());  // 彩色复印数
    message->add(OID_BW_COPIES, new SNMP::NullBER());   // 黑白复印数
    message->add(OID_COL_PRINTS, new SNMP::NullBER());  // 彩色打印数
    message->add(OID_BW_PRINTS, new SNMP::NullBER());   // 黑白打印数
  }

  // 发送 SNMP 请求到目标 IP 的 161 端口 (SNMP 标准端口)
  if (snmp.send(message, target, 161)) {
    lastRequestTime = millis();  // 更新最后请求时间
  }

  // 释放消息内存
  delete message;
}
