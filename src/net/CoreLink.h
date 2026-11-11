#ifndef NET_CORE_LINK_H
#define NET_CORE_LINK_H

#include <Arduino.h>
#include <WebSocketsClient.h>

// Кадр телеметрии ядра в той части, которая нужна приводу.
struct CoreTelemetry {
  bool valid = false;
  bool servoEnabled = false;
  int servoPosition = 0;  // 0…255, шкала DED CORE V4
  int servoType = 0;
  int servoRpm = 0;
  unsigned long frames = 0;
  unsigned long lastFrameMs = 0;
};

// WebSocket-клиент к ядру.
//
// Ядро рассылает всем клиентам кадр {"event":"message","data":{…}} и принимает
// команды. Модулю достаточно читать servo/servo_position и отправлять кадр
// ошибки привода; остальные события (ping/pong, cart, map_*) его не касаются.
class CoreLink {
public:
  static CoreLink &instance();

  void begin(const String &host, uint16_t port);
  void loop();

  bool started() const { return started_; }
  bool connected();
  const CoreTelemetry &telemetry() const { return telemetry_; }

  // true, если последний кадр пришёл не позже timeoutMs назад.
  bool frameFresh(uint32_t timeoutMs) const;

  void reportServoError(uint8_t code, int target, int position);

private:
  CoreLink() = default;

  static void onEvent(WStype_t type, uint8_t *payload, size_t length);
  void handleEvent(WStype_t type, uint8_t *payload, size_t length);
  void handleFrame(const char *payload, size_t length);
  void sendText(String &frame);

  WebSocketsClient socket_;
  CoreTelemetry telemetry_;

  String host_;
  uint16_t port_ = 0;

  bool started_ = false;
  bool connected_ = false;
};

#endif // NET_CORE_LINK_H
