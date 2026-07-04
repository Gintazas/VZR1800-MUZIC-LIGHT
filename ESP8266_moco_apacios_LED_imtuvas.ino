#include <ESP8266WiFi.h>
#include <espnow.h>
#include <FastLED.h>

#define LED_PIN       2   // D4
#define LED_PIN_D2    4   // D2
#define LED_PIN_D5    14  // D5
#define NUM_LEDS      42
#define HALF_LEDS     21

CRGB leds[NUM_LEDS];
CRGB ledsD2[NUM_LEDS];
CRGB ledsD5[NUM_LEDS];

struct DataPacket { int vu = 0; int peak = 0; int mode = 0; } myData;
bool firstContact = false;
unsigned long lastReceived = 0;

// ==========================================
//           BASS SPROGIMO REGULIAVIMAS
// ==========================================
#define BASS_THRESHOLD 150  // 1. JAUTRUMAS: Keisk pagal siųstuvą (kuo mažesnis, tuo lengviau sprogsta)
#define EXPLOSION_WIDTH 16  // 2. PLOTIS: Kelių LED pločio bus raudonas sprogimas (pvz. 10 led)
#define FADE_SPEED 20       // 3. GREITIS: Kaip staigiai užgęsta raudona spalva (didesnis skaičius = gęsta greičiau)

int bassIntensity = 0;      // Sprogimo ryškumas laike

// Spalvos
CRGB backgroundColor = CRGB(0, 0, 50);    // Mėlyna (50)
CRGB waveColor = CRGB(0, 250, 154);      // Smaragdinė (250)
int wavePosition = 0;

void onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  lastReceived = millis();
  memcpy(&myData, incomingData, sizeof(myData));
  if (!firstContact) firstContact = true;

  // Sprogimas suveikia nuo mikrofono signalo
  if (myData.vu > BASS_THRESHOLD) {
    bassIntensity = 255; 
  }
}

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<WS2812B, LED_PIN_D2, GRB>(ledsD2, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<WS2812B, LED_PIN_D5, GRB>(ledsD5, NUM_LEDS).setCorrection(TypicalLEDStrip);
  
  FastLED.setBrightness(150);

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != 0) { ESP.restart(); }
  esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
  esp_now_register_recv_cb(onDataRecv);
  
  lastReceived = millis();
}

void loop() {
  // Sprogimo raudonos spalvos bliskinis užgesimas laike
  EVERY_N_MILLISECONDS(15) {
    if (bassIntensity > 0) {
      bassIntensity = qsub8(bassIntensity, FADE_SPEED); 
    }
  }

  // 1. Smaragdinės bangos judėjimas fone - VEIKIA VISADA BE APRIBOJIMŲ
  EVERY_N_MILLISECONDS(100) {
    wavePosition++;
    if (wavePosition >= 63 + 20) wavePosition = 0; 
  }

  // 2. Bangos ir fono piešimas per visas juostas (63 LED ilgis į abi puses)
  for (int offset = 0; offset < 63; offset++) {
    
    CRGB finalColor = backgroundColor;
    int waveIndex = wavePosition - offset;

    if (waveIndex >= 0 && waveIndex < 20) {
      CRGB emerald = waveColor;
      CRGB darkGreen = CRGB(0, 50, 0); 

      uint8_t blendAmount = map(waveIndex, 0, 19, 0, 255);
      CRGB wavePixelColor = blend(emerald, darkGreen, blendAmount);

      uint8_t tailFade = waveIndex * 12; 
      wavePixelColor.fadeToBlackBy(tailFade);

      if (offset < 5) {
        uint8_t introFade = map(offset, 0, 5, 50, 255);
        wavePixelColor.nscale8_video(introFade);
      }

      uint8_t backgroundBlend = map(waveIndex, 0, 19, 0, 255);
      finalColor = blend(wavePixelColor, backgroundColor, backgroundBlend);
    }

    // Pozicijų skaičiavimas atskiroms juostoms
    int posL = (HALF_LEDS - 1) - offset;
    int posR = HALF_LEDS + offset;

    // KAIRĖ PUSĖ (D4 juosta nuo centro į kairę)
    if (posL >= 0) {
      leds[posL] = finalColor; 
      // BASS SPROGIMAS: Užklojame raudona spalva D4 centro kairėje
      if (bassIntensity > 0 && offset < EXPLOSION_WIDTH) {
        CRGB redExplosion = CRGB(bassIntensity, 0, 0);
        uint8_t fadeAmount = map(offset, 0, EXPLOSION_WIDTH - 1, 0, 255); 
        leds[posL] = blend(redExplosion, leds[posL], fadeAmount);
      }
    } else {
      // PEREINAME Į D2 JUOSTĄ (Skaičiuojame atstumą nuo vidurio)
      int posD2 = offset - HALF_LEDS; 
      if (posD2 < NUM_LEDS) {
        ledsD2[posD2] = finalColor;
        
        // SPROGIMAS PER VIDURĮ: Skaičiuojame atstumą nuo D2 juostos vidurio (HALF_LEDS = 21)
        int distFromCenterD2 = abs(posD2 - HALF_LEDS);
        if (bassIntensity > 0 && distFromCenterD2 < EXPLOSION_WIDTH) {
          CRGB redExplosion = CRGB(bassIntensity, 0, 0);
          uint8_t fadeAmount = map(distFromCenterD2, 0, EXPLOSION_WIDTH - 1, 0, 255);
          ledsD2[posD2] = blend(redExplosion, ledsD2[posD2], fadeAmount);
        }
      }
    }

    // DEŠINĖ PUSĖ (D4 juosta nuo centro į dešinę)
    if (posR < NUM_LEDS) {
      leds[posR] = finalColor; 
      // BASS SPROGIMAS: Užklojame raudona spalva D4 centro dešinėje
      if (bassIntensity > 0 && offset < EXPLOSION_WIDTH) {
        CRGB redExplosion = CRGB(bassIntensity, 0, 0);
        uint8_t fadeAmount = map(offset, 0, EXPLOSION_WIDTH - 1, 0, 255);
        leds[posR] = blend(redExplosion, leds[posR], fadeAmount);
      }
    } else {
      // PEREINAME Į D5 JUOSTĄ (Skaičiuojame atstumą nuo vidurio)
      int posD5 = offset - HALF_LEDS; 
      if (posD5 < NUM_LEDS) {
        ledsD5[posD5] = finalColor;
        
        // SPROGIMAS PER VIDURĮ: Skaičiuojame atstumą nuo D5 juostos vidurio (HALF_LEDS = 21)
        int distFromCenterD5 = abs(posD5 - HALF_LEDS);
        if (bassIntensity > 0 && distFromCenterD5 < EXPLOSION_WIDTH) {
          CRGB redExplosion = CRGB(bassIntensity, 0, 0);
          uint8_t fadeAmount = map(distFromCenterD5, 0, EXPLOSION_WIDTH - 1, 0, 255);
          ledsD5[posD5] = blend(redExplosion, ledsD5[posD5], fadeAmount);
        }
      }
    }
  }

  FastLED.show();
}
