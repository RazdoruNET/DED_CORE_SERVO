#include "app/ServoAxis.h"

#include <esp_timer.h>

#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PINS.h"
#include "net/Settings.h"

namespace {

// Одноразовый таймер на импульс. Создаётся в begin(); если создать не удалось,
// длительность импульса считается по тикам, как раньше.
esp_timer_handle_t g_pulseTimer = nullptr;

}  // namespace

ServoAxis &ServoAxis::instance() {
  static ServoAxis axis;
  return axis;
}

void ServoAxis::begin() {
  pinMode(PIN_MOTOR_UP, OUTPUT);
  pinMode(PIN_MOTOR_DOWN, OUTPUT);
  pinMode(PIN_DRIVER_ENABLE, OUTPUT);
  pinMode(PIN_SPARE_INPUT, INPUT);

  // Датчик питается от 3,3 В: без полного затухания верхний край хода упирался
  // бы в потолок ADC и привод «слеп» у максимума.
  analogSetPinAttenuation(PIN_POSITION, ADC_11db);

  esp_timer_create_args_t timerArgs = {};
  timerArgs.callback = &ServoAxis::pulseTimerCallback;
  timerArgs.name = "servoPulse";

  if (esp_timer_create(&timerArgs, &g_pulseTimer) != ESP_OK) {
    g_pulseTimer = nullptr;
    Serial.println("SERVO: таймер импульсов недоступен, длительность по тикам");
  }

  setOutput(Direction::None, true);
  digitalWrite(PIN_DRIVER_ENABLE, HIGH);

  // Шкала ядра ложится на найденный при первом включении ход. Пока калибровки
  // нет, работает весь диапазон датчика — как у заводской прошивки.
  Settings &settings = Settings::instance();

  if (settings.calibrated()) {
    adcMin_ = settings.adcMin();
    adcMax_ = settings.adcMax();
  }

  runtime.calibrated = settings.calibrated();
  runtime.calMin = adcMin_;
  runtime.calMax = adcMax_;

  position_ = readPosition();
  lastPosition_ = position_;
  target_ = position_;

  runtime.positionAdc = position_;
  runtime.targetAdc = target_;

  // Первое включение: крайние положения ещё не известны.
  if (!settings.calibrated()) {
    calRequested_ = true;
  }
}

void ServoAxis::setPosition(int percent) {
  percent = constrain(percent, 0, SERVO_SOURCE_MAX);

  runtime.servoPercent = percent;

  // Уставку применяет задача контура: снимать защёлку ошибки из loop() значило
  // бы менять её состояние из двух задач сразу.
  portENTER_CRITICAL(&stateMux_);
  pendingPercent_ = percent;
  pendingTargetValid_ = true;
  portEXIT_CRITICAL(&stateMux_);
}

void ServoAxis::setLinkOnline(bool online) {
  portENTER_CRITICAL(&stateMux_);
  linkOnline_ = online;
  portEXIT_CRITICAL(&stateMux_);
}

void ServoAxis::clearFault() {
  portENTER_CRITICAL(&stateMux_);
  clearRequested_ = true;
  portEXIT_CRITICAL(&stateMux_);
}

void ServoAxis::requestCalibration() {
  portENTER_CRITICAL(&stateMux_);
  calRequested_ = true;
  portEXIT_CRITICAL(&stateMux_);
}

void ServoAxis::applyRequests() {
  int percent = 0;
  bool hasTarget = false;
  bool clear = false;
  bool calibrate = false;
  bool online = false;

  portENTER_CRITICAL(&stateMux_);
  hasTarget = pendingTargetValid_;
  percent = pendingPercent_;
  pendingTargetValid_ = false;
  clear = clearRequested_;
  clearRequested_ = false;
  calibrate = calRequested_;
  calRequested_ = false;
  online = linkOnline_;
  portEXIT_CRITICAL(&stateMux_);

  if (online != linkOnlineSeen_) {
    linkOnlineSeen_ = online;
    resetWindow();
  }

  if (calibrate && !calibrating_) {
    beginCalibration();
  }

  if (clear) {
    applyClearRequest();
  }

  if (hasTarget) {
    applyTarget(percent);
  }
}

