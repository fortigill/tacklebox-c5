#pragma once

// Over-the-air firmware updates (ArduinoOTA / espota). Starts once Wi-Fi is
// connected; the device answers as <hostname>.local and requires the password
// set by OTA_PASSWORD in platformio.ini.
namespace ota {

void loop();               // call from loop()
bool ready();
const char *hostname();

}  // namespace ota
