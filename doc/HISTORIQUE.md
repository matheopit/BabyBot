# Historique du projet BabyBot

Récapitulatif des évolutions du projet, jour par jour, reconstitué à partir des sessions de travail et de l'historique git. Les hashes renvoient aux commits principaux de chaque évolution.

## 21-23/09 : remise en route

- Premier commit, migration vers **ESP-IDF 6.1** (`661d265`).
- Tâche NFC, identifiants Wi-Fi lus sur la carte SD, réglages du réveil (`3e69c45`).
- **Tactile en panne** : correction du reset du contrôleur tactile et de la config Wi-Fi station (`6ffe76c`).
- Hygiène du dépôt :
  - fins de ligne LF via `.gitattributes` (`e6ccb69`) ;
  - fichiers Eclipse et IDE ignorés (`a5e7a09`) ;
  - style `.clang-format` et config `.clangd` (`e873905`) ;
  - code formaté (`c9376c4`) et commit de formatage ignoré dans `git blame` (`6ec0fee`).
- README (`d269825`).
- Environnement VS Code (`982d6b6`) : build, flash et debug via OpenOCD, avec une règle udev pour `/dev/ttyACM0`.
- **Conflit PN532 / I2C / audio** : issue GitHub ouverte. La carte PN532 n'a pas de broche RESET.

## 24-25/09 : réveil et NFC

- `set_wakeup_config()` dans `app_manager` : sauvegarde de `g_alarm` en JSON (`45a4a08`).
  - `days` est un bitmask : bit 0 = lundi … bit 6 = dimanche.
  - Le répertoire de config est créé s'il n'existe pas.
- `wakeup_settings.c` : « Valider » devient « Suivant » et ouvre un écran de choix du MP3 de réveil (`2197277`).
- Icône robot au centre de la zone cliquable du bas du lecteur MP3 (`f4706db`).
- Fonction `wakeup()` appelée dans la boucle principale ; drapeau `in_settings` dans `alarm_t` pour ne pas sonner pendant le réglage.
- Wi-Fi coupé une fois l'heure obtenue, pour économiser la batterie (`0bdc3b8`).
- PN532 :
  - passage en **UART/HSU** (GPIO 43/44) ;
  - mise en power-down entre deux détections ;
  - détection toutes les ~2 s.
- **Tags NFC = « mini-disques »** :
  - un tag lance un MP3 de la carte SD ;
  - reposer le même tag ne relance pas la lecture ;
  - retirer le tag arrête la musique (`71d3fe7`).

## 25/09 : visage du robot

- Animation de la main droite (rotation de 45°). Les mains sont supprimées et leur mémoire libérée au bout de 10 s.
- Bouche souriante sous les yeux.
- Primitives des yeux et de la bouche déplacées dans `robot_face.c/h`.
- Yeux en colère sans canvas, dans le même style que `create_square_eye`.
- Yeux **heureux, triste, en colère, fatigué**, façon petits robots Emo (`2a7f3f8`).
- `create_eyes()` choisit les yeux selon `mood_t`. L'écran change avec l'humeur, mais les mains ne sont créées qu'une seule fois (`034e694`).
- CLAUDE.md créé.

## 26/09 : interface et énergie

- **Flash au démarrage** (Waveshare ESP32-S3-Touch-LCD-2.8) : l'écran et le rétroéclairage ne s'allument qu'après le premier rendu LVGL (`329ad9e`).
- Horloge : zone cliquable en bas. Moniteur de perf LVGL (CPU/FPS) désactivé (`b4a49d8`).
- Heure du réveil : les boutons +/- sont remplacés par une boîte de dialogue à deux rollers, heures et minutes (`b47894d`).
- Lecteur MP3 :
  - position et durée de lecture (`69203dd`) ;
  - correction d'un crash ;
  - passage au morceau suivant.
- Humeur « fatigué » plutôt que « en colère » quand le réveil sonne (`e994c9a`).
- Config déplacée dans `/sdcard/settings` (`c4add10`).
- **Mise en veille de l'écran** après inactivité (`c3a7d99`).
- PN532 : réveil fiable après power-down (`95a2d15`).
- **Light sleep automatique** écran éteint, sur la branche `pwm` (`e5e54a0`).
- Popup NFC « Jouer / Réveil » à la pose d'un tag, quel que soit l'écran affiché (`da0208c`).

