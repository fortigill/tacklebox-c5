#pragma once
// Web setup: a small web page on the device (http://<ip>/ or <hostname>.local)
// showing the About information, and a form to enter the YoLink keys by
// copy and paste. Saving needs the 6-digit pairing code shown on the device's
// Web setup screen, which is valid while that screen is open (10 min at most).

#include <Arduino.h>

namespace portal {

void loop();                 // call from loop(): starts the server once Wi-Fi is up, serves requests
bool running();
String baseUrl();            // "http://192.168.1.50" ("" while offline)

void startPairing();         // new code, valid for 10 minutes
void stopPairing();
bool pairingActive();
String pairingCode();        // "123456"
uint32_t pairingSecondsLeft();
String setupUrl();           // QR target: http://<ip>/yolink?code=123456
uint32_t savedAt();          // changes with every successful save from the web

}  // namespace portal