void ServoAxis::applyTarget(int percent) {
  const int span = adcMax_ - adcMin_;

  const int target = adcMin_ + static_cast<int>((static_cast<long>(percent) * span) / SERVO_SOURCE_MAX);

  runtime.targetAdc = target;

  if (target == target_) {
    return;
  }

  // Новая уставка заметно дальше прежней — владелец явно повёл привод в другое
  // место. Защёлкнутую ошибку снимаем и даём приводу ещё попытку.
  if (latched_ && abs(target - latchTarget_) > POSITION_NEAR) {
    latched_ = false;
    attempts_ = 0;
    fault_ = FaultCode::None;
    retryPending_ = false;

    runtime.fault = 0;
    runtime.faultAttempts = 0;
  }

  target_ = target;
}

void ServoAxis::applyClearRequest() {
  fault_ = FaultCode::None;
  attempts_ = 0;
  latched_ = false;
  retryPending_ = false;

  // Неотправленный отчёт об ошибке снимается вместе с ней: иначе ядро получило
  // бы кадр servo_error с кодом 0 уже после ручного сброса.
  portENTER_CRITICAL(&stateMux_);
  reportPending_ = false;
  portEXIT_CRITICAL(&stateMux_);

  runtime.fault = 0;
  runtime.faultAttempts = 0;

  resetWindow();
}

void ServoAxis::update() {
  const unsigned long now = millis();

  applyRequests();

  // Калибровка идёт без линка и без уставки: привод сам ищет упоры.
  if (calibrating_) {
    runPhase(now);
    return;
  }

  if (!linkOnlineSeen_ || latched_) {
    holdBrake(now);
    return;
  }

  if (retryPending_) {
    if (static_cast<uint32_t>(now - retryStarted_) < STALL_RETRY_MS) {
      holdBrake(now);
      return;
    }

    retryPending_ = false;
    resetWindow();
  }

  runPhase(now);
}

void ServoAxis::runPhase(unsigned long now) {
  switch (phase_) {
    case Phase::Read:
      if (calibrating_) {
        handleCalibrationRead(now);
      } else {
        handleRead(now);
      }
      break;

    case Phase::Pulse:
      // Выход переключает таймер; здесь только переходим к паузе. Если таймера
      // нет (или он не создался), импульс завершается по дедлайну.
      if (!pulseEnded_ && static_cast<int32_t>(micros() - pulseDeadlineUs_) >= 0) {
        endPulse();
      }

      if (pulseEnded_) {
        portENTER_CRITICAL(&stateMux_);
        phase_ = Phase::Rest;
        portEXIT_CRITICAL(&stateMux_);

        phaseStarted_ = now;
        phaseDuration_ = phaseBrakeAfter_ ? pulseMs_ : PULSE_COAST_MS;
      }
      break;

    case Phase::Rest:
      if (static_cast<uint32_t>(now - phaseStarted_) >= phaseDuration_) {
        portENTER_CRITICAL(&stateMux_);
        phase_ = Phase::Read;
        portEXIT_CRITICAL(&stateMux_);
      }
      break;
  }
}

void ServoAxis::beginCalibration() {
  if (g_pulseTimer != nullptr) {
    esp_timer_stop(g_pulseTimer);
  }

  portENTER_CRITICAL(&stateMux_);
  pulseEnded_ = true;
  phase_ = Phase::Read;
  portEXIT_CRITICAL(&stateMux_);

  setOutput(Direction::None, true);

  calibrating_ = true;
  calLeg_ = CalLeg::Low;
  calDirection_ = Direction::Up;
  calLegStarted_ = millis();
  calLowSeen_ = position_;

  // Попутно снимаем всё, что могло остаться от прошлой работы.
  fault_ = FaultCode::None;
  attempts_ = 0;
  latched_ = false;
  retryPending_ = false;

  runtime.fault = 0;
  runtime.faultAttempts = 0;
  runtime.calibrating = true;

  resetWindow();

  Serial.println("SERVO: калибровка крайних положений");
}

void ServoAxis::handleCalibrationRead(unsigned long now) {
  position_ = readPosition();
  runtime.positionAdc = position_;

  travel_ += abs(position_ - lastPosition_);
  lastPosition_ = position_;
  ++window_;

  const bool legSettled = static_cast<uint32_t>(now - calLegStarted_) >= CAL_LEG_MIN_MS;

  if (window_ >= CAL_STALL_WINDOW) {
    const int travel = travel_;
    resetWindow();

    // Привод перестал двигаться — считаем, что дошли до упора.
    if (legSettled && travel < CAL_STALL_MIN_TRAVEL) {
      finishCalibrationLeg(now);
      return;
    }
  }

  if (static_cast<uint32_t>(now - calLegStarted_) >= CAL_MOVE_TIMEOUT_MS) {
    finishCalibrationLeg(now);
    return;
  }

  startPulse(calDirection_, PULSE_COARSE_MS, false, now);
}

