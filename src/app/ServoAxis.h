#ifndef APP_SERVO_AXIS_H
#define APP_SERVO_AXIS_H

#include <Arduino.h>

// Код ошибки привода. Ноль — ошибки нет; значение уходит ядру в кадре
// servo_error (см. docs/PROTOCOL.md).
enum class FaultCode : uint8_t {
  None = 0,
  Stall = 1,
};

// Контур привода: удержание положения по аналоговому датчику.
//
// Задача — довести шток до уставки, которую ядро присылает в кадре телеметрии,
// и удерживать её. Привод без обратной связи по скорости, поэтому движение
// строится импульсами: длинный импульс далеко от уставки, короткий — рядом,
// торможение перед уставкой.
class ServoAxis {
public:
  static ServoAxis &instance();

  void begin();

  // Уставка в шкале ядра: 0…255. Вызывается из loop(): сама уставка только
  // записывается, применяет её задача контура (см. update()).
  void setPosition(int percent);

  // Линк с ядром пропал: привод фиксируется тормозом и не двигается,
  // пока кадры не вернутся. Неожиданного движения без команды быть не должно.
  void setLinkOnline(bool online);

  // Снять защёлкнутую ошибку вручную.
  void clearFault();

  // Шаг контура. Вызывается из задачи каждые CONTROL_TICK_MS.
  void update();

  int position() const { return position_; }
  int target() const { return target_; }
  bool faulted() const { return fault_ != FaultCode::None; }
  uint8_t faultCode() const { return static_cast<uint8_t>(fault_); }

  // Забрать событие ошибки для отправки ядру. true — ровно один раз на ошибку.
  bool takeFaultReport(uint8_t &code, int &target, int &position);

private:
  enum class Phase : uint8_t { Read, Pulse, Rest };
  enum class Direction : int8_t { Up = -1, Down = 1, None = 0 };

  ServoAxis() = default;

  int readPosition();

  // Запросы из loop() (уставка, сброс ошибки, состояние линка) переносятся в
  // состояние контура здесь, в задаче привода: менять его из двух задач сразу
  // значило бы ловить защёлку с половиной сброшенных полей.
  void applyRequests();
  void applyTarget(int target);
  void applyClearRequest();

  void handleRead(unsigned long now);
  void startPulse(Direction direction, uint32_t pulseMs, bool brakeAfter, unsigned long now);
  void holdBrake(unsigned long now);
  void setOutput(Direction direction, bool brake);

  void resetWindow();
  void raiseStall(unsigned long now);

  // Импульс завершает одноразовый таймер: задача контура может быть вытеснена
  // Wi-Fi-стеком, а длительность импульса от этого зависеть не должна.
  static void pulseTimerCallback(void *arg);
  void endPulse();

  // Состояние фазы и линка делят задача контура и callback таймера.
  portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;

  // Выходы моста: setOutput() зовут обе стороны, две записи в пины не должны
  // перемешиваться.
  portMUX_TYPE ioMux_ = portMUX_INITIALIZER_UNLOCKED;

  Phase phase_ = Phase::Read;

  unsigned long phaseStarted_ = 0;
  uint32_t phaseDuration_ = 0;
  uint32_t pulseMs_ = 0;
  bool phaseBrakeAfter_ = false;

  // Дедлайн текущего импульса в micros() и признак «выход уже переключён».
  unsigned long pulseDeadlineUs_ = 0;
  volatile bool pulseEnded_ = false;

  bool outUp_ = false;
  bool outDown_ = false;

  // Признаки из loop(), защищены stateMux_.
  bool linkOnline_ = false;
  int pendingTarget_ = 0;
  bool pendingTargetValid_ = false;
  bool clearRequested_ = false;

  bool reportPending_ = false;
  uint8_t reportCode_ = 0;
  int reportTarget_ = 0;
  int reportPosition_ = 0;

  // Копия состояния линка в задаче контура: переход ловится здесь.
  bool linkOnlineSeen_ = false;

  int position_ = 0;
  int lastPosition_ = 0;
  int target_ = 0;

  int travel_ = 0;
  uint8_t window_ = 0;

  FaultCode fault_ = FaultCode::None;
  uint8_t attempts_ = 0;
  bool latched_ = false;
  int latchTarget_ = 0;

  bool retryPending_ = false;
  unsigned long retryStarted_ = 0;
};

#endif // APP_SERVO_AXIS_H
