/*
 * led_indicator.h - LED 注册/锁机状态指示灯
 *
 * 状态：MQTT 未连接=单次闪烁，未注册=灭，已注册未锁机=常亮，已注册已锁机=双次闪烁
 */

#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

void ledIndicatorLoop();

#endif
