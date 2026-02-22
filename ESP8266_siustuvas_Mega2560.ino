#include <ESP8266WiFi.h>
#include <espnow.h>

struct DataPacket {
  int vu;
  int peak;
  int mode;
} myData;

// Siunčiame visiems (Broadcast)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setup() {
  Serial.begin(115200); // Priima iš Mega Serial3
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != 0) return;
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

void loop() {
  if (Serial.find('S')) { // Ieško pradžios baito 'S'
    myData.vu = Serial.parseInt();
    myData.peak = Serial.parseInt();
    myData.mode = Serial.parseInt();
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  }
}
