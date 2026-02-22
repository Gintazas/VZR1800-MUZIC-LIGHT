#include <ESP8266WiFi.h>
#include <espnow.h>
#include <FastLED.h>

#define LED_PIN       2   // D4
#define MOSFET_PIN    5   // D1
#define INTERNAL_LED  2
#define NUM_LEDS      48
#define HALF_LEDS     24

CRGB leds[NUM_LEDS];
struct DataPacket { int vu = 0; int peak = 0; int mode = 0; } myData;
bool firstContact = false;
unsigned long lastReceived = 0;

void onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  lastReceived = millis();
  memcpy(&myData, incomingData, sizeof(myData));
  if (!firstContact) firstContact = true;

  // LED piešimas tik gavus realius duomenis
  FastLED.clear();
  int currentVu = constrain(myData.vu, 0, HALF_LEDS);
  uint8_t hue = millis() >> 4;

  for (int i = 0; i < currentVu; i++) {
    CRGB c = (myData.mode < 2) ? CHSV(map(i, 0, HALF_LEDS, 96, 0), 255, 255) : CHSV(hue + i*5, 255, 255);
    leds[i] = leds[NUM_LEDS - 1 - i] = c;
  }
  if (myData.peak > 0 && myData.peak <= HALF_LEDS) {
    leds[myData.peak - 1] = leds[NUM_LEDS - myData.peak] = CRGB::White;
  }
  FastLED.show();

  // MOSFET valdymas
  int bass = map(currentVu, 0, HALF_LEDS, 0, 255);
  analogWrite(MOSFET_PIN, (currentVu < 12) ? 0 : constrain(bass * 1.5, 0, 255));
}

void setup() {
  // 1. KRITIŠKA: Iškart inicializuojame FastLED ir DU KARTUS išvalome
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.clear(true); // Išvalo buferį ir išsiunčia "juodą" signalą
  FastLED.show();
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Papildomas bandymas "priversti" pin'ą tylėti

  Serial.begin(115200);
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);
  analogWriteRange(255);

  // 2. WiFi paruošimas be mirksėjimo
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) { ESP.restart(); }

  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataRecv);
  
  // 3. Dar kartą išvalome po visų WiFi inicializacijų
  FastLED.clear(true);
  FastLED.show();
  
  lastReceived = millis();
}

void loop() {
  if (firstContact && (millis() - lastReceived > 5000)) {
    // Vietoj restart, tiesiog išvalom LED ir laukiam naujo ryšio
    firstContact = false;
    FastLED.clear(true);
    FastLED.show();
    digitalWrite(MOSFET_PIN, LOW);
  }
}
