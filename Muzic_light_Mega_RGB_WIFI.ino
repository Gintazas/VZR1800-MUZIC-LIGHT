#include <FastLED.h>

// ================= WIFI =================
#define WIFI_BAUD 115200
#define UDP_PORT 4210
#define BROADCAST_IP "192.168.1.255"   // sends to BOTH receivers

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

// ================= ENVELOPES =====================
float bassEnv=0, midEnv=0, trebleEnv=0, dcOffset=512;

// ================= FILTER ========================
const float BASS_ALPHA=0.07, MID_ALPHA=1.02, TREBLE_ALPHA=0.7, DECAY=0.92;

// ================= TIMING ========================
unsigned long lastUpdate=0;
const unsigned long UPDATE_INTERVAL=2;
#define MODE_INTERVAL 10000UL

float lastSignal=0;

int vuDisplayed=0, vuTarget=0;
uint8_t vuMode=0;
const uint8_t TOTAL_MODES=7;

int peakHold=0;
unsigned long peakHoldTimer=0, lastPeakFall=0;

#define PEAK_HOLD_TIME 150
#define PEAK_FALL_TIME 35

uint8_t wsBrightness=50;
float bassGain=4.0;

// =================================================
// WIFI INIT
// =================================================
void wifiInit() {
  Serial.println("AT");
  delay(500);
  Serial.println("AT+CWMODE=1");
  delay(500);

  Serial.println("AT+CWJAP=\"YOUR_WIFI\",\"PASSWORD\""); // <-- change
  delay(5000);

  Serial.print("AT+CIPSTART=\"UDP\",\"");
  Serial.print(BROADCAST_IP);
  Serial.print("\",");
  Serial.println(UDP_PORT);
  delay(500);
}

// =================================================
// SEND DATA TO BOTH RECEIVERS
// =================================================
void wifiSend(uint8_t hue) {
  char buf[32];

  sprintf(buf,"%d,%d,%d,%d\n",
          vuDisplayed,
          peakHold,
          vuMode,
          hue);

  Serial.print("AT+CIPSEND=");
  Serial.println(strlen(buf));
  delay(2);
  Serial.print(buf);
}

// =================================================
void setup() {

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(wsBrightness);

  Serial.begin(WIFI_BAUD);
  delay(2000);

  wifiInit();   // <<< start WiFi
}

// =================================================
void loop() {

  if (millis() - lastUpdate < UPDATE_INTERVAL) return;
  lastUpdate = millis();

  // ---------- AUDIO ----------
  int r = analogRead(AUDIO_PIN_RIGHT);
  int l = analogRead(AUDIO_PIN_LEFT);
  float audio = (r + l) * 0.5;

  dcOffset = dcOffset * 0.999 + audio * 0.001;
  float signal = audio - dcOffset;

  bassEnv = bassEnv * (1.0 - BASS_ALPHA) + abs(signal) * BASS_ALPHA;
  midEnv  = midEnv  * (1.0 - MID_ALPHA)  + abs(signal) * MID_ALPHA;

  float diff = signal - lastSignal;
  lastSignal = signal;
  trebleEnv = trebleEnv * (1.0 - TREBLE_ALPHA) + abs(diff) * TREBLE_ALPHA;

  bassEnv*=DECAY; midEnv*=DECAY; trebleEnv*=DECAY;

  // ---------- RGB ----------
  analogWrite(RED_PIN,   constrain((bassEnv-30)*56,0,255));
  analogWrite(GREEN_PIN, constrain((midEnv-2)*10,0,255));
  analogWrite(BLUE_PIN,  constrain((trebleEnv-100)*12,0,255));

  float vuLevel = bassEnv*2.5 + midEnv*2.0 + trebleEnv*1.5;
  vuLevel = constrain(vuLevel,0,650);
  vuTarget = map(vuLevel,0,650,0,HALF_LEDS);

  vuDisplayed = (vuTarget>vuDisplayed) ? vuTarget : vuDisplayed*0.85+vuTarget*0.15;

  if(vuTarget>peakHold){ peakHold=vuTarget; peakHoldTimer=millis(); }
  else if(millis()-peakHoldTimer>PEAK_HOLD_TIME && millis()-lastPeakFall>PEAK_FALL_TIME){
    lastPeakFall=millis();
    if(peakHold>0) peakHold--;
  }

  static unsigned long lastModeSwitch=0;
  if(millis()-lastModeSwitch>MODE_INTERVAL){
    lastModeSwitch=millis();
    vuMode=(vuMode+1)%TOTAL_MODES;
  }

  FastLED.clear();

  uint8_t baseHue = millis()>>4;

  // (your original LED drawing remains unchanged)
  for(int i=0;i<vuDisplayed;i++)
    leds[i]=leds[NUM_LEDS-1-i]=CHSV(baseHue+i*5,255,255);

  if(peakHold>0){
    leds[peakHold-1]=CRGB::White;
    leds[NUM_LEDS-peakHold]=CRGB::White;
  }

  FastLED.show();

  // ===== send to BOTH wifi receivers =====
  wifiSend(baseHue);
}