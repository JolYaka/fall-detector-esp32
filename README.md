# Détecteur de chute embarqué : ESP32-S3 + ADXL345 (TinyML / Edge Impulse)

Il s'agit d'un détecteur de chute. Sa particularité est qu'il est propulsé par un modèle de machine learning directement installé sur un microcontrôleur ESP32. L'inférence se fait localement, sans cloud ni PC, c'est-à-dire que les données sont collectées et analysées sur place pour ensuite faire une prédiction.

![Montage complet](images/montage_vue_ensemble.jpg)

## Sommaire
- [Le projet](#le-projet)
- [Matériel et câblage](#matériel-et-câblage)
- [Fonctionnement](#fonctionnement)
- [Le modèle](#le-modèle)
- [Résultats](#résultats)
- [Installation et utilisation](#installation-et-utilisation)
- [Structure du dépôt](#structure-du-dépôt)
- [Difficultés rencontrées](#difficultés-rencontrées)
- [Pistes d'amélioration](#pistes-damélioration)

## Le projet

Ce projet consiste à détecter une chute à partir de l'accélération de l'objet sur les trois axes x, y et z. Cette détection est faite par un réseau de neurones qui se charge de classer les différents mouvements en trois classes (normal, chute, ramasser). Le choix des classes plutôt que d'un seuil s'explique par le fait que ce dernier est fixe, et que des mouvements brusques peuvent être pris pour une chute alors que ce n'est pas le cas. Le modèle de classes nous offre une meilleure souplesse : on peut créer une classe pour chaque cas de figure afin de ne pas les confondre, et ainsi mieux détecter les chutes. Ce type de projet peut avoir plusieurs applications concrètes, comme la détection des chutes des personnes âgées, ou celle d'objets précis comme un portefeuille.

## Matériel et câblage

| Composant | Détail |
|---|---|
| Carte | ESP32-S3 WROOM-1 N16R8 (16 Mo de flash, 8 Mo de PSRAM) |
| Capteur | ADXL345, accéléromètre 3 axes, en I2C, réglé sur ±16 g |
| Liaison | I2C, SDA sur GPIO 8 et SCL sur GPIO 9 |
| Alarme | message sur le moniteur série (alarme sur téléphone prévue) |

Pour les essais, le capteur est fixé sur un portefeuille et relié à la carte, montée sur une breadboard.

| Le capteur ADXL345 | La carte ESP32-S3 |
|---|---|
| ![Capteur](images/capteur_adxl345.jpg) | ![Carte](images/carte_esp32s3.jpg) |

```
ADXL345            ESP32-S3
  VCC  ───────────  3V3
  GND  ───────────  GND
  SDA  ───────────  GPIO 8
  SCL  ───────────  GPIO 9
  CS   ───────────  3V3     (mode I2C)
  SDO  ───────────  GND     (adresse I2C 0x53)
```

Le capteur est réglé sur ±16 g, car un impact dépasse largement ±2 g : avec une plage plus petite, le pic de la chute serait écrêté et on perdrait justement l'information la plus utile.

## Fonctionnement

```
[ADXL345] --I2C--> [ESP32-S3] --USB--> [Edge Impulse Studio] --> modèle entraîné
                                                                      |
                 inférence sur la carte (le modèle tourne sur l'ESP32) <┘
```

1. Collecte des données avec `edge-impulse-data-forwarder`, pour les trois classes.
2. Entraînement d'un réseau de neurones sur Edge Impulse.
3. Quantification du modèle en int8, pour qu'il tienne et s'exécute rapidement sur la carte.
4. Déploiement sous forme de bibliothèque Arduino, puis inférence en local sur l'ESP32.

## Le modèle

| Paramètre | Valeur |
|---|---|
| Classes | `normal`, `chute`, `ramasser` |
| Fréquence d'échantillonnage | environ 59 Hz |
| Fenêtre d'analyse | 118 échantillons × 3 axes (environ 2 s) |
| Format embarqué | TensorFlow Lite int8 |
| Occupation sur la carte | RAM 6,8 %, flash 6,3 % |
| Temps d'inférence | moins de 1 ms |

La classe `ramasser` correspond aux manipulations brusques qui ne sont pas des chutes. Elle permet de réduire les fausses alertes : sans elle, le simple fait d'attraper ou de secouer l'objet pourrait être détecté comme une chute.

## Résultats

| Métrique | Valeur |
|---|---|
| Précision (validation, int8) | 85,9 % |
| Précision (jeu de test, float32) | 83,66 % |
| ROC AUC | 0,97 |
| F1 de la classe `chute` | 0,90 |

Matrice de confusion (lignes : classe réelle, colonnes : classe prédite) :

| | chute | normal | ramasser |
|---|---|---|---|
| chute | 86,8 % | 5,3 % | 7,9 % |
| normal | 0 % | 90,2 % | 9,8 % |
| ramasser | 1,7 % | 18,1 % | 80,2 % |

- Les fausses alertes sont rares : 0 % des mouvements normaux et 1,7 % des manipulations sont pris pour une chute.
- Environ 13 % des chutes ne sont pas détectées (classées en normal ou ramasser). C'est le premier point à améliorer.
- La confusion entre `ramasser` et `normal` est sans conséquence pour la sécurité, puisqu'il s'agit dans les deux cas d'un non-événement.

Lors d'un test réel sur la carte, 4 chutes provoquées ont été détectées toutes les 4, avec un score `chute` de 1,00, et la classe `ramasser` s'est bien activée pendant la manipulation, sans aucune fausse alerte.

## Installation et utilisation

Prérequis : VS Code avec l'extension PlatformIO.

```bash
# Compiler
pio run

# Flasher la carte (adapter le port)
pio run -t upload --upload-port COM3

# Ouvrir le moniteur série (115200 bauds)
pio device monitor
```

Le modèle entraîné est déjà inclus dans `lib/`. Pour collecter de nouvelles données, il suffit de recopier `collecte/main_collecte.cpp` dans `src/main.cpp`, puis de lancer `edge-impulse-data-forwarder`.

## Structure du dépôt

```
fall-detector-esp32/
├── platformio.ini            # configuration de la carte, des bibliothèques et du moniteur
├── src/main.cpp              # programme d'inférence embarquée
├── collecte/main_collecte.cpp  # programme de collecte des données
├── lib/                      # bibliothèque Edge Impulse (modèle + SDK)
├── images/                   # photos du montage
└── README.md
```

## Difficultés rencontrées

- Le port série de la carte se déconnectait à chaque ouverture du moniteur. Le problème venait des signaux DTR et RTS, qui redémarraient la carte ; il a été réglé en les désactivant dans `platformio.ini`.
- Le modèle doit recevoir les données exactement comme pendant l'entraînement, c'est-à-dire les valeurs brutes en m/s² avec la plage ±16 g. Une simple conversion d'unité suffit à fausser les prédictions.
- La quantification en int8 fait passer la précision d'environ 94 % à 86 %, mais c'est cette version qui tourne sur la carte, car elle est environ quatre fois plus légère.
- Pendant les tests de chute, un câble USB trop tendu peut provoquer un redémarrage de la carte au moment de l'impact. Il faut donc laisser du mou au câble.

## Pistes d'amélioration

- Collecter des chutes plus variées (angles, hauteurs, surfaces, vitesses) afin de réduire les 13 % de chutes manquées.
- Vérifier l'équilibre des classes avant une nouvelle collecte.
- Utiliser la LED RGB intégrée (GPIO 48) comme alarme visuelle, puis un buzzer.
- Rendre le détecteur autonome, sur batterie et sans PC, pour pouvoir le porter sur soi.
