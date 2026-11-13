#ifndef NET_SETUP_PORTAL_H
#define NET_SETUP_PORTAL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Портал настройки модуля.
//
// Поднимается, когда модуль ещё не знает ядро или потерял его: точка доступа
// DED_SERVO_DRIVER, страница со состоянием, ручной ввод параметров ядра,
// запуск сопряжения и обновление прошивки. В рабочем режиме портала нет.
//
// Обработчики ESPAsyncWebServer выполняются в задаче AsyncTCP, а не в loop():
// состояние портала между ними передаётся под мьютексом.
class SetupPortal {
public:
  static SetupPortal &instance();

  void begin();
  void loop();
  void stop();

  bool active() const { return active_; }

  bool takePairingRequest();
  void setPairingStatus(const String &text);

private:
  SetupPortal();

  void registerRoutes();
  String buildPage() const;
  String buildStatusJson() const;

  // Маршруты настроек не отвечают, когда портал закрыт, и не отвечают клиентам
  // из сети ядра: в AP+STA портал виден и там, а настройки меняет только
  // владелец, подключённый к точке доступа портала. Исключение — /update: его
  // регистрирует ElegantOTA, он защищён паролем.
  bool allowRequest(AsyncWebServerRequest *request) const;
  static bool isPortalClient(AsyncWebServerRequest *request);

  // Токен формы: без него POST с чужой страницы (и просто подделанный запрос)
  // не проходит, даже если портал открыт.
  bool checkCsrf(AsyncWebServerRequest *request) const;

  String pairingStatusSnapshot() const;

  AsyncWebServer server_{80};

  volatile bool started_ = false;
  volatile bool active_ = false;

  volatile bool pairingRequested_ = false;
  volatile unsigned long restartAt_ = 0;

  mutable SemaphoreHandle_t statusMutex_ = nullptr;
  String pairingStatus_;

  // Выдаётся один раз при регистрации маршрутов и живёт до перезагрузки.
  String csrfToken_;
};

#endif // NET_SETUP_PORTAL_H
