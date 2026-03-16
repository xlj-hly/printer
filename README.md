# Printer Node

基于 ESP32 的 Ricoh 打印机监控节点，通过 SNMP 读取打印机计数器，通过 MQTT 上报数据，支持 OTA 升级、Web 配置与远程锁定。

## 硬件

| 组件   | 规格                  |
| ------ | --------------------- |
| MCU    | ESP32 NodeMCU-32S     |
| 以太网 | W5500 (SPI)           |
| 网络   | 以太网优先，WiFi 备用 |

### W5500 引脚

| 引脚 | GPIO |
| ---- | ---- |
| CS   | 5    |
| SCK  | 18   |
| MISO | 19   |
| MOSI | 23   |
| IRQ  | -1   |
| RST  | -1   |

### 打印机锁定

- GPIO 22：高电平解锁，低电平锁定
- MQTT 掉线时自动锁定

## 功能

- **SNMP 监控**：读取 Ricoh 打印机序列号、彩色/黑白复印数、彩色/黑白打印数、系统总打印数
- **MQTT 上报**：数据变化时上报、状态在线/离线、遗嘱消息
- **设备发现**：未配置打印机 IP 时自动扫描网段（先 Port 9100 过滤，再 SNMP 序列号匹配）
- **看门狗**：60 秒无 SNMP 响应时检测 Port 9100，离线则重新扫描
- **OTA 更新**：支持 MQTT 单播/广播固件更新，带自检与回滚
- **Web 配置**：WiFi SSID、目标序列号、打印机 IP 配置

## 配置

1. 首次烧录后访问设备 IP（以太网或 WiFi 获取）
2. 配置 WiFi（可选，以太网优先）
3. 配置目标序列号（可选）：留空则锁定网段内第一台发现的打印机
4. 保存并重启

## MQTT 主题

| 主题                              | 方向 | 说明                                  |
| --------------------------------- | ---- | ------------------------------------- |
| `printer/{MAC}/status`            | 发送 | 在线/离线状态                         |
| `printer/{MAC}/init`              | 发送 | 初始化信息（版本、MAC、IP、序列号）   |
| `printer/{MAC}/data`              | 发送 | 打印数数据                            |
| `printer/{MAC}/lock`              | 发送 | 锁定状态                              |
| `printer/{MAC}/web`               | 发送 | Web 配置页 URL                        |
| `printer/oid/{MAC}`               | 发送 | 按需 OID 查询结果                     |
| `server/{MAC}/ota/update`         | 接收 | OTA 更新：`{"url":"http://..."}`      |
| `server/ota/broadcast/update`     | 接收 | 广播 OTA                              |
| `server/{MAC}/lock`               | 接收 | `lock` / `unlock`                     |
| `server/oid` / `server/oid/{MAC}` | 接收 | OID 查询：JSON 数组 `["oid1","oid2"]` |

## 编译与烧录

- **IDE**：Arduino IDE
- **开发板**：ESP32 Dev Module / NodeMCU-32S
- **依赖**：ESP32 核心、WiFi、ETH、Network、WebServer、SNMP、Preferences、PubSubClient、ArduinoJson、HTTPUpdate

## 文件结构

```
printer_draft/
├── printer_draft.ino   # 主程序
├── config.h           # 固件版本、MQTT、以太网引脚、SNMP OID
├── globals.h/cpp      # 全局变量
├── mqtt.h/cpp         # MQTT 连接、消息、OTA 触发
├── snmp_handler.h/cpp # SNMP 请求与响应解析
├── printer_monitor.h/cpp # 扫描、锁定、看门狗
├── ota.h/cpp          # OTA 更新、回滚、自检
└── html_content.h     # Web 配置页 HTML
```

## 固件版本

当前版本：`0.0.2`（见 `config.h`）
