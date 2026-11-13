#include "app/ServoAxis.h"

#include <esp_timer.h>

#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PINS.h"

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

  position_ = readPosition();
  lastPosition_ = position_;
  target_ = position_;

  runtime.positionAdc = position_;
  runtime.targetAdc = target_;
}

void ServoAxis::setPosition(int percent) {
  percent = constrain(percent, 0, SERVO_SOURCE_MAX);

  const int target = static_cast<int>((static_cast<long>(percent) * POSITION_ADC_MAX) / SERVO_SOURCE_MAX);

  runtime.servoPercent = percent;
  runtime.targetAdc = target;

  portENTER_CRITICAL(&stateMux_);
  pendingTarget_ = target;
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

void ServoAxis::applyRequests() {
  int target = 0;
  bool hasTarget = false;
  bool clear = false;
  bool online = false;

  portENTER_CRITICAL(&stateMux_);
  hasTarget = pendingTargetValid_;
  target = pendingTarget_;
  pendingTargetValid_ = false;
  clear = clearRequested_;
  clearRequested_ = false;
  online = linkOnline_;
  portEXIT_CRITICAL(&stateMux_);

  if (online != linkOnlineSeen_) {
    linkOnlineSeen_ = online;
    resetWindow();
  }

  if (clear) {
    applyClearRequest();
  }

  if (hasTarget) {
    applyTarget(target);
  }
}

void ServoAxis::applyTarget(int target) {
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

  switch (phase_) {
    case Phase::Read:
      handleRead(now);
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
