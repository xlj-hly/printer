/*
 * led_indicator.cpp - LED 注册/锁机状态指示灯
 *
 * 单次闪烁：匀速闪烁，每次闪一下，间隔一致。例：亮 80ms-灭 420ms，周期 500ms
 * 双次闪烁：每周期闪两下，周期间隔一致。例：亮 80ms-灭 80ms-亮 80ms-灭 760ms，周期 1000ms
 */

#include "led_indicator.h"
#include "config.h"
#include "globals.h"
#include <PubSubClient.h>

enum LedState {
  LED_OFF,           // 未注册
  LED_ON,            // 已注册且解锁
  LED_SINGLE_BLINK,  // MQTT 未连接：每次闪一下
  LED_DOUBLE_BLINK,  // 已注册且锁定：每周期闪两下
};

static LedState getLedState() {
  if (!mqttClient.connected()) return LED_SINGLE_BLINK;
  if (!isRegistered) return LED_OFF;
  return printerLockPinState == "lock" ? LED_DOUBLE_BLINK : LED_ON;
}

void ledIndicatorLoop() {
  unsigned long now = millis();
  LedState state = getLedState();

  if (state == LED_OFF) {
    digitalWrite(LED_TEST_PIN, LOW);
    return;
  }
  if (state == LED_ON) {
    digitalWrite(LED_TEST_PIN, HIGH);
    return;
  }

  if (state == LED_SINGLE_BLINK) {
    // 单闪：500ms 周期，前 80ms 亮（闪一下），后 420ms 灭（间隔）
    unsigned long cycle = now % 500;
    digitalWrite(LED_TEST_PIN, cycle < 80 ? HIGH : LOW);
    return;
  }

  if (state == LED_DOUBLE_BLINK) {
    // 双闪：1000ms 周期，0-80ms 亮、80-160ms 灭、160-240ms 亮（闪两下），240-1000ms 灭（周期间隔）
    unsigned long cycle = now % 1000;
    digitalWrite(LED_TEST_PIN, (cycle < 80 || (cycle >= 160 && cycle < 240)) ? HIGH : LOW);
    return;
  }
}
