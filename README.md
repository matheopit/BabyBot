# BabyBot

Veilleuse / réveil pour enfant sur ESP32-S3 avec écran tactile : un robot animé,
une horloge, un réglage de réveil, un lecteur MP3 et un lecteur de tags NFC.

## Matériel

| Fonction        | Composant             | Liaison                          |
|-----------------|-----------------------|----------------------------------|
| MCU             | ESP32-S3 (16 Mo flash, 8 Mo PSRAM) | —                   |
| Écran           | ST7789T 240×320       | SPI (MOSI 45, SCLK 40, CS 42, DC 41, RST 39, BL 5) |
| Tactile         | CST328                | I2C port 1 (SDA 1, SCL 3, INT 4, RST 2) |
| Audio           | PCM5101 (DAC I2S)     | I2S 1 (BCLK 48, WS 38, DOUT 47)  |
| Carte SD        | SDMMC 1 bit           | CLK 14, CMD 17, D0 16            |
| NFC             | PN532                 | I2C port 0 (SDA 11, SCL 10)      |
| Batterie        | ADC1 canal 7          | GPIO 8                           |
| Bouton marche   | —                     | entrée GPIO 6, maintien GPIO 7   |

## Fonctionnement

1. Écran de démarrage + lecture de `startup.mp3`.
2. Connexion Wi-Fi avec les identifiants de la carte SD, puis synchro de l'heure
   (SNTP `pool.ntp.org`, fuseau Europe/Paris).
3. Écran du robot. Un appui n'importe où ouvre le menu circulaire :
   **Réveil**, **Horloge**, **Music**, **BabyBot** (retour au robot).
4. Lecture NFC en tâche de fond (PN532).

## Carte SD

À la racine de la carte :

| Fichier       | Rôle                                  |
|---------------|---------------------------------------|
| `startup.mp3` | son joué au démarrage                 |
| `wifi.txt`    | identifiants Wi-Fi (JSON, voir ci-dessous) |
| `*.mp3`       | morceaux pour le lecteur de musique   |

`wifi.txt` :

```json
{
  "ssid": "NomDuReseau",
  "password": "MotDePasse"
}
```

## Compilation

Prérequis : **ESP-IDF 6.1**, cible `esp32s3`.

> ⚠️ Le dossier `components/` est ignoré par git mais nécessaire à la compilation.
> Il doit contenir les composants locaux :
> `chmorgan__esp-audio-player` (1.0.7), `chmorgan__esp-libhelix-mp3` (1.0.3)
> et `lvgl__lvgl` (8.3.11).
> Les autres dépendances (`espressif/cjson`, `garag/esp-idf-pn532`) sont
> téléchargées automatiquement dans `managed_components/`.

En ligne de commande :

```bash
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Sous Linux/WSL, le fichier de verrouillage des dépendances est
`dependencies.linux.lock` (`dependencies.lock` contient des chemins Windows).

## VS Code (WSL)

Ouvrir le projet depuis un terminal WSL avec `code .` (la barre d'état doit
afficher `WSL: …`). Extensions recommandées (`.vscode/extensions.json`) :
**ESP-IDF** et **clangd**.

| Action                        | Raccourci  |
|-------------------------------|------------|
| Build                         | `Ctrl+E B` |
| Flash                         | `Ctrl+E F` |
| Monitor                       | `Ctrl+E M` |
| Build + Flash + Monitor       | `Ctrl+E D` |
| Debug (JTAG USB intégré)      | `F5`       |

### Accès au port série et au JTAG sous WSL

La carte est partagée depuis Windows avec `usbipd` :

```powershell
usbipd list
usbipd attach --wsl --busid <busid>
```

Côté WSL :

```bash
# port série (ou: sudo chmod a+rw /dev/ttyACM0, à refaire à chaque branchement)
sudo usermod -aG dialout $USER

# JTAG USB pour OpenOCD / le débogueur
sudo cp ~/.espressif/tools/openocd-esp32/*/openocd-esp32/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

### Débogage

Le débogage passe par le JTAG USB intégré de l'ESP32-S3
(`board/esp32s3-builtin.cfg`), sans sonde externe. Flasher d'abord le
firmware, puis `F5` (configuration « ESP-IDF Debug (JTAG USB) », arrêt sur
`app_main`).

## Organisation du code

```
main/
├── main.c                 démarrage, boucle LVGL, tâches pilotes et NFC
├── Audio_Driver/          PCM5101 + lecteur MP3
├── BAT_Driver/            mesure de la batterie
├── LCD_Driver/            ST7789T
├── LVGL_Driver/           intégration LVGL (affichage + tactile)
├── NFC_Tag/               lecture de tags NTAG via PN532
├── PWR_Key/               bouton marche/arrêt
├── SD_Card/               carte SD
├── Touch_Driver/          CST328 (+ esp_lcd_touch)
├── Wireless/              Wi-Fi, lecture de wifi.txt
└── cute/                  interface : robot, menu, horloge, réveil, musique
```

## Style de code

Le code est formaté avec `clang-format` (`.clang-format` à la racine). Les
pilotes tiers (`esp_lcd_touch`, `Vernon_ST7789T`) et les images générées
(`*_png.c`) ne sont pas reformatés. Le commit de formatage est listé dans
`.git-blame-ignore-revs` :

```bash
git config blame.ignoreRevsFile .git-blame-ignore-revs
```
