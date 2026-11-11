#ifndef NET_SETUP_PORTAL_H
#define NET_SETUP_PORTAL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Портал настройки модуля.
//
// Поднимается, когда модуль ещё не знает ядро или потерял его: точка доступа
// DED_SERVO_DRIVER, страница со состоянием, ручной ввод параметров ядра,
// запуск сопряжения и обновление прошивки. В рабочем режиме портала нет.
class SetupPortal {
public:
  static SetupPortal &instance();

  void begin();
  void loop();
  void stop();

  bool active() const { return active_; }

  void requestPairing();
  bool takePairingRequest();
  void setPairingStatus(const String &text);

private:
  SetupPortal() = default;

  void registerRoutes();
  String buildPage() const;
  String buildStatusJson() const;

  AsyncWebServer server_{80};

  bool started_ = false;
  bool active_ = false;

  bool pairingRequested_ = false;
  String pairingStatus_;

  unsigned long restartAt_ = 0;
};

#endif // NET_SETUP_PORTAL_H
