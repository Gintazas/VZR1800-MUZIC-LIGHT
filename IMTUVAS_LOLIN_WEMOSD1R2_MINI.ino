#include <ESP8266WiFi.h>
#include <espnow.h>
#include <FastLED.h>

#define LED_PIN 2
#define MOSFET_PIN 5   // Pridėta: MOSFET D1
#define NUM_LEDS 48
#define HALF (NUM_LEDS / 2)

CRGB leds[NUM_LEDS];

typedef struct { uint8_t vu; uint8_t br; } Packet;
Packet pkt;

// ---- REGULIAVIMAI ----
#define MIN_BRIGHTNESS 60
#define MAX_BRIGHTNESS 255
#define SENSITIVITY 4.0
#define MAX_VU 24

// ---- AUTO GAIN ----
float gain = 1.0;
uint8_t lastVu = 0;
unsigned long lastReceived = 0; // Pridėta ryšio sekimui
// ---4 efektas----
float peakPos = 0;
float peakVelocity = 0;
unsigned long lastPeakUpdate = 0;
// ---- EFEKTAI ----
uint8_t effect = 0;
unsigned long lastSwitch = 0;
#define EFFECT_INTERVAL 10000 

// ---- BASS DETECT ----
#define BASS_THRESHOLD 11

void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  lastReceived = millis(); // Atnaujinam laiką gavus duomenis
  memcpy(&pkt, data, sizeof(pkt));
}

// ---- HELPERS ----
int getVU() {
  if (pkt.vu > lastVu) gain *= 1.05;
  else gain *= 0.98;

  gain = constrain(gain, 0.5, 5.0);
  lastVu = pkt.vu;

  int vuScaled = pkt.vu * SENSITIVITY * gain;
  return constrain(vuScaled, 0, MAX_VU);
}

void centerBar(int vu) {
  for (int i = 0; i < vu; i++) {
    int l = i;
    int r = NUM_LEDS - 1 - i;
    leds[l] = leds[r] = CHSV(map(i,0,MAX_VU,0,160),255,255);
  }
}

// ---- EFEKTAI ----
void effectVU(int vu) { centerBar(vu); }

void effectCometSpark(int vu) {
  // ---- REGULIAVIMAS ----
  static float pos = 0;
  float speed = 0.2;      // Pastovus greitis
  int direction = 1;      // Kitam imtuvui pakeisk į -1

  // 1. Fonas (raudonas 5 ryškumas)
  fill_solid(leds, NUM_LEDS, CRGB(5, 0, 0));

  // 2. Judėjimas
  pos += (speed * direction);
  if (pos >= NUM_LEDS) pos -= NUM_LEDS;
  if (pos < 0) pos += NUM_LEDS;

  // 3. Kometos piešimas (Galva 2 led + Uodega 5 led)
  int head = (int)pos;
  for (int i = 0; i < 7; i++) {
    int idx = (head - (i * direction) + NUM_LEDS) % NUM_LEDS;
    if (i < 2) {
      leds[idx] = CRGB(255, 0, 0); 
    } else {
      uint8_t br = map(i, 2, 6, 150, 20); 
      leds[idx] = CRGB(br, 0, 0);
    }
  }

  // 4. Random sparks (Dvigubi LED, retais blyksniais)
  static int sparkPos = -1;
  static uint8_t sparkType = 0;
  static uint8_t sparkLife = 0;

  if (sparkLife == 0 && random16(1000) < 28) { 
    sparkPos = random8(NUM_LEDS - 1); // -1, kad tilptų 2 led
    sparkType = random8(3);
    sparkLife = 15; 
  }

  if (sparkLife > 0) {
    CRGB sCol;
    if (sparkType == 0) sCol = CRGB::White;
    else if (sparkType == 1) sCol = CRGB::Black;
    else sCol = CRGB(0, 0, 150);

    // Užtepam 2 LED
    leds[sparkPos] = sCol;
    leds[sparkPos + 1] = sCol;
    
    sparkLife--;
  }
}


// 2 - peak hold (su smaragdiniu stulpeliu)
void effectPeak(int vu) {
  static int peak = 0;
  peak = max(peak - 1, vu);

  // Apibrėžiame smaragdinę spalvą
  CRGB emeraldColor = CRGB(0, 255, 120);

  // Nupiešiame pagrindinį stulpelį (bar) smaragdine spalva
  for (int i = 0; i < vu; i++) {
    int l = i;
    int r = NUM_LEDS - 1 - i;
    leds[l] = leds[r] = emeraldColor;
  }

  // Nupiešiame Peak tašką (paliekam jį baltą arba galime padaryti kitokį)
  int lPeak = peak;
  int rPeak = NUM_LEDS - 1 - peak;
  if (lPeak < HALF) {
    leds[lPeak] = leds[rPeak] = CRGB::White; // Jei nori, čia irgi gali įrašyti emeraldColor
  }
}


