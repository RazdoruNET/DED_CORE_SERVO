#ifndef CONFIG_CONFIG_H
#define CONFIG_CONFIG_H

#include <Arduino.h>

// Настройки контура привода. Значения повторяют заводскую прошивку: привод
// приехал на них откалиброванным, менять их стоит только вместе с механикой.

// DED CORE V4 отдаёт положение сервы в 0…255 (SETTINGS_SERVO_PERCENT_MAX),
// датчик положения — 12-битный ADC (0…4095). Раньше в прошивке ядра было
// 0…100, поэтому шкала вынесена в константу и проверяется одним местом.
constexpr int SERVO_SOURCE_MAX = 255;
constexpr int POSITION_ADC_MAX = 4095;

// --- Контур -----------------------------------------------------------------

// Сколько отсчётов ADC усредняем за одно измерение.
constexpr int POSITION_SAMPLES = 5;

// Зона нечувствительности: в ней привод стоит на тормозе.
constexpr int POSITION_DEADBAND = 10;

// Дальше этого расстояния (в отсчётах ADC) привод идёт длинными импульсами.
constexpr int POSITION_COARSE = 500;

// Ближе этого расстояния импульс заканчивается торможением, а не выбегом:
// так привод не проскакивает уставку.
constexpr int POSITION_NEAR = 200;

// Делитель для коротких импульсов: 1 отсчёт ADC → 1 мс импульса.
constexpr int POSITION_FINE_DIVISOR = 100;

constexpr uint32_t PULSE_COARSE_MS = 10;
constexpr uint32_t PULSE_COAST_MS = 1;
constexpr uint32_t PULSE_HOLD_MS = 5;
constexpr uint32_t PULSE_MIN_MS = 2;
constexpr uint32_t PULSE_MAX_MS = 10;

// Контроль заклинивания: если за окно из STALL_WINDOW измерений привод
// прошёл меньше STALL_MIN_TRAVEL отсчётов, значит он не двигается.
constexpr uint8_t STALL_WINDOW = 25;
constexpr int STALL_MIN_TRAVEL = 25;
constexpr uint8_t STALL_MAX_ATTEMPTS = 3;
constexpr uint32_t STALL_RETRY_MS = 1500;

// Период задачи контура.
constexpr uint32_t CONTROL_TICK_MS = 1;

// --- Связь с ядром ----------------------------------------------------------

// Сколько ждём кадр телеметрии, прежде чем считать линк потерянным.
constexpr uint32_t LINK_TIMEOUT_MS = 3000;
constexpr uint32_t WS_RECONNECT_MS = 1000;
constexpr uint32_t WS_PING_INTERVAL_MS = 15000;
constexpr uint32_t WS_PONG_TIMEOUT_MS = 3000;
constexpr uint8_t WS_PONG_MISSES = 2;

// --- Сопряжение -------------------------------------------------------------

constexpr uint32_t WIFI_RETRY_MS = 20000;
constexpr uint32_t PAIR_SCAN_TIMEOUT_MS = 25000;
constexpr uint32_t PAIR_SCAN_INTERVAL_MS = 20000;
constexpr uint32_t PAIR_AFTER_LINK_LOSS_MS = 60000;

// Сколько занятых сетей видим в сканировании — столько и обходим.
constexpr uint16_t WIFI_SCAN_LIMIT = 40;

// --- Портал настройки -------------------------------------------------------

// После стольких неудачных стартов портал поднимается даже при сохранённых
// настройках: иначе модуль с неверным паролем ядра недоступен по сети.
constexpr uint8_t PORTAL_AFTER_FAILED_BOOTS = 3;

#endif // CONFIG_CONFIG_H