void ServoAxis::finishCalibrationLeg(unsigned long now) {
  holdBrake(now);

  if (calLeg_ == CalLeg::Low) {
    calLowSeen_ = position_;
    calLeg_ = CalLeg::High;
    calDirection_ = Direction::Down;
    calLegStarted_ = now;
    resetWindow();
    return;
  }

  calibrating_ = false;
  calLeg_ = CalLeg::None;
  runtime.calibrating = false;

  const int low = calLowSeen_;
  const int high = position_;

  if (high - low >= CAL_MIN_SPAN) {
    adcMin_ = low;
    adcMax_ = high;

    Settings::instance().saveCalibration(static_cast<uint16_t>(adcMin_),
                                        static_cast<uint16_t>(adcMax_));

    runtime.calibrated = true;
    runtime.calMin = adcMin_;
    runtime.calMax = adcMax_;

    Serial.printf("SERVO: крайние положения %d…%d ADC\n", adcMin_, adcMax_);
  } else {
    // Ход подозрительно мал: механика заела или датчик не подключён. Оставляем
    // прежнюю калибровку (или весь диапазон датчика), а не кривую новую.
    Serial.printf("SERVO: калибровка не удалась (%d…%d ADC), шкала прежняя\n", low, high);
  }

  // Уставка пересчитывается по новой шкале.
  target_ = -1;

  resetWindow();
}

void ServoAxis::handleRead(unsigned long now) {
  position_ = readPosition();
  runtime.positionAdc = position_;

  const int diff = target_ - position_;
  const int distance = abs(diff);

  travel_ += abs(position_ - lastPosition_);
  lastPosition_ = position_;
  ++window_;

  if (window_ >= STALL_WINDOW) {
    const int travel = travel_;
    resetWindow();

    if (travel < STALL_MIN_TRAVEL && distance > POSITION_DEADBAND) {
      raiseStall(now);
      return;
    }
  }

  if (distance <= POSITION_DEADBAND) {
    // Доехали: держим тормозом. Счётчик попыток обнуляется только здесь,
    // иначе заклинивший привод «лечился» бы каждой сменой уставки по оборотам.
    attempts_ = 0;
    retryPending_ = false;

    if (fault_ != FaultCode::None) {
      fault_ = FaultCode::None;
      runtime.fault = 0;
      runtime.faultAttempts = 0;
    }

    holdBrake(now);
    return;
  }

  uint32_t pulseMs;
  bool brakeAfter;

  if (distance > POSITION_COARSE) {
    pulseMs = PULSE_COARSE_MS;
    brakeAfter = false;
  } else {
    pulseMs = constrain(distance / POSITION_FINE_DIVISOR,
                        static_cast<int>(PULSE_MIN_MS),
                        static_cast<int>(PULSE_MAX_MS));
    brakeAfter = distance < POSITION_NEAR;
  }

  startPulse(diff < 0 ? Direction::Up : Direction::Down, pulseMs, brakeAfter, now);
}

void ServoAxis::startPulse(Direction direction, uint32_t pulseMs, bool brakeAfter, unsigned long now) {
  pulseMs_ = pulseMs;
  phaseBrakeAfter_ = brakeAfter;

  // Дедлайн — до перевода фазы в Pulse: запоздавший callback прошлого импульса
  // увидит новую фазу с уже будущим дедлайном и новый импульс не оборвёт.
  portENTER_CRITICAL(&stateMux_);
  pulseDeadlineUs_ = micros() + pulseMs * 1000;
  pulseEnded_ = false;
  phase_ = Phase::Pulse;
  portEXIT_CRITICAL(&stateMux_);

  setOutput(direction, false);

  phaseStarted_ = now;
  phaseDuration_ = pulseMs;

  if (g_pulseTimer != nullptr) {
    esp_timer_stop(g_pulseTimer);
    esp_timer_start_once(g_pulseTimer, static_cast<uint64_t>(pulseMs) * 1000);
  }
}

