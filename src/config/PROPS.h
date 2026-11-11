#ifndef CONFIG_PROPS_H
#define CONFIG_PROPS_H

#include <Arduino.h>

// Версия прошивки модуля. Приходит в портал настройки и в кадр ошибки ядру.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

// Имя точки доступа портала настройки. Портал поднимается, когда модуль не
// настроен или потерял ядро, и служит только для ввода настроек и обновления.
constexpr const char *PORTAL_AP_SSID = "DED_SERVO_DRIVER";

// Ядро поднимает сеть сопряжения с этим префиксом: "DED_PAIR_" + хвост MAC.
// Сеть открытая и живёт, пока на блоке нажата кнопка (см. DED CORE V4,
// core/net/MODULE_PAIRING.h).
constexpr const char *PAIR_AP_PREFIX = "DED_PAIR_";

// Адрес ядра в его собственной сети и порт WebSocket.
constexpr const char *CORE_DEFAULT_HOST = "192.168.4.1";
constexpr uint16_t CORE_DEFAULT_PORT = 81;

#endif // CONFIG_PROPS_H
