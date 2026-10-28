// Collecte des donnees pour Edge Impulse (data forwarder)
// a copier dans src/main.cpp pour l'utiliser

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

#define SDA_PIN 8
#define SCL_PIN 9

#define FREQUENCY_HZ   62.5
#define INTERVAL_MS    (1000.0 / FREQUENCY_HZ)

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
static unsigned long last_interval_ms = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!accel.begin()) {
    Serial.println("ERREUR : ADXL345 introuvable.");
    Serial.println("Verifiez le cablage : CS->3V3, SDO->GND, VCC->3V3, SDA->8, SCL->9");
    while (1) { delay(1000); }
  }
  accel.setRange(ADXL345_RANGE_16_G);
}

void loop() {
  if (millis() > last_interval_ms + INTERVAL_MS) {
    last_interval_ms = millis();
    sensors_event_t event;
    accel.getEvent(&event);
    Serial.print(event.acceleration.x, 4);
    Serial.print('\t');
    Serial.print(event.acceleration.y, 4);
    Serial.print('\t');
    Serial.println(event.acceleration.z, 4);
  }
}