void ServoAxis::endPulse() {
  bool brakeAfter = false;
  bool brake = false;

  portENTER_CRITICAL(&stateMux_);

  const bool inPulse = phase_ == Phase::Pulse;

  // Запоздавший callback прошлого импульса: дедлайн ещё не наступил.
  const bool deadlineReached = static_cast<int32_t>(micros() - pulseDeadlineUs_) >= 0;

  if (inPulse && deadlineReached) {
    // Линк мог пропасть в середине импульса: тогда тормоз важнее выбега.
    brake = !linkOnline_ || latched_;
    brakeAfter = phaseBrakeAfter_;

    pulseEnded_ = true;
  }

  portEXIT_CRITICAL(&stateMux_);

  if (!inPulse || !deadlineReached) {
    return;
  }

  setOutput(Direction::None, brake || brakeAfter);
}

void ServoAxis::pulseTimerCallback(void *arg) {
  (void) arg;

  ServoAxis::instance().endPulse();
}

void ServoAxis::holdBrake(unsigned long now) {
  if (phase_ == Phase::Pulse && g_pulseTimer != nullptr) {
    esp_timer_stop(g_pulseTimer);
  }

  // Запоздавший callback не должен вернуть выбег после тормоза.
  portENTER_CRITICAL(&stateMux_);
  pulseEnded_ = true;
  phase_ = Phase::Rest;
  portEXIT_CRITICAL(&stateMux_);

  setOutput(Direction::None, true);

  phaseStarted_ = now;
  phaseDuration_ = PULSE_HOLD_MS;
  phaseBrakeAfter_ = true;
}

void ServoAxis::setOutput(Direction direction, bool brake) {
  portENTER_CRITICAL(&ioMux_);

  const bool up = brake || direction == Direction::Up;
  const bool down = brake || direction == Direction::Down;

  if (up != outUp_) {
    digitalWrite(PIN_MOTOR_UP, up ? HIGH : LOW);
    outUp_ = up;
  }

  if (down != outDown_) {
    digitalWrite(PIN_MOTOR_DOWN, down ? HIGH : LOW);
    outDown_ = down;
  }

  portEXIT_CRITICAL(&ioMux_);
}

void ServoAxis::resetWindow() {
  travel_ = 0;
  window_ = 0;
  lastPosition_ = position_;
}

void ServoAxis::raiseStall(unsigned long now) {
  ++attempts_;

  fault_ = FaultCode::Stall;

  // Отчёт фиксируется на момент ошибки: если кадр удастся отправить позже,
  // ядро не должно получить текущее положение вместо того, где привод встал.
  portENTER_CRITICAL(&stateMux_);
  reportCode_ = static_cast<uint8_t>(FaultCode::Stall);
  reportTarget_ = target_;
  reportPosition_ = position_;
  reportPending_ = true;
  portEXIT_CRITICAL(&stateMux_);

  runtime.fault = static_cast<uint8_t>(FaultCode::Stall);
  runtime.faultAttempts = attempts_;

  holdBrake(now);

  if (attempts_ >= STALL_MAX_ATTEMPTS) {
    latched_ = true;
    latchTarget_ = target_;
    retryPending_ = false;

    Serial.printf("SERVO: stall %u/%u, ожидание новой уставки\n",
                  static_cast<unsigned>(attempts_),
                  static_cast<unsigned>(STALL_MAX_ATTEMPTS));
    return;
  }

  retryPending_ = true;
  retryStarted_ = now;

  Serial.printf("SERVO: stall %u/%u, повтор через %lu мс\n",
                static_cast<unsigned>(attempts_),
                static_cast<unsigned>(STALL_MAX_ATTEMPTS),
                static_cast<unsigned long>(STALL_RETRY_MS));
}

int ServoAxis::readPosition() {
  int sum = 0;

  for (int i = 0; i < POSITION_SAMPLES; ++i) {
    sum += analogRead(PIN_POSITION);
  }

  return constrain(sum / POSITION_SAMPLES, 0, POSITION_ADC_MAX);
}

bool ServoAxis::takeFaultReport(uint8_t &code, int &target, int &position) {
  bool pending = false;

  portENTER_CRITICAL(&stateMux_);

  if (reportPending_) {
    reportPending_ = false;
    pending = true;

    code = reportCode_;
    target = reportTarget_;
    position = reportPosition_;
  }

  portEXIT_CRITICAL(&stateMux_);

  return pending;
}
