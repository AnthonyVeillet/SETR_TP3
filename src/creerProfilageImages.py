# Lit les fichiers de profilage et cree un graphique correspondant
# Marc-Andre Gardner, Hiver 2025
#
# Modifs:
#  - input/output par defaut dans ./Profilages + creation du dossier
#  - demande interactive de la duree AVEC validation selon la duree max dispo
#  - demande interactive pour nettoyer Profilages/ (supprime SEULEMENT profilage*.txt)

import argparse
import math
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
from matplotlib import pyplot as plt
from matplotlib.patches import Rectangle, Patch

COULEUR_MAPPING = {  # Voir https://jfly.uni-koeln.de/color/
    0: "#000000",
    10: "#404040",
    20: "#F0E442",
    30: "#009E73",
    40: "#D55E00",
    50: "#56B4E9",
}
NS_TO_MS = 1e6  # 1e6 ns = 1 ms


def _ensure_dir_exists(dir_path: Path) -> None:
    dir_path.mkdir(parents=True, exist_ok=True)


def _resolve_sortie_path(sortie: str, profilages_dir: Path) -> Path:
    """
    Si --sortie est un nom simple (ex: graph.png), on le met dans Profilages/.
    Si --sortie contient un chemin, on le respecte, et on crée le dossier parent.
    """
    sortie_path = Path(sortie)

    if sortie_path.is_absolute() or sortie_path.parent != Path("."):
        _ensure_dir_exists(sortie_path.parent)
        return sortie_path

    _ensure_dir_exists(profilages_dir)
    return profilages_dir / sortie_path


def _ask_duration_seconds(max_seconds: float) -> int:
    """
    Demande une duree a l'utilisateur.
    - Enter => -1 (tout afficher)
    - Entier >= 0 et <= ceil(max_seconds) => OK
    """
    max_allowed = max(0, int(math.ceil(max_seconds)))
    while True:
        raw = input(
            f"Durée à afficher en secondes (Enter = tout afficher, max ≈ {max_seconds:.2f}s) : "
        ).strip()

        if raw == "":
            return -1

        try:
            v = int(raw)
        except ValueError:
            print("⚠️  Entre un entier (ex: 20) ou Enter.")
            continue

        if v < 0:
            print("⚠️  Mets un entier >= 0, ou Enter pour tout afficher.")
            continue

        if v > max_allowed:
            print(f"⚠️  Trop grand. Max dispo: {max_allowed}s (≈ {max_seconds:.2f}s).")
            continue

        return v


def _ask_cleanup_profilages_dir(dir_path: Path) -> bool:
    while True:
        raw = input(
            f"Veux-tu supprimer les fichiers 'profilage*.txt' dans '{dir_path}' (O/n) ? "
        ).strip().lower()
        if raw in ("", "o", "oui", "y", "yes"):
            return True
        if raw in ("n", "non", "no"):
            return False
        print("⚠️  Réponds O ou N.")


def _cleanup_only_profilage_txt(dir_path: Path) -> int:
    """
    Supprime uniquement les fichiers texte de profilage:
    - profilage*.txt (ex: profilage-decodeur-1501.txt)
    Retourne le nombre de fichiers supprimés.
    """
    count = 0
    for f in dir_path.glob("profilage*.txt"):
        if f.is_file():
            try:
                f.unlink()
                count += 1
            except FileNotFoundError:
                pass
    return count


