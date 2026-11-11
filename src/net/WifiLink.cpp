#include "net/WifiLink.h"

#include <WiFi.h>

#include "config/CONFIG.h"

WifiLink &WifiLink::instance() {
  static WifiLink link;
  return link;
}

void WifiLink::begin(const String &ssid, const String &password) {
  ssid_ = ssid;
  password_ = password;

  if (!started_) {
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    started_ = true;
  }

  // Портал настройки держит точку доступа: в AP+STA линк к ядру сохраняется,
  // поэтому режим портала не перебиваем.
  if (WiFi.getMode() != WIFI_AP_STA) {
    WiFi.mode(WIFI_STA);
  }

  connect();
}

void WifiLink::loop() {
  if (ssid_.length() == 0) {
    return;
  }

  const bool connected = WiFi.status() == WL_CONNECTED;

  if (connected != online_) {
    online_ = connected;

    if (connected) {
      Serial.printf("WIFI: связь с '%s' есть, ip %s\n", ssid_.c_str(), ip().c_str());
    } else {
      Serial.println("WIFI: связь с ядром потеряна");
    }
  }

  if (connected) {
    return;
  }

  if (static_cast<uint32_t>(millis() - attemptAt_) >= WIFI_RETRY_MS) {
    connect();
  }
}

void WifiLink::stop() {
  ssid_ = "";
  password_ = "";
  online_ = false;

  WiFi.disconnect(true);
}

bool WifiLink::isConnected() const {
  return WiFi.status() == WL_CONNECTED && ssid_.length() > 0;
}

String WifiLink::ip() const {
  return WiFi.localIP().toString();
}

void WifiLink::connect() {
  attemptAt_ = millis();

  Serial.printf("WIFI: подключаемся к '%s'\n", ssid_.c_str());
  WiFi.begin(ssid_.c_str(), password_.c_str());
}
