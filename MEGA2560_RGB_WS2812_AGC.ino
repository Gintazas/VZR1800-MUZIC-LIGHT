#include <FastLED.h>

// ================= WS2812 CONFIG =================
#define LED_PIN     9
#define NUM_LEDS    48
#define HALF_LEDS   24
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
CRGB leds[NUM_LEDS];

// ================= AUDIO INPUT ===================
const int AUDIO_PIN_RIGHT = A8;
const int AUDIO_PIN_LEFT  = A9;

// ================= RGB MOSFETS ===================
const int RED_PIN   = 3;
const int GREEN_PIN = 5;
const int BLUE_PIN  = 6;
const int BOARD_LED = 13; 

// ================= ENVELOPES =====================
float bassEnv   = 0;
float midEnv    = 0;
float trebleEnv = 0;
float dcOffset  = 512;
float lastSignal = 0; 

// ================= AGC & SILENCE =================
float dynamicGain = 2.0;       // Pradinis stiprinimas
float maxObserved = 0;         // Didžiausias matytas signalas
const float SILENCE_THRESHOLD = 5.0; // Tylos riba (reguliuoti pagal triukšmą)
unsigned long lastSilenceTime = 0;

// ================= FILTER TUNING =================
const float BASS_ALPHA   = 0.07;
const float MID_ALPHA    = 0.12;   
const float TREBLE_ALPHA = 0.7;
const float DECAY        = 0.92;

// ================= TIMING ========================
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 2;
#define MODE_INTERVAL 15000UL // Pailginau iki 15s

// ================= VU STATE ======================
float vuDisplayed = 0;   
int   vuTarget = 0;
uint8_t vuMode = 0;
const uint8_t TOTAL_MODES = 7;

// ================= PRO PEAK HOLD =================
int peakHold = 0;
unsigned long peakHoldTimer = 0;
unsigned long lastPeakFall = 0;
#define PEAK_HOLD_TIME 150
#define PEAK_FALL_TIME 35

uint8_t wsBrightness = 200;

void setup() {
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(BOARD_LED, OUTPUT);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(wsBrightness);
  
  Serial.begin(115200);   
  Serial3.begin(115200); 

  for(int i=0; i<3; i++) {
    digitalWrite(BOARD_LED, HIGH); delay(100);
    digitalWrite(BOARD_LED, LOW);  delay(100);
  }
}

