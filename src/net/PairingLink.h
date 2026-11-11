#ifndef NET_PAIRING_LINK_H
#define NET_PAIRING_LINK_H

#include <Arduino.h>

#include "config/CONFIG.h"

// Параметры ядра, полученные при сопряжении.
struct CoreCredentials {
  String ssid;
  String password;
  String host;
  uint16_t port = 0;

  bool valid() const;
};

// Сопряжение с ядром через его сеть сопряжения.
//
// Пароль точки доступа DED CORE V4 уникален для блока и не отдаётся ни по HTTP
// без ключа устройства, ни в кадре телеметрии. Поэтому ядро по команде из
// приложения на минуту поднимает открытую сеть DED_PAIR_* с маршрутом
// GET /module/pair.
// Модуль находит эту сеть, забирает параметры и дальше работает как обычный
// клиент основной точки доступа.
class PairingLink {
public:
  static PairingLink &instance();

  bool pair(CoreCredentials &out, uint32_t timeoutMs = PAIR_SCAN_TIMEOUT_MS);

private:
  PairingLink() = default;
};

#endif // NET_PAIRING_LINK_H
