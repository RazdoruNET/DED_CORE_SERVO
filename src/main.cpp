#include <Arduino.h>
#include <WiFi.h>

#include "app/ServoAxis.h"
#include "app/Status.h"
#include "app/Tasks.h"
#include "config/CONFIG.h"
#include "config/PROPS.h"
#include "net/CoreLink.h"
#include "net/PairingLink.h"
#include "net/Settings.h"
#include "net/SetupPortal.h"
#include "net/WifiLink.h"

RuntimeStatus runtime;

namespace {

unsigned long g_pairAttemptAt = 0;
unsigned long g_wifiDownSince = 0;

// explicitRequest — запуск из портала: ищем любое ядро. Автоматически после
// потери связи ищем только то ядро, с которым уже сопряжены (по BSSID), иначе
// чужая DED_PAIR_* увела бы модуль на поддельный блок.
void runPairing(bool explicitRequest) {
  CoreCredentials credentials;
  Settings &settings = Settings::instance();

  const String expectedBssid = explicitRequest ? String() : settings.bssid();
  const String pairCode = settings.pairCode();

  if (pairCode.length() < 8) {
    // Кода нет — сопряжение невозможно; без портала модуль не настроить.
    SetupPortal::instance().begin();
    SetupPortal::instance().setPairingStatus(
        "Нужен код сопряжения: откройте «Модули» в приложении блока, нажмите "
        "«Найти модуль» и введите показанный код в портале модуля.");
    return;
  }

  SetupPortal::instance().setPairingStatus("Ищу сеть сопряжения ядра (DED_PAIR_*)…");

  if (!PairingLink::instance().pair(credentials, pairCode, expectedBssid)) {
    // Код одноразовый: неудача чаще всего значит, что окно закрыто, а код уже
    // израсходован. Без портала модуль остался бы без линка до десяти
    // перезагрузок — открываем его сразу, чтобы владелец ввёл новый код.
    SetupPortal::instance().begin();
    SetupPortal::instance().setPairingStatus(
        "Сеть сопряжения не найдена или код не подошёл. Откройте в приложении "
        "«Модули» → «Найти модуль», введите новый код и повторите.");

    // Попытка сопряжения увела станцию с сети ядра: возвращаем её на место.
    if (settings.provisioned()) {
      WifiLink::instance().begin(settings.ssid(), settings.password());
    }

    return;
  }

  settings.saveCore(credentials.ssid,
                    credentials.password,
                    credentials.host,
                    credentials.port);
  settings.saveToken(credentials.token);
  settings.saveBssid(credentials.bssid);

  SetupPortal::instance().setPairingStatus("Ядро найдено: " + credentials.ssid + ". Перезагрузка…");

  delay(1500);
  ESP.restart();
}

// Сопряжение ищется только когда линка нет: сканирование сетей занимает
// секунды и на время связи с ядром не нужно.
void schedulePairing() {
  Settings &settings = Settings::instance();
  const unsigned long now = millis();

  if (WifiLink::instance().isConnected()) {
    g_wifiDownSince = now;
    return;
  }

  if (g_pairAttemptAt != 0 && static_cast<uint32_t>(now - g_pairAttemptAt) < PAIR_SCAN_INTERVAL_MS) {
    return;
  }

  const bool unprovisioned = !settings.provisioned();
  const bool linkLostLongEnough = settings.provisioned() &&
                                  static_cast<uint32_t>(now - g_wifiDownSince) >= PAIR_AFTER_LINK_LOSS_MS;

  if (!unprovisioned && !linkLostLongEnough) {
    return;
  }

  // Настроенный модуль без сохранённого BSSID не знает, какое ядро искать:
  // автоматическое сопряжение выключено, новое ядро выбирают из портала.
  if (!unprovisioned && settings.bssid().length() == 0) {
    return;
  }

  g_pairAttemptAt = now;
  runPairing(false);
}

// Ядро сменило имя или пароль точки доступа и прислало новые параметры до
// перезапуска сети: сохраняем и переподключаемся, не теряя связку.
void applyCoreCredentials() {
  String ssid;
  String password;
  String host;
  uint16_t port = 0;

  if (!CoreLink::instance().takeCredentials(ssid, password, host, port)) {
    return;
  }

  Settings::instance().saveCore(ssid, password, host, port);

  WifiLink::instance().applyCredentials(ssid, password);

  // В кадре мог прийти и новый адрес. Сокет, открытый на прежний, сам не
  // переедет: следующий loop() поднимет его по сохранённым настройкам.
  CoreLink::instance().rebind();
}

void updateLinkState() {
  const bool online = CoreLink::instance().frameFresh(LINK_TIMEOUT_MS);

  if (online == runtime.linkOnline) {
    return;
  }

  runtime.linkOnline = online;
  ServoAxis::instance().setLinkOnline(online);

  if (online) {
    Serial.println("CORE: телеметрия пошла");
    Settings::instance().setFailedBoots(0);

    // Линк есть — открытая точка доступа портала больше не нужна.
    if (SetupPortal::instance().active()) {
      SetupPortal::instance().stop();
    }
  } else {
    Serial.println("CORE: кадров нет, привод на тормозе");
  }
}

void applyTelemetry() {
  const CoreTelemetry &telemetry = CoreLink::instance().telemetry();

  if (!CoreLink::instance().frameFresh(LINK_TIMEOUT_MS)) {
    return;
  }

  // Выключенная серва в ядре означает уставку 0: привод возвращается в исходное
  // положение, а не остаётся там, где был в момент выключения.
  ServoAxis::instance().setPosition(telemetry.servoEnabled ? telemetry.servoPosition : 0);
}

void reportFaults() {
  CoreLink &core = CoreLink::instance();

  // Отчёт забираем только при живом сокете: takeFaultReport() сбрасывает
  // событие, и при закрытом WS ошибка ядру уже не уйдёт.
  if (!core.connected()) {
    return;
  }

  uint8_t code = 0;
  int target = 0;
  int position = 0;

  if (!ServoAxis::instance().takeFaultReport(code, target, position)) {
    return;
  }

  Serial.printf("SERVO: ошибка %u, цель %d, положение %d\n",
                static_cast<unsigned>(code), target, position);

  core.reportServoError(code, target, position);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  // До первого WiFi.mode(): режим хранения WiFi-конфига фиксируется при
  // инициализации стека. Портал поднимает AP раньше WifiLink, и без этого
  // параметры сопряжения писались бы в NVS.
  WiFi.persistent(false);

  // Заводская прошивка опускала частоту до 160 МГц: модуль стоит в закрытом
  // корпусе рядом с двигателем, лишний нагрев там ни к чему.
  setCpuFrequencyMhz(160);

  // Радио включаем сразу: без Wi-Fi esp_random() выдаёт предсказуемую
  // последовательность, а на нём строится пароль обновления прошивки.
  WiFi.mode(WIFI_STA);

  Serial.printf("\nDED CORE SERVO %s\n", FIRMWARE_VERSION);

  ServoAxis::instance().begin();

  // Контур — отдельной задачей: импульсы моста не должны ждать разбора кадров
  // и переподключения WiFi, а loop() занят сетью.
  if (xTaskCreatePinnedToCore(TaskServoCode, "ServoAxis", 4096, nullptr, 3, nullptr, 0) != pdPASS) {
    // Без задачи привод не стронется с тормоза: молчать об этом нельзя.
    Serial.println("SERVO: задача контура не создана, привод не работает");
  }

  Settings &settings = Settings::instance();
  settings.begin();

  // Счётчик неудачных стартов: модуль с неверным паролем ядра иначе остался бы
  // без сети и без портала настройки.
  const uint8_t failedBoots = settings.failedBoots();
  settings.setFailedBoots(static_cast<uint8_t>(failedBoots + 1));

  if (!settings.provisioned() || failedBoots >= PORTAL_AFTER_FAILED_BOOTS) {
    Serial.println("SETUP: открыт портал настройки");
    SetupPortal::instance().begin();
  }

  if (settings.provisioned()) {
    WifiLink::instance().begin(settings.ssid(), settings.password());
  }

  g_wifiDownSince = millis();
}

void loop() {
  WifiLink::instance().loop();
  SetupPortal::instance().loop();

  if (SetupPortal::instance().takePairingRequest()) {
    runPairing(true);
  }

  CoreLink &core = CoreLink::instance();

  if (WifiLink::instance().isConnected() && !core.started()) {
    core.begin(Settings::instance().host(), Settings::instance().port());
  }

  core.loop();

  applyCoreCredentials();

  updateLinkState();
  applyTelemetry();
  reportFaults();
  schedulePairing();
}