void loop() {
  if (millis() - lastUpdate < UPDATE_INTERVAL) return;
  lastUpdate = millis();

  // 1. Skaitome audio signalą
  int r = analogRead(AUDIO_PIN_RIGHT);
  int l = analogRead(AUDIO_PIN_LEFT);
  float audio = (r + l) * 0.5f;

  // 2. DC Offset ir signalo gryninimas
  dcOffset = dcOffset * 0.999f + audio * 0.001f;
  float signal = audio - dcOffset;
  float absSig = abs(signal);

  // 3. TYLOS DETEKCIJA (Silence Detect)
  bool isSilent = (absSig < SILENCE_THRESHOLD);
  
  // 4. AGC (Automatinis lygio reguliavimas)
  if (!isSilent) {
    // Stebime signalo piką AGC tikslais
    if (absSig > maxObserved) maxObserved = absSig;
    maxObserved *= 0.995f; // Lėtas max reikšmės „pamiršimas“

    // Adaptuojame gain (taikomės į tai, kad pikas būtų ~150-200 vienetų)
    float targetGain = 180.0f / (maxObserved + 10.0f);
    dynamicGain = dynamicGain * 0.98f + targetGain * 0.02f; // Švelnus perėjimas
    dynamicGain = constrain(dynamicGain, 0.5, 10.0); // Ribos
  }

  // 5. Dažnių filtravimas su Dynamic Gain
  float scaledSig = absSig * dynamicGain;
  
  bassEnv = bassEnv * (1.0f - BASS_ALPHA) + scaledSig * BASS_ALPHA;
  midEnv  = midEnv  * (1.0f - MID_ALPHA)  + scaledSig * MID_ALPHA;
  float diff = signal - lastSignal;
  lastSignal = signal;
  trebleEnv = trebleEnv * (1.0f - TREBLE_ALPHA) + abs(diff * dynamicGain) * TREBLE_ALPHA;

  bassEnv   *= DECAY;
  midEnv    *= DECAY;
  trebleEnv *= DECAY;

  // 6. Jei tyla - išvalome efektus (išjungia AGC „pumpavimą“)
  if (isSilent) {
    bassEnv *= 0.8f;
    midEnv *= 0.8f;
    trebleEnv *= 0.8f;
  }

  // RGB MOSFETS (Valdymas)
  analogWrite(RED_PIN,   (uint8_t)constrain((bassEnv   - 15) * 4, 0, 255));
  analogWrite(GREEN_PIN, (uint8_t)constrain((midEnv    - 5)  * 5, 0, 255));
  analogWrite(BLUE_PIN,  (uint8_t)constrain((trebleEnv - 40) * 3, 0, 255));

  // VU LEVEL skaičiavimas
  float vuLevel = (bassEnv * 1.2f + midEnv * 1.0f + trebleEnv * 0.8f);
  vuTarget = map(vuLevel, 0, 200, 0, HALF_LEDS);
  vuTarget = constrain(vuTarget, 0, HALF_LEDS);

  if (vuTarget > vuDisplayed) vuDisplayed = vuTarget;
  else vuDisplayed = vuDisplayed * 0.8f + vuTarget * 0.2f;

  // PEAK HOLD logika
  if (vuTarget > peakHold) {
    peakHold = vuTarget;
    peakHoldTimer = millis();
  } else if (millis() - peakHoldTimer > PEAK_HOLD_TIME) {
    if (millis() - lastPeakFall > PEAK_FALL_TIME) {
      lastPeakFall = millis();
      if (peakHold > 0) peakHold--;
    }
  }

  // Režimų perjungimas
  static unsigned long lastModeSwitch = 0;
  if (millis() - lastModeSwitch > MODE_INTERVAL) {
    lastModeSwitch = millis();
    vuMode = (vuMode + 1) % TOTAL_MODES;
  }

  // LED PIEŠIMAS
  FastLED.clear();
  
  if (!isSilent || vuDisplayed > 0.5) { // Piešiame tik jei yra garsas
    int disp = (int)vuDisplayed;
    int center = HALF_LEDS / 2;
    uint8_t baseHue = millis() >> 4;

    switch (vuMode) {
      case 0: case 1: case 4:
        for (int i = 0; i < disp; i++) {
          CRGB c = (vuMode == 4) ? CHSV(baseHue + i * 6, 255, 255) : CHSV(map(i, 0, HALF_LEDS - 1, 96, 0), 255, 255);
          if(i < HALF_LEDS) leds[i] = leds[NUM_LEDS - 1 - i] = c;
        }
        break;
      case 2: case 3: case 5:
        for (int i = 0; i < disp / 2; i++) {
          CRGB c = (vuMode == 5) ? CHSV(baseHue + i * 8, 255, 255) : CHSV(96, 255, 255);
          if(center + i < HALF_LEDS) {
            leds[center + i] = leds[center - 1 - i] = c;
            leds[NUM_LEDS - 1 - (center + i)] = leds[NUM_LEDS - 1 - (center - 1 - i)] = c;
          }
        }
        break;
      case 6: {
        for (int i = 0; i < NUM_LEDS; i++) leds[i] = CHSV(85, 200, 12);
        int bassLevel = constrain(map(bassEnv, 0, 100, 0, center), 0, center);
        for (int i = 0; i < bassLevel; i++) {
          leds[center + i] = leds[center - 1 - i] = CRGB::Red;
          leds[NUM_LEDS - 1 - (center + i)] = leds[NUM_LEDS - 1 - (center - 1 - i)] = CRGB::Red;
        }
      } break;
    }

    if (peakHold > 0 && peakHold <= HALF_LEDS) {
      leds[peakHold - 1] = CRGB::White;
      leds[NUM_LEDS - peakHold] = CRGB::White;
    }
  }

  FastLED.show();
}

