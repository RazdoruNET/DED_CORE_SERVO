#include "app/ServoAxis.h"

#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PINS.h"

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

void ServoAxis::setLinkOnline(bool online) {
  if (linkOnline_ == online) {
    return;
  }

  linkOnline_ = online;
  resetWindow();

  if (!online) {
    holdBrake(millis());
  }
}

void ServoAxis::clearFault() {
  fault_ = FaultCode::None;
  attempts_ = 0;
  latched_ = false;
  retryPending_ = false;

  runtime.fault = 0;
  runtime.faultAttempts = 0;

  resetWindow();
}

void ServoAxis::update() {
  const unsigned long now = millis();

  if (!linkOnline_ || latched_) {
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
      if (static_cast<uint32_t>(now - phaseStarted_) >= phaseDuration_) {
        setOutput(Direction::None, phaseBrakeAfter_);

        phase_ = Phase::Rest;
        phaseStarted_ = now;
        phaseDuration_ = phaseBrakeAfter_ ? pulseMs_ : PULSE_COAST_MS;
      }
      break;

    case Phase::Rest:
      if (static_cast<uint32_t>(now - phaseStarted_) >= phaseDuration_) {
        phase_ = Phase::Read;
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

  setOutput(direction, false);

  phase_ = Phase::Pulse;
  phaseStarted_ = now;
  phaseDuration_ = pulseMs;
}

void ServoAxis::holdBrake(unsigned long now) {
  setOutput(Direction::None, true);

  phase_ = Phase::Rest;
  phaseStarted_ = now;
  phaseDuration_ = PULSE_HOLD_MS;
  phaseBrakeAfter_ = true;
}

void ServoAxis::setOutput(Direction direction, bool brake) {
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
}

void ServoAxis::resetWindow() {
  travel_ = 0;
  window_ = 0;
  lastPosition_ = position_;
}

void ServoAxis::raiseStall(unsigned long now) {
  ++attempts_;

  fault_ = FaultCode::Stall;
  reportPending_ = true;

  ++runtime.faultsTotal;
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
  if (!reportPending_) {
    return false;
  }

  reportPending_ = false;

  code = static_cast<uint8_t>(fault_);
  target = target_;
  position = position_;

  return true;
}
