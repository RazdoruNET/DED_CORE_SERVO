#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <Arduino.h>

#include "app/ServoAxis.h"
#include "config/CONFIG.h"

// Контур привода живёт отдельной задачей: импульсы моста не должны зависеть от
// того, сколько времени занял разбор кадра телеметрии или переподключение WiFi.
// Задача на ядре 0 вместе с WiFi-стеком, loop() — на ядре 1.
inline void TaskServoCode(void *parameter) {
  (void) parameter;

  for (;;) {
    ServoAxis::instance().update();
    vTaskDelay(pdMS_TO_TICKS(CONTROL_TICK_MS));
  }
}

#endif // APP_TASKS_H