void effectWave(int vu) {
  for (int i=0;i<NUM_LEDS;i++) leds[i] = CHSV((i*10 + millis()/5),255,vu*10);
}

void effectGreenBar(int vu) {
  // 1. Backlight - silpna raudona visai juostai
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(5, 0, 0); 

  // 2. Padidiname jautrumą būtent šiam efektui (pvz. padauginam iš 1.2)
  int highSensVu = constrain(vu * 1.2, 0, MAX_VU);
  int fillSize = map(highSensVu, 0, MAX_VU, 0, 12); 

  for (int i = 0; i < fillSize; i++) {
    leds[i] = CRGB::Green;               
    leds[NUM_LEDS - 1 - i] = CRGB::Green; 
    leds[HALF - 1 - i] = CRGB::Green;     
    leds[HALF + i] = CRGB::Green;         
  }

  // 4. Peak geltona spalva su užlaikymu ir kritimu
  static float peakPos = 0;
  static float peakVel = 0;
  
  if (fillSize >= peakPos) {
    peakPos = fillSize;
    peakVel = -0.5; // "Atšokimas" į viršų, kad būtų gyviau
  } else {
    peakVel += 0.08; // Krenta šiek tiek greičiau
    peakPos -= peakVel;
  }
  if (peakPos < 0) peakPos = 0;

  int p = round(peakPos);
  if (p > 0 && p <= 12) {
    // Sukuriam spalvą rankiniu būdu (R, G, B)
    CRGB emeraldColor = CRGB(0, 255, 120); 

    leds[p - 1] = emeraldColor;
    leds[NUM_LEDS - p] = emeraldColor;
    leds[HALF - p] = emeraldColor;
    leds[HALF + p - 1] = emeraldColor;
  }
}

void effectBassColor(int vu) {
  if (vu > BASS_THRESHOLD) fill_solid(leds, NUM_LEDS, CHSV(random8(),255,255));
}

void effectPulse(int vu) {
  for (int i = 0; i < vu; i++) leds[i] = leds[NUM_LEDS - 1 - i] = CHSV(millis()/5,255,255);
}

void effectRainbowBass(int vu) {
  for (int i=0;i<NUM_LEDS;i++) leds[i] = CHSV(i*5 + millis()/10,255,100);
  if (vu > BASS_THRESHOLD) fill_solid(leds, NUM_LEDS, CRGB::White);
}
//----Efektu perjungiklis---
void runEffect(int vu) {
  switch(effect) {
    case 0: effectVU(vu); break;
    case 1: effectCometSpark(vu); break;
    case 2: effectPeak(vu); break;
    case 3: effectWave(vu); break;
    case 4: effectGreenBar(vu); break;
    case 5: effectBassColor(vu); break;
    case 6: effectPulse(vu); break;
    case 7: effectRainbowBass(vu); break;
  }
}
// ---- SERIAL INFO ----
void printEffect() {
  Serial.print("EFEKTAS: ");
  switch(effect) {
    case 0: Serial.println("0 - VU"); break;
    case 1: Serial.println("1 - Comet Spark"); break;
    case 2: Serial.println("2 - Peak"); break;
    case 3: Serial.println("3 - Wave"); break;
    case 4: Serial.println("4 - Green Bar VU"); break;
    case 5: Serial.println("5 - Bass Color Flash"); break;
    case 6: Serial.println("6 - Pulse"); break;
    case 7: Serial.println("7 - Rainbow + Bass"); break;
  }
}
void setup() {
  Serial.begin(115200);
  
  // MOSFET inicializacija
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, LOW);
  analogWriteRange(255);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.clear(true);
  FastLED.show();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) return;
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataRecv);
  printEffect();
  lastReceived = millis();
}

void loop() {
  // Saugiklis: jei duomenų nėra > 5s, viską išjungiam
  if (millis() - lastReceived > 5000) {
    FastLED.clear(true);
    FastLED.show();
    digitalWrite(MOSFET_PIN, LOW);
    delay(100);
    return;
  }

  uint8_t brightness = constrain(pkt.br, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
  FastLED.setBrightness(brightness);

  int vu = getVU();

  // MOSFET valdymas pagal VU lygį (identiškas tavo logikai)
  int bassVal = map(vu, 0, MAX_VU, 0, 255);
  analogWrite(MOSFET_PIN, (vu < 12) ? 0 : constrain(bassVal * 1.5, 0, 255));

  FastLED.clear();
  runEffect(vu);
  FastLED.show();

  if (millis() - lastSwitch > EFFECT_INTERVAL) {
    effect++;
    if (effect > 7) effect = 0;
    printEffect();
    lastSwitch = millis();
  }

  delay(5);
}
