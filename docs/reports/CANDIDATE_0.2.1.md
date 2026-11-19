# CAMPUSLINK 0.2.1 — rapport de validation

2026-11-19 UTC. Commit exécutable : `40329e771bca9736ca8a0992dc8db8fabfdc5870`. [PR #2 vers `main`](https://github.com/MONST-CODE/CAMPUSLINK/pull/2), non fusionnée. Les exigences de référence et les livrables 0.1.0 et 0.2.0 n'ont pas été réécrits.

## Corrections de cette intervention

- **Sélection** : un score numérique ou un échange CLP1 réussi ne peut plus faire passer une validation hôte qui a échoué (DNS ou passerelle obligatoire, par exemple). Une tentative plus récente invalide l'ancienne mesure du même profil/BSSID, sur la même interface et avec la même méthode. Une autre carte est comparable mais ne constitue pas un remplacement pour la carte active.
- **Laboratoire logique** : les lots actifs publient un aperçu borné ; le résultat complet permet la navigation par pages et l'inspection de chaque historique. Le nouveau scénario DHCP renouvelle le bail avant expiration, sur le même slot, sans relancer le Discover. La bibliothèque de scénarios reste hors ligne et les anciens identifiants de rejeu restent stables.
- **Radio et package** : les propriétés PHY active, 802.11d, protection des trames de gestion et Hosted Network sont des rapports de Windows, pas des preuves de concurrence. Le nombre de BSSID dans une liste de sélection choisie n'est pas un nombre de stations. Le ZIP inclut le guide réseau et le vérificateur exige les guides, licences, scripts d'acceptation et exemple de périmètre pour 0.2.1.

## Tests

| Vérification | Résultat |
|---|---|
| GCC Debug et Release, cœur C++20 + SDL/ImGui | PASS : 38/38 dans chaque configuration, rendu logiciel des onze panneaux et codes de sortie. [Journal](evidence/v0.2.1/linux-debug-release.log). |
| ASan/UBSan et fuites ; CLP1 Python local | PASS : [38/38](evidence/v0.2.1/asan-ubsan.log) et [2/2 protocolaires](evidence/v0.2.1/clp1-loopback.log). |
| Backend Windows x64 : huit objets produits avec Zig/Clang GNU | PASS (compilation croisée seule) |
| [CI Windows native 44606827196](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44606827196) | PASS : 38/38 en Debug et Release, test Winsock CLP1 local, inventaire natif, codes de sortie/ASCII/UTF-8, `package`, `release-all` et `clean`. Le runner a vérifié 21 fichiers et exécuté les smokes depuis le ZIP extrait sous un chemin contenant des espaces. [Étapes](evidence/v0.2.1/windows-ci-44606827196.json) · [Annotations](evidence/v0.2.1/windows-ci-annotations.jsonl). |
| Windows 11 client propre et interactions sur fenêtre visible | NOT_TESTED_ON_HARDWARE |
| Dell Precision 7750 / Intel AX201, vraie découverte/bascule/DHCP/rollback, concurrence radio | NOT_TESTED_ON_HARDWARE |
| Vulkan natif sur GPU | NOT_TESTED_ON_HARDWARE |

La [procédure Windows 11](../WINDOWS11_VALIDATION.md) exécute les tests non disruptifs et décrit les observations à faire ensuite sur une infrastructure WLAN autorisée. Aucune station radio n'est créée par le laboratoire. CLP1 fournit des observations bornées sur la cible locale, pas un débit Internet. Le plafond de concurrence radio reste `UNKNOWN`.

## Profil de simulation Linux

Exécution GCC `-O2 -DNDEBUG`, même moteur C++20, budgets `{50000 clients, 64 MiB de croissance RSS, 4000 ms murales, 3000 ms CPU}`. Réglages équivalents : **Settings → Measure local simulation capacity**. Au palier **50 000 clients / 550 000 transitions** : 75,783 ms murales, 75,782 ms CPU, p95 19,702 µs par lot de 128 événements ; RSS échantillonné de 1 941 504 octets à 16 961 536 octets. [Journal complet](evidence/v0.2.1/linux-profile-50k.csv). Le profil s'arrête sur `LIMIT_REACHED`, c'est-à-dire à la limite configurée, et non parce qu'une capacité maximale a été mesurée.

## Package et publication

- [Préversion Windows x64 `v0.2.1`](https://github.com/MONST-CODE/CAMPUSLINK/releases/tag/v0.2.1) — non stable, commit testé `40329e7`.
- ZIP `CAMPUSLINK-0.2.1-windows-x64-portable.zip` (1 787 223 octets) : `45b837038bf82c9e6a01317ea3ccc202db7b600145e2e1926ba8d4d20c0f5ccd`.
- Manifeste `manifest.json` : `8069fc0a8c9970b1fccffecc2a3784a2f10290b1b855fbadfb2f1c6054308fdb`.
- Les SHA-256 du ZIP et du manifeste annoncés par le runner correspondent aux [digests GitHub des assets](evidence/v0.2.1/release-assets.json). Les fichiers attachés incluent `SHA256SUMS.txt`, les informations de compilation, la liste des dépendances, le rapport et les journaux Windows. Pas de nouveau téléchargement ni d'exécution du ZIP dans Arena : l'extraction et l'exécution ont eu lieu sur le runner Windows.
- [Artefact CI 14568699945](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44606827196/artifacts/14568699945) : 3 579 813 octets, expiration 2027-02-17 20:15:40 UTC. L'empreinte de cette enveloppe Actions est distincte de celle du ZIP portable.