## 27-28/09

- Lecteur MP3 : explorateur de dossiers de la carte SD. Le répertoire `settings` est ignoré (`52dc1de`).
- Licence MIT (`d84f84b`).
- `settings/nfc_tags.json` : un tag est associé à un MP3 ou à un répertoire, c'est-à-dire une playlist (`b9acea2`).
- Bouton **« Arrêter le réveil »** au centre de l'écran robot (`452f622`) :
  - le menu circulaire est bloqué tant que le réveil n'est pas arrêté ;
  - l'arrêt remet `g_mood` à `MOOD_NORMAL`.

## 29-30/09

- Démarrage sans Wi-Fi : l'écran ne reste plus bloqué sur le splash (`581cac1`).
- Noms de tâches FreeRTOS distincts (`414508f`).
- **Volume sauvegardé** dans `config.json` (`ca7260e`).
- SNTP (`58ecaaf`, `56534d8`) :
  - callback de synchro ;
  - Wi-Fi coupé dès que l'heure est obtenue ;
  - `MSG_TIME_READY` passe par `app_manager_handle_msg()` ;
  - code déplacé dans `time_sync.c/h`.
- Notes de musique qui défilent sur l'écran robot pendant la lecture (`36581b6`).
- **Tactile trop sensible** : anti-rebond sur la lecture du CST328 (`df5cf4a`).
- Crash `spinlock` à la reprise audio : reprise de lecture fiable, plus de double `fclose` (`4bc2888`).
- Yeux en colère au démarrage. Branches déjà mergées supprimées.

## 01/10

- `playlist_manager` : gestion des répertoires et des MP3 extraite du lecteur, partagée avec la popup NFC (`94f8713`, `197c688`, `465ee71`).
- Volume : son de démarrage à 10, volume de `config.json` appliqué au lancement (`ad932aa`).
- `nfc_task` épinglée sur le cœur 0 (`9ea63d2`).
- Rétroéclairage : suppression du `gpio_config()` qui provoquait le warning LEDC sur le GPIO 5 (`74e4de4`).
- Popup NFC : titre = nom du répertoire ou du MP3, sans chemin ni extension (`6d05eda`).
- Splash screen : zoom du titre, puis écran supprimé une fois quitté (`4df0e70`).
- Icônes `*_png.c` déplacées dans `main/ressources`. Son de démarrage désactivable par un define (`22b12b5`).
- **Mise à jour du firmware** (`2e74d03`) :
  - téléchargement de la dernière release GitHub sur la carte SD, puis flash OTA au démarrage ;
  - script `release.sh` ;
  - mbedTLS alloué en PSRAM : la RAM interne ne suffisait pas pour le TLS (`b9649b1`) ;
  - version 1.0.1 (`c30ad15`).

## 02/10

- Batterie (`42b15e1`, `6e286c8`, `c7f97aa`) :
  - mesure filtrée et pourcentage LiPo 3,7 V ;
  - indicateur en bas à droite de l'écran robot ;
  - à **20 % ou moins**, icône affichée et yeux fatigués.
- USB : pas de light sleep automatique quand l'USB Serial/JTAG est branché, sinon la connexion USB décroche (`1d43e73`).
- Branche `pwm` mergée dans master, anciennes branches et PR nettoyées.
- Vérification de mise à jour GitHub lancée sur le cœur 1, pour ne pas faire lagger l'affichage au démarrage (`b473073`).
- Création de ce fichier `doc/HISTORIQUE.md` (`4841bed`), avec une règle dans CLAUDE.md pour le tenir à jour à chaque demande.
- Robot : yeux souriants (`MOOD_HAPPY`) tant qu'une musique joue (lecteur ou tag NFC), sauf pendant la sonnerie du réveil ; la batterie basse reste prioritaire (PR `yeux-musique`).

## Points restés ouverts ou à surveiller

- **PN532** : instabilité avec le power-down (voir l'issue GitHub). Pas de broche RESET sur la carte.
- **Réveil** : environ une minute de retard constatée le 28/09, jugé acceptable sur le moment.
- **Partitions et bootloader** : toute modification nécessite toujours un flash USB, l'OTA ne les met pas à jour.
