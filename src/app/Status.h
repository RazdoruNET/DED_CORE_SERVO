#ifndef APP_STATUS_H
#define APP_STATUS_H

#include <Arduino.h>

// Состояние модуля для портала настройки и для кадра ошибки ядру.
// Пишут его контур привода и сетевой слой, читает портал (задача AsyncTCP).
// Только POD-поля: строки здесь нельзя, портал читал бы их из другой задачи.
struct RuntimeStatus {
  bool linkOnline = false;

  int servoPercent = 0;
  int targetAdc = 0;
  int positionAdc = 0;

  // Код последней ошибки привода (см. app/ServoAxis.h, FaultCode).
  uint8_t fault = 0;
  uint8_t faultAttempts = 0;

  unsigned long frames = 0;
  unsigned long lastFrameMs = 0;
};

extern RuntimeStatus runtime;

#endif // APP_STATUS_H
