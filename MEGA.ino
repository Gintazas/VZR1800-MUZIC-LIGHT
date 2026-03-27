#include <FastLED.h>

#define LED_PIN 9
#define NUM_LEDS 48
CRGB leds[NUM_LEDS];
const int RED_PIN = 3, GREEN_PIN = 5, BLUE_PIN = 6;

void setup() {
  Serial.begin(115200);   // Monitorius per USB
  Serial3.begin(115200);  // Ryšys su ESP (RXD3/TXD3)
  
  pinMode(RED_PIN, OUTPUT); pinMode(GREEN_PIN, OUTPUT); pinMode(BLUE_PIN, OUTPUT);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
}

void loop() {
  // 1. Skaitome audio (supaprastintas pavyzdys testui)
  int raw = analogRead(A8);
  uint8_t vu = map(constrain(raw, 512, 800), 512, 800, 0, 24);

  // 2. Valdome RGB MOSFET
  analogWrite(RED_PIN, vu * 10);
  
  // 3. Valdome savo LED juostą
  FastLED.clear();
  for(int i=0; i<vu; i++) leds[i] = leds[47-i] = CRGB::Blue;
  FastLED.show();

  // 4. SIUNČIAME Į ESP (Header + VU + Brightness)
  Serial3.write(0xAA); // Kontrolinis baitas
  Serial3.write(vu);
  Serial3.write((uint8_t)150); // Brightness

  // DEBUG Monitoriuje
  Serial.print("MEGA SIUNČIA -> VU: "); Serial.println(vu);
  
  delay(30); 
}
