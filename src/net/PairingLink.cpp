#include "net/PairingLink.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config/CONFIG.h"
#include "config/PROPS.h"

bool CoreCredentials::valid() const {
  return ssid.length() > 0 && password.length() >= 8 && host.length() > 0 && port != 0;
}

PairingLink &PairingLink::instance() {
  static PairingLink link;
  return link;
}

bool PairingLink::pair(CoreCredentials &out, uint32_t timeoutMs) {
  const unsigned long started = millis();

  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
  }

  Serial.println("PAIR: ищем сеть сопряжения ядра");

  const int found = WiFi.scanNetworks();
  int best = -1;
  int bestRssi = -1000;

  for (int i = 0; i < found; ++i) {
    if (!WiFi.SSID(i).startsWith(PAIR_AP_PREFIX)) {
      continue;
    }

    const int rssi = WiFi.RSSI(i);

    if (rssi > bestRssi) {
      bestRssi = rssi;
      best = i;
    }
  }

  String apSsid;

  if (best >= 0) {
    apSsid = WiFi.SSID(best);
  }

  WiFi.scanDelete();

  if (apSsid.length() == 0) {
    Serial.println("PAIR: сеть DED_PAIR_* не найдена");
    return false;
  }

  Serial.printf("PAIR: найдена '%s', подключаемся\n", apSsid.c_str());

  // Сеть сопряжения открытая: пароля нет по устройству протокола. Секретов,
  // кроме параметров точки доступа, она не отдаёт и живёт только по кнопке.
  WiFi.begin(apSsid.c_str());

  while (WiFi.status() != WL_CONNECTED) {
    if (static_cast<uint32_t>(millis() - started) > timeoutMs) {
      Serial.println("PAIR: не удалось подключиться к сети сопряжения");
      WiFi.disconnect(false);
      return false;
    }

    delay(100);
  }

  const String url = "http://" + WiFi.gatewayIP().toString() + "/module/pair";

  Serial.printf("PAIR: запрос %s\n", url.c_str());

  String payload;

  {
    HTTPClient http;
    http.setTimeout(5000);

    if (!http.begin(url)) {
      Serial.println("PAIR: HTTP-клиент не открылся");
      WiFi.disconnect(false);
      return false;
    }

    const int code = http.GET();

    if (code == HTTP_CODE_OK) {
      payload = http.getString();
    } else {
      Serial.printf("PAIR: ядро ответило %d\n", code);
    }

    http.end();
  }

  if (payload.length() == 0) {
    WiFi.disconnect(false);
    return false;
  }

  DynamicJsonBuffer jsonBuffer;
  JsonObject &root = jsonBuffer.parseObject(payload);

  if (!root.success() || root["event"] != "module_pair") {
    Serial.println("PAIR: неожиданный ответ ядра");
    WiFi.disconnect(false);
    return false;
  }

  const char *ssid = root["data"]["ssid"];
  const char *password = root["data"]["pass"];
  const char *host = root["data"]["host"];

  out.ssid = ssid != nullptr ? String(ssid) : String();
  out.password = password != nullptr ? String(password) : String();
  out.host = host != nullptr ? String(host) : String();
  out.port = static_cast<uint16_t>(root["data"]["port"] | CORE_DEFAULT_PORT);

  if (out.host.length() == 0) {
    out.host = CORE_DEFAULT_HOST;
  }

  if (!out.valid()) {
    Serial.println("PAIR: ядро вернуло неполные параметры");
    WiFi.disconnect(false);
    return false;
  }

  Serial.println("PAIR: параметры ядра получены");

  // Ядро возвращает основную точку доступа из ответа на этот запрос: ждём,
  // пока она поднимется, иначе следующий WiFi.begin уйдёт в пустоту.
  WiFi.disconnect(false);
  delay(3000);

  return true;
}
