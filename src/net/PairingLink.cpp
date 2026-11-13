#include "net/PairingLink.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config/CONFIG.h"
#include "config/PROPS.h"

bool CoreCredentials::valid() const {
  return ssid.length() > 0 && password.length() >= 8 && host.length() > 0 && port != 0 &&
         token.length() == 16;
}

PairingLink &PairingLink::instance() {
  static PairingLink link;
  return link;
}

bool PairingLink::pair(CoreCredentials &out, const String &pairCode,
                       const String &expectedBssid, uint32_t timeoutMs) {
  if (pairCode.length() < 8) {
    Serial.println("PAIR: нет кода сопряжения — сеть ядра закрыта паролем");
    return false;
  }

  if (WiFi.getMode() == WIFI_OFF) {
    WiFi.mode(WIFI_STA);
  }

  Serial.println("PAIR: ищем сеть сопряжения ядра");

  const int maxCandidates = 16;
  String ssids[maxCandidates];
  String bssids[maxCandidates];
  int rssis[maxCandidates];
  int count = 0;

  const int found = WiFi.scanNetworks();

  for (int i = 0; i < found && count < maxCandidates; ++i) {
    if (!WiFi.SSID(i).startsWith(PAIR_AP_PREFIX)) {
      continue;
    }

    // Своё ядро узнаём по BSSID: без этой проверки любая DED_PAIR_* в радиусе
    // увела бы настроенный модуль на поддельный блок.
    const String bssid = WiFi.BSSIDstr(i);

    if (expectedBssid.length() > 0 && !bssid.equalsIgnoreCase(expectedBssid)) {
      continue;
    }

    ssids[count] = WiFi.SSID(i);
    bssids[count] = bssid;
    rssis[count] = WiFi.RSSI(i);
    count++;
  }

  WiFi.scanDelete();

  if (count == 0) {
    Serial.println(expectedBssid.length() > 0
                       ? "PAIR: сеть сопряжения этого ядра не найдена"
                       : "PAIR: сеть DED_PAIR_* не найдена");
    return false;
  }

  // Сильнейший сигнал пробуем первым, но при ошибке пароля переходим к
  // следующему кандидату: чужая DED_PAIR_* рядом не должна блокировать своё ядро.
  for (int i = 0; i < count; i++) {
    for (int j = i + 1; j < count; j++) {
      if (rssis[j] > rssis[i]) {
        const int rssiSwap = rssis[i];
        rssis[i] = rssis[j];
        rssis[j] = rssiSwap;

        const String ssidSwap = ssids[i];
        ssids[i] = ssids[j];
        ssids[j] = ssidSwap;

        const String bssidSwap = bssids[i];
        bssids[i] = bssids[j];
        bssids[j] = bssidSwap;
      }
    }
  }

  String apSsid;
  String apBssid;
  bool connected = false;

  const uint32_t perCandidateTimeout = timeoutMs < 8000 ? timeoutMs : 8000;

  for (int candidate = 0; candidate < count && !connected; candidate++) {
    Serial.printf("PAIR: пробуем '%s' (%s)\n", ssids[candidate].c_str(), bssids[candidate].c_str());

    WiFi.begin(ssids[candidate].c_str(), pairCode.c_str());

    const unsigned long attemptStarted = millis();

    while (WiFi.status() != WL_CONNECTED) {
      if (static_cast<uint32_t>(millis() - attemptStarted) > perCandidateTimeout) {
        break;
      }

      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      apSsid = ssids[candidate];
      apBssid = bssids[candidate];
    } else {
      WiFi.disconnect(false);
      delay(50);
    }
  }

  if (!connected) {
    Serial.println("PAIR: не удалось подключиться к сети сопряжения (код не подошёл?)");
    return false;
  }

  Serial.printf("PAIR: подключились к '%s' (%s)\n", apSsid.c_str(), apBssid.c_str());

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

    // Ядро привязывает токен к MAC модуля: украденный токен не сработает
    // с другого устройства.
    http.addHeader("X-Module-Mac", WiFi.macAddress());

    const int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
    } else {
      Serial.printf("PAIR: ядро ответило %d\n", httpCode);
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
  const char *token = root["data"]["token"];

  out.ssid = ssid != nullptr ? String(ssid) : String();
  out.password = password != nullptr ? String(password) : String();
  out.host = host != nullptr ? String(host) : String();
  out.token = token != nullptr ? String(token) : String();
  out.bssid = apBssid;
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
