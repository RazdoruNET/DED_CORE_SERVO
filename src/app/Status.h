#ifndef APP_STATUS_H
#define APP_STATUS_H

#include <Arduino.h>

// Состояние модуля для портала настройки и для кадра ошибки ядру.
// Пишут его контур привода и сетевой слой, читает портал.

struct RuntimeStatus {
  bool linkOnline = false;
  bool coreProvisioned = false;

  String coreSsid;
  String coreHost;

  int servoPercent = 0;
  int targetAdc = 0;
  int positionAdc = 0;

  // Код последней ошибки привода (см. app/ServoAxis.h, FaultCode).
  uint8_t fault = 0;
  uint8_t faultAttempts = 0;
  unsigned long faultsTotal = 0;

  unsigned long frames = 0;
  unsigned long lastFrameMs = 0;
};

extern RuntimeStatus runtime;

#endif // APP_STATUS_H
