#include "net/SetupPortal.h"

#include <ElegantOTA.h>
#include <WiFi.h>

#include "app/ServoAxis.h"
#include "app/Status.h"
#include "config/CONFIG.h"
#include "config/PROPS.h"
#include "net/Settings.h"
#include "net/WifiLink.h"

namespace {

String formParam(AsyncWebServerRequest *request, const char *name) {
  // Второй аргумент true — параметр из тела POST-формы, а не из строки запроса.
  if (!request->hasParam(name, true)) {
    return "";
  }

  return request->getParam(name, true)->value();
}

String htmlMessage(const String &text) {
  return "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>DED SERVO</title></head><body style=\"font-family:system-ui,sans-serif;"
         "background:#111;color:#eee;padding:24px\">" + text +
         "<p><a style=\"color:#6ea8ff\" href=\"/\">Назад</a></p></body></html>";
}

}  // namespace

SetupPortal &SetupPortal::instance() {
  static SetupPortal portal;
  return portal;
}

void SetupPortal::begin() {
  if (!started_) {
    registerRoutes();
    server_.begin();
    started_ = true;
  }

  if (active_) {
    return;
  }

  // AP+STA: портал может быть открыт, пока модуль уже подключён к ядру,
  // и терять этот линк из-за настройки нельзя.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(PORTAL_AP_SSID);

  active_ = true;
  pairingStatus_ = "";

  Serial.printf("SETUP: сеть '%s', страница http://%s/\n",
                PORTAL_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void SetupPortal::stop() {
  if (!active_) {
    return;
  }

  WiFi.softAPdisconnect(true);
  active_ = false;

  Serial.println("SETUP: портал закрыт");
}

void SetupPortal::loop() {
  // Обновление прошивки перезагружает модуль само, но только из этого вызова.
  ElegantOTA.loop();

  if (restartAt_ != 0 && static_cast<int32_t>(millis() - restartAt_) >= 0) {
    Serial.println("SETUP: перезагрузка по настройкам портала");
    ESP.restart();
  }
}

void SetupPortal::requestPairing() {
  pairingRequested_ = true;
}

bool SetupPortal::takePairingRequest() {
  if (!pairingRequested_) {
    return false;
  }

  pairingRequested_ = false;
  return true;
}

void SetupPortal::setPairingStatus(const String &text) {
  pairingStatus_ = text;
}

void SetupPortal::registerRoutes() {
  server_.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", buildPage());
  });

  server_.on("/status.json", HTTP_GET, [this](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildStatusJson());
  });

  server_.on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
    const String ssid = formParam(request, "ssid");
    const String password = formParam(request, "pass");
    const String host = formParam(request, "host");
    const String portText = formParam(request, "port");

    if (ssid.length() == 0) {
      request->send(400, "text/html; charset=utf-8",
                    htmlMessage("Имя точки доступа ядра не может быть пустым."));
      return;
    }

    Settings &settings = Settings::instance();

    uint16_t port = CORE_DEFAULT_PORT;

    if (portText.length() > 0) {
      const long value = portText.toInt();

      if (value > 0 && value <= 65535) {
        port = static_cast<uint16_t>(value);
      }
    }

    // Пустое поле пароля — «не менять»: страница портала открытая и пароль
    // ядра в неё не выводится.
    const String storedPassword = password.length() > 0 ? password : settings.password();

    if (storedPassword.length() < 8) {
      request->send(400, "text/html; charset=utf-8",
                    htmlMessage("Нужен пароль точки доступа ядра (не короче 8 символов)."));
      return;
    }

    settings.saveCore(ssid,
                      storedPassword,
                      host.length() > 0 ? host : String(CORE_DEFAULT_HOST),
                      port);

    request->send(200, "text/html; charset=utf-8", htmlMessage("Сохранено. Перезагрузка…"));
    restartAt_ = millis() + 1500;
  });

  server_.on("/pair", HTTP_GET, [this](AsyncWebServerRequest *request) {
    pairingStatus_ = "Ищу сеть сопряжения ядра…";
    pairingRequested_ = true;
    request->redirect("/");
  });

  server_.on("/fault/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    ServoAxis::instance().clearFault();
    request->redirect("/");
  });

  server_.on("/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
    Settings::instance().clearCore();
    request->send(200, "text/html; charset=utf-8", htmlMessage("Настройки сброшены. Перезагрузка…"));
    restartAt_ = millis() + 1500;
  });

  server_.on("/close", HTTP_POST, [this](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", htmlMessage("Портал закрыт."));
    stop();
  });

  // Обновление прошивки закрыто паролем из NVS: точка доступа портала открытая.
  ElegantOTA.begin(&server_, "admin", Settings::instance().otaPassword().c_str());
}

