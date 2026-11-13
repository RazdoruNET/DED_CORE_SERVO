#include "net/Settings.h"

#include <esp_random.h>

#include "config/CONFIG.h"
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
constexpr const char *kKeyPairCode = "paircode";
constexpr const char *kKeyAdcMin = "adcmin";
constexpr const char *kKeyAdcMax = "adcmax";
constexpr const char *kKeyCalOk = "calok";
}  // namespace

Settings &Settings::instance() {
  static Settings settings;
  return settings;
}

void Settings::lock() const {
  if (mutex_ != nullptr) {
    xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
  }
}

void Settings::unlock() const {
  if (mutex_ != nullptr) {
    xSemaphoreGiveRecursive(mutex_);
  }
}

bool Settings::begin() {
  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateRecursiveMutex();
  }

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
  lock();
  const bool ready = ssid_.length() > 0 && password_.length() >= 8;
  unlock();

  return ready;
}

String Settings::ssid() const {
  lock();
  const String value = ssid_;
  unlock();

  return value;
}

String Settings::password() const {
  lock();
  const String value = password_;
  unlock();

  return value;
}

String Settings::host() const {
  lock();
  const String value = host_;
  unlock();

  return value;
}

uint16_t Settings::port() const {
  lock();
  const uint16_t value = port_;
  unlock();

  return value;
}

String Settings::token() const {
  lock();
  const String value = token_;
  unlock();

  return value;
}

String Settings::bssid() const {
  lock();
  const String value = bssid_;
  unlock();

  return value;
}

String Settings::pairCode() const {
  lock();
  const String value = pairCode_;
  unlock();

  return value;
}

uint8_t Settings::failedBoots() const {
  lock();
  const uint8_t value = failedBoots_;
  unlock();

  return value;
}

void Settings::saveCore(const String &ssid, const String &password, const String &host, uint16_t port) {
  lock();

  ssid_ = ssid;
  password_ = password;
  host_ = host;
  port_ = port;

  prefs_.putString(kKeySsid, ssid_);
  prefs_.putString(kKeyPass, password_);
  prefs_.putString(kKeyHost, host_);
  prefs_.putUShort(kKeyPort, port_);
  setFailedBoots(0);

  unlock();

  Serial.printf("SETTINGS: ядро '%s' (%s:%u) сохранено\n",
                ssid.c_str(), host.c_str(), static_cast<unsigned>(port));
}

void Settings::saveToken(const String &token) {
  lock();
  token_ = token;
  prefs_.putString(kKeyToken, token_);
  unlock();
}

void Settings::saveBssid(const String &bssid) {
  lock();
  bssid_ = bssid;
  prefs_.putString(kKeyBssid, bssid_);
  unlock();
}

void Settings::savePairCode(const String &code) {
  lock();
  pairCode_ = code;
  prefs_.putString(kKeyPairCode, pairCode_);
  unlock();
}

void Settings::clearCore() {
  lock();

  ssid_ = "";
  password_ = "";
  host_ = CORE_DEFAULT_HOST;
  port_ = CORE_DEFAULT_PORT;

  prefs_.remove(kKeySsid);
  prefs_.remove(kKeyPass);
  prefs_.remove(kKeyHost);
  prefs_.remove(kKeyPort);
  prefs_.remove(kKeyBssid);

  // Токен и код сопряжения привязаны к прежнему ядру: после сброса они только
  // путали бы новое сопряжение.
  token_ = "";
  pairCode_ = "";
  bssid_ = "";
  prefs_.remove(kKeyToken);
  prefs_.remove(kKeyPairCode);

  setFailedBoots(0);

  unlock();

  Serial.println("SETTINGS: настройки ядра сброшены");
}

void Settings::setFailedBoots(uint8_t value) {
  lock();

  if (value != failedBoots_) {
    failedBoots_ = value;
    prefs_.putUChar(kKeyFails, failedBoots_);
  }

  unlock();
}

bool Settings::calibrated() const {
  lock();
  const bool value = calibrated_;
  unlock();

  return value;
}

uint16_t Settings::adcMin() const {
  lock();
  const uint16_t value = adcMin_;
  unlock();

  return value;
}

uint16_t Settings::adcMax() const {
  lock();
  const uint16_t value = adcMax_;
  unlock();

  return value;
}

void Settings::saveCalibration(uint16_t min, uint16_t max) {
  lock();

  adcMin_ = min;
  adcMax_ = max;
  calibrated_ = true;

  prefs_.putUShort(kKeyAdcMin, adcMin_);
  prefs_.putUShort(kKeyAdcMax, adcMax_);
  prefs_.putUChar(kKeyCalOk, 1);

  unlock();

  Serial.printf("SETTINGS: крайние положения %u…%u ADC сохранены\n",
                static_cast<unsigned>(min), static_cast<unsigned>(max));
}

void Settings::load() {
  ssid_ = prefs_.getString(kKeySsid, "");
  password_ = prefs_.getString(kKeyPass, "");
  host_ = prefs_.getString(kKeyHost, CORE_DEFAULT_HOST);
  port_ = prefs_.getUShort(kKeyPort, CORE_DEFAULT_PORT);
  otaPassword_ = prefs_.getString(kKeyOtaPass, "");
  token_ = prefs_.getString(kKeyToken, "");
  bssid_ = prefs_.getString(kKeyBssid, "");
  pairCode_ = prefs_.getString(kKeyPairCode, "");
  failedBoots_ = prefs_.getUChar(kKeyFails, 0);

  adcMin_ = prefs_.getUShort(kKeyAdcMin, 0);
  adcMax_ = prefs_.getUShort(kKeyAdcMax, 0);
  calibrated_ = prefs_.getUChar(kKeyCalOk, 0) != 0 &&
                static_cast<int>(adcMax_) - static_cast<int>(adcMin_) >= CAL_MIN_SPAN;

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

  // 12 символов, а не 8: страница обновления отвечает и после закрытия портала,
  // и подобрать пароль из 40 бит по сети ядра было бы слишком просто.
  constexpr int kLength = 12;

  String password;
  password.reserve(kLength);

  for (int i = 0; i < kLength; ++i) {
    password += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }

  return password;
}
