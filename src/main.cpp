/*
 * Detecteur de chute - inference embarquee
 * Carte   : ESP32-S3 WROOM-1 N16R8
 * Capteur : ADXL345 en I2C (adresse 0x53), plage +-16 g, valeurs en m/s^2
 *
 * Le modele Edge Impulse tourne directement sur l'ESP32.
 * Toutes les ~2 s : on remplit une fenetre de 118 echantillons x 3 axes,
 * on lance le classifieur, et on affiche les scores chute/normal/ramasser.
 * Alarme : message dans le moniteur serie si "chute" depasse le seuil.
 *
 * Les donnees doivent etre fournies au modele comme a l'entrainement :
 * valeurs brutes de l'ADXL345 en m/s^2, plage +-16 g, sans conversion.
 *
 * Cablage :
 *   ADXL345 VCC -> 3V3      SDA -> GPIO 8
 *   ADXL345 GND -> GND      SCL -> GPIO 9
 *   ADXL345 CS  -> 3V3      SDO -> GND
 */

#include <Fall_Detection_inferencing.h>   // modele + moteur d'inference Edge Impulse
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

// --- Broches I2C ---
#define SDA_PIN 8
#define SCL_PIN 9

// --- Seuil de confiance pour declencher l'alarme "chute" (0.0 a 1.0) ---
// 0.70 = il faut au moins 70% de confiance. A ajuster apres les premiers tests.
#define CHUTE_THRESHOLD 0.70f

Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

void setup() {
  Serial.begin(115200);
  delay(2000);   // laisse le temps a l'USB de s'initialiser

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!accel.begin()) {
    Serial.println("ERREUR : ADXL345 introuvable.");
    Serial.println("Verifiez le cablage : CS->3V3, SDO->GND, VCC->3V3, SDA->8, SCL->9");
    while (1) { delay(1000); }
  }

  // MEME plage qu'a la collecte : INDISPENSABLE pour que le modele reconnaisse les donnees
  accel.setRange(ADXL345_RANGE_16_G);

  // Garde-fou : le modele attend bien 3 axes
  if (EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME != 3) {
    Serial.println("ERREUR : le modele n'attend pas 3 axes.");
  }

  Serial.println("=== Detecteur de chute pret ===");
  Serial.printf("Fenetre : %d echantillons x 3 axes (~%d ms)\n",
                EI_CLASSIFIER_RAW_SAMPLE_COUNT,
                (int)(EI_CLASSIFIER_RAW_SAMPLE_COUNT * EI_CLASSIFIER_INTERVAL_MS));
}

void loop() {
  // --- 1) Remplir une fenetre : 118 echantillons x 3 axes = 354 valeurs ---
  float buffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE] = { 0 };

  for (size_t ix = 0; ix < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; ix += 3) {
    // Cadence precise : on vise le meme intervalle qu'a la collecte (~17 ms)
    uint64_t next_tick = micros() + (EI_CLASSIFIER_INTERVAL_MS * 1000);

    sensors_event_t event;
    accel.getEvent(&event);   // deja en m/s^2, plage +-16 g

    // On met les valeurs BRUTES directement (pas de conversion, pas de bridage)
    buffer[ix + 0] = event.acceleration.x;
    buffer[ix + 1] = event.acceleration.y;
    buffer[ix + 2] = event.acceleration.z;

    // Attendre jusqu'au prochain "tick" pour respecter la frequence
    int64_t wait = (int64_t)next_tick - (int64_t)micros();
    if (wait > 0) delayMicroseconds(wait);
  }

  // --- 2) Emballer le buffer en "signal" que le classifieur sait lire ---
  signal_t signal;
  int err = numpy::signal_from_buffer(buffer, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  if (err != 0) {
    Serial.printf("Erreur signal_from_buffer (%d)\n", err);
    return;
  }

  // --- 3) Lancer l'inference ---
  ei_impulse_result_t result = { 0 };
  err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("Erreur run_classifier (%d)\n", err);
    return;
  }

  // --- 4) Afficher les scores des 3 classes ---
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

  // --- 5) Alarme si "chute" depasse le seuil ---
  if (chute_score >= CHUTE_THRESHOLD) {
    Serial.println(">>> ALERTE : CHUTE DETECTEE ! <<<");
  }
}
