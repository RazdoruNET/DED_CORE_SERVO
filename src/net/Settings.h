#ifndef NET_SETTINGS_H
#define NET_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

// Настройки модуля в NVS: к какому ядру подключаться и пароль обновления.
//
// Пароль точки доступа ядра больше не зашит в прошивку DED CORE V4: он уникален
// для каждого блока, поэтому модуль хранит его у себя и получает при сопряжении
// (см. net/PairingLink.h).
class Settings {
public:
  static Settings &instance();

  bool begin();

  bool provisioned() const;
  const String &ssid() const { return ssid_; }
  const String &password() const { return password_; }
  const String &host() const { return host_; }
  uint16_t port() const { return port_; }

  // Токен доверенного модуля: выдаётся при сопряжении, по нему ядро присылает
  // новые параметры сети при смене пароля точки доступа.
  const String &token() const { return token_; }
  void saveToken(const String &token);

  void saveCore(const String &ssid, const String &password, const String &host, uint16_t port);
  void clearCore();

  uint8_t failedBoots();
  void setFailedBoots(uint8_t value);

  const String &otaPassword() const { return otaPassword_; }

private:
  Settings() = default;

  void load();
  static String makeOtaPassword();

  Preferences prefs_;

  String ssid_;
  String password_;
  String host_;
  uint16_t port_ = 0;
  String token_;
  String otaPassword_;
};

#endif // NET_SETTINGS_H
