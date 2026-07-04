#include <Arduino.h>
#include <driver/i2s.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h> 

// ---- IDENTIŠKA IMTUVO DUOMENŲ STRUKTŪRA ----
struct DataPacket { 
  int vu = 0; 
  int peak = 0; 
  int mode = 0; 
} myData;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Kintamasis duomenų perdavimui tarp branduolių
volatile int sharedTargetUs = 1000; 

Servo mouthServo;
const int SERVO_PIN = 18;

// Variklio nustatymai mikrosekundėmis (Surpass Hobby S0017M)
const int MOUTH_CLOSED_US = 1000; 
const int MOUTH_OPEN_US = 1600; 

// I2S Kontaktai
const i2s_port_t I2S_PORT = I2S_NUM_0;
const int I2S_WS = 25;
const int I2S_SD = 32;
const int I2S_SCK = 26;

const int BLOCK_SIZE = 64; 
int32_t samples[BLOCK_SIZE];

const float NOISE_FLOOR = 1500000.0;     
const float MAX_SOUND_VAL = 15000000.0; 

int currentTargetUs = MOUTH_CLOSED_US;
unsigned long soundDetectedTime = 0;

const unsigned long HOLD_TIME_MS = 1; 

unsigned long lastLogTime = 0;
const unsigned long LOG_INTERVAL = 100; 

// ---- ESP-NOW UŽDUOTIS ANT CORE 0 ----
void EspNowSenderTask(void * pvParameters) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); 

  // Užrakiname WiFi kanalą ties 1, kad sutaptų su imtuvu
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    vTaskDelete(NULL);
    return;
  }

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo)); 
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1; 
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  while(1) {
    int currentUs = sharedTargetUs; 

    // GRIEŽTA SĄLYGA PAGAL SERIAL MONITORIŲ:
    // Jei žandikaulis uždarytas (Tyla) – siunčiame griežtą 0 (mažiau už imtuvo 150 slenkstį)
    if (currentUs <= MOUTH_CLOSED_US + 10) {
      myData.vu = 0; 
    } else {
      // Jei žandikaulis juda (Garsas/Bass) – siunčiame skaičių nuo 50 iki 255.
      // Kuo stipresnis garsas ir labiau atsidariusi burna, tuo didesnis skaičius.
      // Kai burna atsidaro stipriai, reikšmė viršys imtuvo 150 slenkstį ir paleis sprogimą.
      myData.vu = map(currentUs, MOUTH_CLOSED_US, MOUTH_OPEN_US, 120, 255);
    }

    myData.peak = 0;
    myData.mode = 0;

    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));

    vTaskDelay(pdMS_TO_TICKS(20)); // Siunčiame kas 20ms
  }
}

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 2, 
    .dma_buf_len = BLOCK_SIZE,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
}

void setup() {
  Serial.begin(115200);
  mouthServo.attach(SERVO_PIN);
  mouthServo.writeMicroseconds(MOUTH_CLOSED_US); 
  setupI2S();
  
  // Paleidžiame siuntimo procesą ant Core 0
  xTaskCreatePinnedToCore(EspNowSenderTask, "EspNowTask", 4096, NULL, 1, NULL, 0);
  
  Serial.println("Žaibiškos reakcijos režimas grąžintas. ESP-NOW struktūra suderinta.");
}

void loop() {
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, &samples, sizeof(samples), &bytesRead, portMAX_DELAY);
  
  int samplesCount = bytesRead / sizeof(int32_t);
  if (samplesCount == 0) return;

  long long sampleSum = 0;
  for (int i = 0; i < samplesCount; i++) {
    sampleSum += samples[i];
  }
  int32_t mean = sampleSum / samplesCount;

  double sumSquares = 0;
  for (int i = 0; i < samplesCount; i++) {
    double cleanSample = (double)(samples[i] - mean); 
    sumSquares += cleanSample * cleanSample;
  }
  
  float currentVolume = sqrt(sumSquares / samplesCount);

  float limitedVolume = currentVolume;
  if (limitedVolume < NOISE_FLOOR) limitedVolume = NOISE_FLOOR;
  if (limitedVolume > MAX_SOUND_VAL) limitedVolume = MAX_SOUND_VAL;
  int calculatedTargetUs = map(limitedVolume, NOISE_FLOOR, MAX_SOUND_VAL, MOUTH_CLOSED_US, MOUTH_OPEN_US);

  if (currentVolume >= NOISE_FLOOR) {
    soundDetectedTime = millis();
    if (calculatedTargetUs > currentTargetUs) {
      currentTargetUs = calculatedTargetUs;
    }
  } 
  else {
    if (millis() - soundDetectedTime >= HOLD_TIME_MS) {
      currentTargetUs = MOUTH_CLOSED_US;
    }
  }

  // Servo judėjimas lieka visiškai toks pat
  mouthServo.writeMicroseconds(currentTargetUs);
  
  // Perduodame reikšmę į Core 0
  sharedTargetUs = currentTargetUs; 

  if (millis() - lastLogTime >= LOG_INTERVAL) {
    lastLogTime = millis();
    if (currentTargetUs <= MOUTH_CLOSED_US + 10) {
      Serial.println("Tyla |>");
    } else {
      Serial.print("Garsas |");
      int barLength = map(currentTargetUs, MOUTH_CLOSED_US, MOUTH_OPEN_US, 1, 20);
      for(int i = 0; i < barLength; i++) Serial.print("=");
      Serial.println(">");
    }
  }
}
