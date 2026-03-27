#include <espnow.h>
#include <ESP8266WiFi.h>

typedef struct { uint8_t vu; uint8_t br; } Packet;
Packet pkt;
uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != 0) return;
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(broadcast, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

void loop() {
  // Laukiame 3 baitų (Header 0xAA + VU + BR)
  if (Serial.available() >= 3) {
    if (Serial.read() == 0xAA) { 
      pkt.vu = Serial.read();
      pkt.br = Serial.read();
      
      esp_now_send(broadcast, (uint8_t *) &pkt, sizeof(pkt));
      
      // DEBUG (Matysi monitoriuje tik jei DIP 1,2,3,4 ON)
      Serial.print("ESP SIUNČIA Į ORĄ: "); Serial.println(pkt.vu);
    }
  }
}
