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

// Делитель для коротких импульсов: 100 отсчётов ADC дают 1 мс импульса
// (10 отсчётов — 0,1 мс, но импульс короче PULSE_MIN_MS не выдаётся).
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

// --- Калибровка крайних положений -------------------------------------------

// Первое включение: привод сам находит упоры, чтобы шкала ядра 0…255 легла на
// реальный ход, а не на весь диапазон датчика 0…4095.
//
// Упор — это окно измерений, в котором привод не сдвинулся. Сравниваем положение
// в начале и в конце окна, а не сумму приращений: шум датчика в сумме выглядит
// как движение и упор не находится.
constexpr int CAL_STALL_MIN_TRAVEL = 8;
constexpr uint8_t CAL_STALL_WINDOW = 15;

// Дольше этого упор не ищем: значит механика заела или датчик не отвечает.
// Ограничение важно и физически: всё это время привод давит в упор.
constexpr uint32_t CAL_MOVE_TIMEOUT_MS = 6000;

// В начале ноги упор не ищем: со упора привод трогается не мгновенно, и первые
// отсчёты показывают почти нулевой ход.
constexpr uint32_t CAL_LEG_MIN_MS = 400;

// Ход меньше этого — калибровка не удалась (нет механики, датчик не подключён).
constexpr int CAL_MIN_SPAN = 300;

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
// Общий бюджет одного поиска сети сопряжения: скан плюс перебор кандидатов.
// Больше нельзя — на это время loop() не обслуживает ни линк, ни портал.
constexpr uint32_t PAIR_SCAN_TIMEOUT_MS = 25000;
constexpr uint32_t PAIR_SCAN_INTERVAL_MS = 20000;
constexpr uint32_t PAIR_AFTER_LINK_LOSS_MS = 60000;

// Сколько сетей DED_PAIR_* перебираем за одну попытку и сколько ждём каждую.
constexpr int PAIR_MAX_CANDIDATES = 16;
constexpr uint32_t PAIR_CANDIDATE_TIMEOUT_MS = 8000;

// --- Портал настройки -------------------------------------------------------

// После стольких неудачных стартов портал поднимается даже при сохранённых
// настройках: иначе модуль с неверным паролем ядра недоступен по сети.
// Порог намеренно высокий: каждый запуск без связи — это ещё один старт, и
// случайные перезагрузки не должны открывать точку доступа.
constexpr uint8_t PORTAL_AFTER_FAILED_BOOTS = 10;

#endif // CONFIG_CONFIG_H
