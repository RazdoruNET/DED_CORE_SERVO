#ifndef NET_SETTINGS_H
#define NET_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Настройки модуля в NVS: к какому ядру подключаться и пароль обновления.
//
// Пароль точки доступа ядра больше не зашит в прошивку DED CORE V4: он уникален
// для каждого блока, поэтому модуль хранит его у себя и получает при сопряжении
// (см. net/PairingLink.h).
//
// Настройки меняет loop() (кадр module_credentials) и портал настройки, а он
// работает в задаче AsyncTCP. Строки поэтому отдаются копией под мьютексом:
// иначе сборка страницы читала бы буфер String, который в этот момент
// перевыделяет другая задача.
class Settings {
public:
  static Settings &instance();

  bool begin();

  bool provisioned() const;
  String ssid() const;
  String password() const;
  String host() const;
  uint16_t port() const;

  // Токен доверенного модуля: выдаётся при сопряжении, по нему ядро присылает
  // новые параметры сети при смене пароля точки доступа.
  String token() const;
  void saveToken(const String &token);

  // BSSID точки сопряжения ядра. По нему автоматическое пересопряжение находит
  // только своё ядро, а не любую сеть DED_PAIR_* в радиусе.
  String bssid() const;
  void saveBssid(const String &bssid);

  // Код сопряжения из приложения блока. Сеть DED_PAIR_* закрыта WPA2-паролем,
  // выведенным из этого кода: без него модуль не подключится и не получит пароль ядра.
  String pairCode() const;
  void savePairCode(const String &code);

  void saveCore(const String &ssid, const String &password, const String &host, uint16_t port);
  void clearCore();

  // Крайние положения привода, найденные при первом включении. Привязка к
  // механике, а не к ядру: при сбросе настроек ядра калибровка сохраняется.
  bool calibrated() const;
  uint16_t adcMin() const;
  uint16_t adcMax() const;
  void saveCalibration(uint16_t min, uint16_t max);

  uint8_t failedBoots() const;
  void setFailedBoots(uint8_t value);

  // Пароль обновления создаётся в begin() и больше не меняется, а ElegantOTA
  // хранит указатель на строку: отдаём ссылку, а не копию.
  const String &otaPassword() const { return otaPassword_; }

private:
  Settings() = default;

  void load();
  static String makeOtaPassword();

  void lock() const;
  void unlock() const;

  Preferences prefs_;

  // Рекурсивный: saveCore() и clearCore() вызывают setFailedBoots().
  mutable SemaphoreHandle_t mutex_ = nullptr;

  String ssid_;
  String password_;
  String host_;
  uint16_t port_ = 0;
  String token_;
  String bssid_;
  String pairCode_;
  String otaPassword_;

  bool calibrated_ = false;
  uint16_t adcMin_ = 0;
  uint16_t adcMax_ = 0;

  // Кэш счётчика: NVS пишется только при изменении значения, иначе «мигающий»
  // линк изнашивал бы flash на каждом переходе в online.
  uint8_t failedBoots_ = 0;
};

#endif // NET_SETTINGS_H