String SetupPortal::buildPage() const {
  Settings &settings = Settings::instance();

  const bool provisioned = settings.provisioned();

  String page;
  page.reserve(3200);

  page += "<!DOCTYPE html><html lang=\"ru\"><head><meta charset=\"utf-8\">";
  page += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<meta http-equiv=\"refresh\" content=\"5\">";
  page += "<title>DED SERVO</title><style>";
  page += "body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#111;color:#eee}";
  page += "h1{font-size:19px;margin:0 0 14px}table{border-collapse:collapse;width:100%;margin-bottom:14px}";
  page += "td{padding:5px 8px;border-bottom:1px solid #333;font-size:14px}";
  page += "td:first-child{color:#999;width:46%}";
  page += "input{width:100%;padding:9px;margin:5px 0;border:1px solid #444;border-radius:6px;";
  page += "background:#1b1b1b;color:#eee;box-sizing:border-box;font-size:14px}";
  page += "button,a.btn{display:block;width:100%;padding:10px;margin:8px 0;border:0;border-radius:6px;";
  page += "background:#2d7ef7;color:#fff;text-align:center;text-decoration:none;font-size:15px}";
  page += "button.ghost,a.btn.ghost{background:#333}.note{color:#aaa;font-size:13px;line-height:1.4}";
  page += "</style></head><body>";

  page += "<h1>DED CORE SERVO " FIRMWARE_VERSION "</h1><table>";

  page += "<tr><td>Связь с ядром</td><td>";
  page += (runtime.linkOnline ? String("есть") : String("нет"));
  page += "</td></tr>";

  page += "<tr><td>Ядро</td><td>";
  page += (provisioned ? settings.ssid() : String("не настроено"));
  page += "</td></tr>";

  page += "<tr><td>Адрес ядра</td><td>";
  page += settings.host();
  page += ":";
  page += String(settings.port());
  page += "</td></tr>";

  page += "<tr><td>Мой адрес</td><td>";
  page += (WifiLink::instance().isConnected() ? WifiLink::instance().ip() : String("—"));
  page += "</td></tr>";

  page += "<tr><td>Уставка</td><td>";
  page += String(runtime.servoPercent);
  page += " / ";
  page += String(SERVO_SOURCE_MAX);
  page += "</td></tr>";

  page += "<tr><td>Положение</td><td>";
  page += String(runtime.positionAdc);
  page += " / ";
  page += String(POSITION_ADC_MAX);
  page += "</td></tr>";

  page += "<tr><td>Ошибка привода</td><td>";
  page += (runtime.fault == 0
               ? String("нет")
               : String("код ") + String(static_cast<unsigned>(runtime.fault)) +
                     ", попытка " + String(static_cast<unsigned>(runtime.faultAttempts)));
  page += "</td></tr>";

  page += "<tr><td>Кадров от ядра</td><td>";
  page += String(runtime.frames);
  page += "</td></tr>";

  page += "</table>";

  page += "<form method=\"post\" action=\"/save\">";
  page += "<input name=\"ssid\" placeholder=\"Имя точки доступа ядра\" value=\"";
  page += (provisioned ? settings.ssid() : String(""));
  page += "\">";
  page += "<input name=\"pass\" type=\"password\" autocomplete=\"new-password\" placeholder=\"";
  page += (provisioned ? "Пароль ядра (пусто — не менять)" : "Пароль точки доступа ядра");
  page += "\">";
  page += "<input name=\"host\" placeholder=\"Адрес ядра\" value=\"";
  page += settings.host();
  page += "\">";
  page += "<input name=\"port\" placeholder=\"Порт WebSocket\" value=\"";
  page += String(settings.port());
  page += "\">";
  page += "<button type=\"submit\">Сохранить</button></form>";

  page += "<a class=\"btn\" href=\"/pair\">Найти ядро сопряжением</a>";
  page += "<a class=\"btn ghost\" href=\"/fault/clear\">Сбросить ошибку привода</a>";
  page += "<a class=\"btn ghost\" href=\"/update\">Обновление прошивки</a>";

  page += "<form method=\"post\" action=\"/reset\" onsubmit=\"return confirm('Сбросить настройки ядра?')\">";
  page += "<button class=\"ghost\" type=\"submit\">Сбросить настройки</button></form>";

  if (pairingStatus_.length() > 0) {
    page += "<p class=\"note\">";
    page += pairingStatus_;
    page += "</p>";
  }

  page += "<p class=\"note\">Сопряжение: нажмите и удерживайте кнопку на блоке ядра, "
          "затем нажмите «Найти ядро сопряжением». Сеть сопряжения живёт около минуты.</p>";

  page += "</body></html>";

  return page;
}

String SetupPortal::buildStatusJson() const {
  const Settings &settings = Settings::instance();

  String json = "{\"event\":\"status\",\"data\":{";
  json += "\"fw\":\"" FIRMWARE_VERSION "\",";
  json += "\"link\":" + String(runtime.linkOnline ? 1 : 0) + ",";
  json += "\"core_ssid\":\"" + settings.ssid() + "\",";
  json += "\"core_host\":\"" + settings.host() + "\",";
  json += "\"servo\":" + String(runtime.servoPercent) + ",";
  json += "\"target\":" + String(runtime.targetAdc) + ",";
  json += "\"position\":" + String(runtime.positionAdc) + ",";
  json += "\"fault\":" + String(static_cast<unsigned>(runtime.fault)) + ",";
  json += "\"fault_attempts\":" + String(static_cast<unsigned>(runtime.faultAttempts)) + ",";
  json += "\"frames\":" + String(runtime.frames);
  json += "}}";

  return json;
}
