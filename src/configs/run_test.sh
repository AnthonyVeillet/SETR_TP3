#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

cleanup() {
  echo
  echo "🛑 Stop demandé -> arrêt propre..."
  # tue les sudo lancés par les scripts (recommandé par l'énoncé)
  sudo killall sudo 2>/dev/null || true

  # par sécurité : tue les programmes du pipeline si encore vivants
  sudo pkill -f compositeur 2>/dev/null || true
  sudo pkill -f decodeur 2>/dev/null || true
  sudo pkill -f convertisseur 2>/dev/null || true
  sudo pkill -f filtreur 2>/dev/null || true
  sudo pkill -f redimensionneur 2>/dev/null || true

  echo "✅ Terminé."
}

# Ctrl+C (SIGINT) et fermeture du script
trap cleanup INT
trap cleanup EXIT

# Nettoyage au démarrage
rm -f profilage-*.txt stats.txt

echo "===================================="
echo "   Lancement des tests - Labo 3"
echo "===================================="
echo
echo "Nettoyage fait: profilage-*.txt et stats.txt supprimés"
echo
echo "Tests disponibles :"
echo "  1) 01_sourceUnique.bash"
echo "  2) 02_deuxVideos.bash"
echo "  3) 03_troisVideos.bash"
echo "  4) 04_mosaique.bash"
echo "  5) 05_grossevideo.bash"
echo "  6) 06_troisPetiteVideosRedimensionnees.bash"
echo "  7) 07_deuxVideosGris.bash"
echo "  8) 08_deuxFiltres.bash"
echo "  9) 09_realtime4videos.bash"
echo " 10) 10_realtimeDeuxFiltres.bash"
echo " 11) 11_deadlineTroisVideos.bash"
echo

read -rp "Entre le numero du test a lancer (1-11) : " choice

case "$choice" in
  1)  test="01_sourceUnique.bash" ;;
  2)  test="02_deuxVideos.bash" ;;
  3)  test="03_troisVideos.bash" ;;
  4)  test="04_mosaique.bash" ;;
  5)  test="05_grossevideo.bash" ;;
  6)  test="06_troisPetiteVideosRedimensionnees.bash" ;;
  7)  test="07_deuxVideosGris.bash" ;;
  8)  test="08_deuxFiltres.bash" ;;
  9)  test="09_realtime4videos.bash" ;;
  10) test="10_realtimeDeuxFiltres.bash" ;;
  11) test="11_deadlineTroisVideos.bash" ;;
  *)
    echo "❌ Choix invalide. Entre un numero entre 1 et 11."
    exit 1
    ;;
esac

if [[ ! -f "$test" ]]; then
  echo "❌ Script introuvable: $test"
  exit 1
fi

chmod +x "./$test" 2>/dev/null || true

echo
echo "🚀 Lancement du test : $test"
echo "Appuie sur Ctrl+C pour arreter."
echo "------------------------------------"
echo

./"$test"

# Si le test finit "normalement", on ne veut pas forcément killer tout,
# donc on désactive le trap EXIT avant de quitter.
trap - EXIT
cleanup
