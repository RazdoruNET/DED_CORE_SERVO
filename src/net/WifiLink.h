#ifndef NET_WIFI_LINK_H
#define NET_WIFI_LINK_H

#include <Arduino.h>

// Подключение модуля к точке доступа ядра.
//
// Модуль — обычный STA-клиент: точка доступа ядра раздаёт адреса и держит
// WebSocket на 81 порту. Сон WiFi выключен: с ним кадр телеметрии приходит
// с задержкой до сотен миллисекунд, а привод должен реагировать сразу.
class WifiLink {
public:
  static WifiLink &instance();

  void begin(const String &ssid, const String &password);

  // Новые параметры сети от ядра: смена пароля точки доступа не должна рвать
  // связку, поэтому модуль переподключается сам, без нового сопряжения.
  void applyCredentials(const String &ssid, const String &password);

  void loop();

  bool isConnected() const;
  String ip() const;

private:
  WifiLink() = default;

  void connect();

  String ssid_;
  String password_;

  unsigned long attemptAt_ = 0;
  bool started_ = false;
  bool online_ = false;
};

#endif // NET_WIFI_LINK_H
