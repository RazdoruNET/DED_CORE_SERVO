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

  // Пропуск доверенного модуля: по нему ядро присылает смену параметров сети.
  String token;

  // BSSID точки сопряжения: по нему автоматическое пересопряжение отличает
  // своё ядро от чужой открытой DED_PAIR_*.
  String bssid;

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

  // expectedBssid: если задан, подходит только точка сопряжения с этим BSSID.
  // Пустая строка — любая сеть DED_PAIR_* (первое сопряжение или явный запуск
  // из портала, когда ядро заменили).
  bool pair(CoreCredentials &out,
            const String &expectedBssid = String(),
            uint32_t timeoutMs = PAIR_SCAN_TIMEOUT_MS);

private:
  PairingLink() = default;
};

#endif // NET_PAIRING_LINK_H
