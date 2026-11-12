#include "net/Settings.h"

#include <esp_random.h>

#include "config/PROPS.h"

namespace {
constexpr const char *kNamespace = "servo";

// Имена ключей короче 15 символов: столько вмещает NVS. В заводской прошивке
// ключ "count_run_succes" (16 символов) молча не сохранялся, и защита от
// bootloop не работала.
constexpr const char *kKeySsid = "ssid";
constexpr const char *kKeyPass = "pass";
constexpr const char *kKeyHost = "host";
constexpr const char *kKeyPort = "port";
constexpr const char *kKeyFails = "fails";
constexpr const char *kKeyOtaPass = "otapw";
constexpr const char *kKeyToken = "mtok";
constexpr const char *kKeyBssid = "bssid";
}  // namespace

Settings &Settings::instance() {
  static Settings settings;
  return settings;
}

bool Settings::begin() {
  const bool nvsReady = prefs_.begin(kNamespace, false);

  if (nvsReady) {
    load();
  } else {
    Serial.println("SETTINGS: NVS недоступна");
  }

  // Пароль OTA не должен оставаться пустым даже при сбое NVS: ElegantOTA с
  // пустым паролем отключает авторизацию, а точка доступа портала открытая.
  if (otaPassword_.length() == 0) {
    otaPassword_ = makeOtaPassword();

    if (nvsReady) {
      prefs_.putString(kKeyOtaPass, otaPassword_);
    }

    // Пароль печатаем один раз: портал открытый, а обновление прошивки —
    // единственное действие, которым можно испортить модуль.
    Serial.printf("SETTINGS: пароль обновления прошивки: %s\n", otaPassword_.c_str());
  }

  return nvsReady;
}

bool Settings::provisioned() const {
  return ssid_.length() > 0 && password_.length() >= 8;
}

void Settings::saveCore(const String &ssid, const String &password, const String &host, uint16_t port) {
  ssid_ = ssid;
  password_ = password;
  host_ = host;
  port_ = port;

  prefs_.putString(kKeySsid, ssid_);
  prefs_.putString(kKeyPass, password_);
  prefs_.putString(kKeyHost, host_);
  prefs_.putUShort(kKeyPort, port_);
  setFailedBoots(0);

  Serial.printf("SETTINGS: ядро '%s' (%s:%u) сохранено\n",
                ssid_.c_str(), host_.c_str(), static_cast<unsigned>(port_));
}

void Settings::saveToken(const String &token) {
  token_ = token;
  prefs_.putString(kKeyToken, token_);
}

void Settings::saveBssid(const String &bssid) {
  bssid_ = bssid;
  prefs_.putString(kKeyBssid, bssid_);
}

void Settings::clearCore() {
  ssid_ = "";
  password_ = "";
  host_ = CORE_DEFAULT_HOST;
  port_ = CORE_DEFAULT_PORT;

  prefs_.remove(kKeySsid);
  prefs_.remove(kKeyPass);
  prefs_.remove(kKeyHost);
  prefs_.remove(kKeyPort);
  prefs_.remove(kKeyBssid);
  bssid_ = "";

  setFailedBoots(0);

  Serial.println("SETTINGS: настройки ядра сброшены");
}

void Settings::setFailedBoots(uint8_t value) {
  if (value == failedBoots_) {
    return;
  }

  failedBoots_ = value;
  prefs_.putUChar(kKeyFails, failedBoots_);
}

void Settings::load() {
  ssid_ = prefs_.getString(kKeySsid, "");
  password_ = prefs_.getString(kKeyPass, "");
  host_ = prefs_.getString(kKeyHost, CORE_DEFAULT_HOST);
  port_ = prefs_.getUShort(kKeyPort, CORE_DEFAULT_PORT);
  otaPassword_ = prefs_.getString(kKeyOtaPass, "");
  token_ = prefs_.getString(kKeyToken, "");
  bssid_ = prefs_.getString(kKeyBssid, "");
  failedBoots_ = prefs_.getUChar(kKeyFails, 0);

  if (host_.length() == 0) {
    host_ = CORE_DEFAULT_HOST;
  }

  if (port_ == 0) {
    port_ = CORE_DEFAULT_PORT;
  }
}

String Settings::makeOtaPassword() {
  // Без похожих символов: пароль набирают вручную с экрана или из лога.
  static const char alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";

  String password;
  password.reserve(8);

  for (int i = 0; i < 8; ++i) {
    password += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }

  return password;
}