if __name__ == "__main__":
    script_dir = Path(__file__).resolve().parent
    profilages_dir_default = script_dir / "Profilages"

    parser = argparse.ArgumentParser(prog="Afficheur des etats des programmes, Labo 3 SETR")
    parser.add_argument(
        "dossier_input",
        nargs="?",
        default=profilages_dir_default,
        help="Chemin vers le dossier contenant les fichiers profilage-*.txt (defaut: ./Profilages a cote du script)",
        type=Path,
    )
    parser.add_argument(
        "--sortie",
        type=str,
        default="graph.png",
        help="Fichier de sortie (PNG). Si nom simple, sera cree dans ./Profilages",
    )

    args = parser.parse_args()

    # Dossier Profilages (input) : creation si absent
    _ensure_dir_exists(args.dossier_input)

    # Output : par defaut dans Profilages/
    sortie_path = _resolve_sortie_path(args.sortie, args.dossier_input)

    # Recupere les fichiers profilage txt
    files = sorted(
        args.dossier_input.glob("profilage-*.txt"),
        key=lambda f: int(f.stem.rpartition("-")[2]) if f.stem.rpartition("-")[2].isdigit() else -1,
        reverse=True,
    )

    if not files:
        print(f"Aucun fichier profilage-*.txt trouve dans: {args.dossier_input}")
        print("Assure-toi d'avoir copie les profilage-*.txt dans ce dossier.")
        raise SystemExit(1)

    data = {}

    print("Liste des derniers evenements par processus (voir utils.h pour la definition de chaque valeur numerique)")

    # Lecture + validation (on charge en RAM)
    for fpath in files:
        with open(fpath) as f:
            d = f.readlines()
            if len(d) < 3:
                print(f"Attention, le fichier {fpath.stem} possède moins de 2 lignes et sera ignoré!")
                continue
            if any(len(ligne) < 10 for ligne in d[:-1]):
                print(f"Attention, le fichier {fpath.stem} possède des lignes invalides (< 10 caractères) et sera ignoré!")
                continue
            if any(ligne.count(",") != 1 for ligne in d[:-1]):
                print(f"Attention, le fichier {fpath.stem} possède des lignes invalides (séparateur) et sera ignoré!")
                continue

        arr = np.loadtxt(fpath, delimiter=",")
        # Sécurité: si jamais loadtxt retourne 1D, on force 2D
        if arr.ndim == 1:
            arr = np.expand_dims(arr, axis=0)

        data[fpath.stem] = arr

        print(
            f"Dernier evenement pour {fpath.stem.partition('-')[2]} : {int(arr[-1,0])} au temps t={arr[-1,1]/1e9:.6f}"
        )

        # Transforme (etat, t_start) + prochaine ligne -> (etat, t_start, t_end)
        data[fpath.stem] = np.concatenate((arr[:-1, :], arr[1:, 1:]), axis=1)

    if not data:
        print("Aucun fichier de profilage valide apres validation.")
        raise SystemExit(1)

    # Durée max dispo selon les données (en secondes)
    ref_temps = min(d[0, 1] for d in data.values())
    end_temps = max(d[-1, 2] for d in data.values())
    max_seconds_disponible = max(0.0, (end_temps - ref_temps) / 1e9)

    # Demande interactive de la duree (validee)
    duree = _ask_duration_seconds(max_seconds_disponible)

    # Demande si on supprime seulement profilage*.txt (APRES lecture -> safe)
    if _ask_cleanup_profilages_dir(args.dossier_input):
        nb = _cleanup_only_profilage_txt(args.dossier_input)
        print(f"✅ Nettoyage fait: {nb} fichier(s) profilage*.txt supprimé(s). (stats.txt conservé)")

    # Temps max en ns pour l'affichage
    if duree == -1:
        temps_max_ns = max(0.0, end_temps - ref_temps)
    else:
        temps_max_ns = min(duree * 1e9, max(0.0, end_temps - ref_temps))

    # Evite un graphe degenerate si 0
    if temps_max_ns <= 0:
        temps_max_ns = 1e6  # 1ms

    # Plot
    fig, ax = plt.subplots(figsize=((temps_max_ns / 1e9 / 2) + 10, 4), dpi=300)

    for i, (nom, d) in enumerate(data.items()):
        for etat, temps_debut, temps_fin in d:
            # Coupe ce qui depasse la fenetre
            if temps_debut < ref_temps:
                temps_debut = ref_temps
            if temps_fin > ref_temps + temps_max_ns:
                temps_fin = ref_temps + temps_max_ns
            if temps_fin <= temps_debut:
                continue

            temps_norm_debut = (temps_debut - ref_temps) / NS_TO_MS
            temps_norm_fin = (temps_fin - ref_temps) / NS_TO_MS

            r = Rectangle(
                (temps_norm_debut, i + 0.1),
                temps_norm_fin,
                0.8,
                color=COULEUR_MAPPING.get(int(etat), "#000000"),
            )
            ax.add_patch(r)

    ax.set_yticks(
        [(i + 0.5) for i in range(len(data))],
        labels=[k.partition("-")[2] for k in data.keys()],
    )

    graphpos = ax.get_position()
    ax.set_position(
        [
            graphpos.x0,
            graphpos.y0 + graphpos.height * 0.11,
            graphpos.width,
            graphpos.height * 0.89,
        ]
    )

    ax.legend(
        loc="upper left",
        bbox_to_anchor=(0.03, -0.15),
        ncol=6,
        handles=[
            Patch(color=COULEUR_MAPPING[0], label="Indefini"),
            Patch(color=COULEUR_MAPPING[10], label="Initialisation"),
            Patch(color=COULEUR_MAPPING[20], label="Attente lecture"),
            Patch(color=COULEUR_MAPPING[30], label="Traitement"),
            Patch(color=COULEUR_MAPPING[40], label="Attente ecriture"),
            Patch(color=COULEUR_MAPPING[50], label="En pause"),
        ],
    )

    plt.xlim(0, temps_max_ns / NS_TO_MS)
    plt.ylim(0, len(data))
    plt.xlabel("Temps (ms)")
    plt.ylabel("Processus")

    _ensure_dir_exists(sortie_path.parent)
    plt.savefig(str(sortie_path), bbox_inches="tight", pad_inches=0.1)
    print(f"📈 Graph genere: {sortie_path}")