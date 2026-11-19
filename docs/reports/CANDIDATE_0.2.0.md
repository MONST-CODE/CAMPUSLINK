# Rapport de livraison — CAMPUSLINK 0.2.0 candidate

Date : 2026-11-19 UTC. [PR #2 vers `main`](https://github.com/MONST-CODE/CAMPUSLINK/pull/2), non fusionnée. Le produit et l'artefact candidat correspondent au commit `ae4c7882d0db94a781f37654c08ae5e04f554b81`. Le cahier des charges et le [rapport historique 0.1.0](FINAL_DELIVERY_REPORT.md) restent inchangés.

## Réalisé et vérifié

| Validation | Statut | Preuve et portée |
| --- | --- | --- |
| Cœur C++20, GCC Debug et Release | PASS | 36/36 dans chaque configuration ; 16 scénarios, charge déterministe de 50 000 clients et 550 000 transitions, DHCP DORA / baux / retries, budgets, validation de scope et rollback. [Journal](evidence/v0.2.0/linux-debug-release.log). |
| ASan, UBSan et détection de fuites | PASS | 36/36 sur le cœur Linux. [Journal](evidence/v0.2.0/asan-ubsan.log). |
| SDL3/ImGui sur Linux sans écran | PASS | Fenêtre dummy, rendu logiciel des onze panneaux, évènement de fermeture ; arguments invalides code 2, pilote vidéo invalide code 1. Ne démontre pas l'interaction à l'écran. |
| CLP1, serveur sur loopback | PASS | Les deux tests Python vérifient l'aller-retour et rejettent un volume non autorisé. Ne vérifie pas un trajet WLAN. |
| Comparaison physique et restauration | PASS sur mocks | Candidats sérialisés si interface, périphérique ou endpoint partagé ; une tentative de restauration quand activée, y compris après annulation ; contrôle du profil, BSSID, IPv4 et passerelle. Aucune de ces preuves n'est un essai radio. |
| Contrôle croisé Windows x64 GNU | PASS (compilation seule) | Huit unités compilées en objets avec Zig/Clang. Ne vaut ni lien MSVC ni exécution native. |
| Projet Windows — premier lot `e85629f` | PASS | [CI 44604476627](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44604476627) : installation vérifiée, doctor, UTF-8/repli ASCII, code invalide, package, release-all, clean. Le lot suivant ajoute le test Winsock et la restauration après annulation. |
| Projet Windows — candidat `ae4c788` | PASS | [CI 44604849536](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44604849536) : 36/36 Debug et Release, 11 panneaux hors écran, inventaire, profiler, codes de sortie et encodage. [Étapes](evidence/v0.2.0/windows-ci-44604849536.json) · [Annotations](evidence/v0.2.0/windows-ci-annotations.jsonl). |
| Client Winsock CLP1 | PASS | Échange sur loopback à 1, 16 et 64 Kio et rejet d’une réponse invalide. Aucun trafic WLAN. |
| ZIP, manifeste et nettoyage | PASS | 20 fichiers vérifiés, exécution extraite dans un chemin avec espaces ; `clean` préserve le ZIP, les sommes et les journaux. Le job de préparation a attaché les mêmes octets (digests concordants) sur GitHub. |
| Windows 11 client propre, fenêtre interactive et performance UI | NOT_TESTED_ON_HARDWARE | Le runner est Windows Server, pas un poste Windows 11. |
| Dell Precision 7750, présence de l'AX201 et version pilote réelle | NOT_TESTED_ON_HARDWARE | La procédure non disruptive collecte l'inventaire sans scanner. |
| Scan réel, transitions WLAN, DHCP de l'hôte, rollback sur AP, concurrence physique | NOT_TESTED_ON_HARDWARE | Nécessite un atelier autorisé. La limite simultanée reste `UNKNOWN`. |
| Vulkan interactif sur GPU | NOT_TESTED_ON_HARDWARE | `--graphics-smoke` exige le backend Vulkan natif et ne valide pas un repli logiciel en tant que Vulkan. |

## Package Windows x64

La [préversion GitHub `v0.2.0`](https://github.com/MONST-CODE/CAMPUSLINK/releases/tag/v0.2.0) est publiée, non stable. Le tag pointe vers le commit testé `ae4c788`. Le ZIP, le manifeste, `SHA256SUMS.txt` et les journaux Windows sont des fichiers attachés ; la validation matérielle ne fait pas partie de cette publication.

- [Artefact CI 14568234921](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44604849536/artifacts/14568234921), 3 561 512 octets, expiration 2027-02-17 20:04:19 UTC. C’est l’enveloppe Actions, pas le ZIP portable.
- `CAMPUSLINK-0.2.0-windows-x64-portable.zip`, 1 778 177 octets : `40d6edee86e0905d7f043ee17623724a1702788d77266cd5e546438c281e5278`.
- `manifest.json` : `aef227283ca85c3f38398e7538cac0960c4159b91678dbe70bb799ea0f409188`.
- Les deux SHA-256 annoncés par le runner concordent avec les [métadonnées GitHub des fichiers attachés](evidence/v0.2.0/release-assets.json). Le téléchargement direct dans Arena (redirection vers le stockage GitHub) est **BLOCKED** ; le runner a extrait et lancé le ZIP, puis transféré les fichiers attachés.

## Mesure locale de simulation

Run Linux GCC, 10 000 clients (plafond configuré), 110 000 transitions :

| Configuration | Temps mural | CPU du processus | p95 / 128 transitions | RSS de base → pic échantillonné |
| --- | ---: | ---: | ---: | ---: |
| Debug | 198,839 ms | 198,494 ms | 284,906 µs | 2 318 336 → 6 602 752 octets |
| Release | 15,535 ms | 15,014 ms | 28,383 µs | 2 306 048 → 6 516 736 octets |

Arrêt `LIMIT_REACHED` dans les deux cas. Ce n'est ni un plafond de la machine, ni le nombre de stations radio possibles. Les budgets RSS utilisent la croissance observée du processus. Le pic est échantillonné et ne capture pas chaque pic transitoire.

## Ce qui reste à valider sur le terrain

Appliquer la [procédure Windows 11](../WINDOWS11_VALIDATION.md) sur le poste cible. Un profil Windows et un BSSID autorisés sont nécessaires pour toute mutation. Le laboratoire logique ne produit aucun trafic radio ; les mesures CLP1 concernent seulement la cible de labo et sa méthode. L'outil ne certifie ni débit Internet, ni simultanéité RF, ni un nombre de STA par déduction du 2×2.
