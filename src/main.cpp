// Detecteur de chute - ESP32-S3 + ADXL345 + modele Edge Impulse
// Cablage : VCC->3V3, GND->GND, SDA->GPIO8, SCL->GPIO9, CS->3V3, SDO->GND

#include <Fall_Detection_inferencing.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

#define SDA_PIN 8
#define SCL_PIN 9

// seuil pour l'alerte chute, a ajuster
#define CHUTE_THRESHOLD 0.70f

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

void setup() {
  Serial.begin(115200);
  delay(2000);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!accel.begin()) {
    Serial.println("ERREUR : ADXL345 introuvable.");
    Serial.println("Verifiez le cablage : CS->3V3, SDO->GND, VCC->3V3, SDA->8, SCL->9");
    while (1) { delay(1000); }
  }

  // meme plage que pendant la collecte, sinon le modele se trompe
  accel.setRange(ADXL345_RANGE_16_G);

  if (EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 3) {
    Serial.println("ERREUR : le modele n'attend pas 3 axes.");
  }

  Serial.println("Detecteur de chute pret");
  Serial.printf("Fenetre : %d echantillons x 3 axes (~%d ms)\n",
                EI_CLASSIFIER_RAW_SAMPLE_COUNT,
                (int)(EI_CLASSIFIER_RAW_SAMPLE_COUNT * EI_CLASSIFIER_INTERVAL_MS));
}

void loop() {
  // remplissage de la fenetre (118 echantillons x 3 axes)
  float buffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE] = { 0 };

  for (size_t ix = 0; ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; ix += 3) {
    uint64_t next_tick = micros() + (EI_CLASSIFIER_INTERVAL_MS * 1000);

    sensors_event_t event;
    accel.getEvent(&event);   // en m/s^2

    // valeurs brutes, comme a l'entrainement
    buffer[ix + 0] = event.acceleration.x;
    buffer[ix + 1] = event.acceleration.y;
    buffer[ix + 2] = event.acceleration.z;

    int64_t wait = (int64_t)next_tick - (int64_t)micros();
    if (wait > 0) delayMicroseconds(wait);
  }

  signal_t signal;
  int err = numpy::signal_from_buffer(buffer, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  if (err != 0) {
    Serial.printf("Erreur signal_from_buffer (%d)\n", err);
    return;
  }

  ei_impulse_result_t result = { 0 };
  err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("Erreur run_classifier (%d)\n", err);
    return;
  }

  Serial.println("------ Prediction ------");
  float chute_score = 0.0f;
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    Serial.printf("  %-9s : %.2f\n",
                  result.classification[ix].label,
                  result.classification[ix].value);
    if (strcmp(result.classification[ix].label, "chute") == 0) {
      chute_score = result.classification[ix].value;
    }
  }
  Serial.printf("  (DSP %d ms / NN %d ms)\n",
                result.timing.dsp, result.timing.classification);

  if (chute_score >= CHUTE_THRESHOLD) {
    Serial.println(">>> ALERTE : CHUTE DETECTEE ! <<<");
  }
}
