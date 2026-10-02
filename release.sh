#!/usr/bin/env bash
# Publie une release GitHub que BabyBot téléchargera au prochain démarrage.
# Usage : ./release.sh 1.2.3
# (version.txt mis à jour + commit + tag v1.2.3 + build + release avec BabyBot.bin)
set -euo pipefail

version="${1:?Usage : $0 X.Y.Z}"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Version X.Y.Z attendue"; exit 1; }
cd "$(dirname "$0")"
[[ -z "$(git status --porcelain)" ]] || { echo "Dépôt pas propre, commiter d'abord"; exit 1; }

echo "$version" > version.txt
git commit -m "Version $version" version.txt
git tag "v$version"

. "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" > /dev/null
idf.py build

git push origin HEAD "v$version"
gh release create "v$version" build/BabyBot.bin --title "v$version" --generate-notes
