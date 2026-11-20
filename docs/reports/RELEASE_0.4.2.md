# CAMPUSLINK 0.4.2 — Rapport de livraison

## Ce qui est livré

Une 0.4.2 centrée sur le parcours principal — **Connexion Wi-Fi → réseau → mode normal ou assisté** — rendue fiable sur les réseaux lents, lisible et mesurée. Le laboratoire (CLP1, clients virtuels, multipath) est conservé, séparé et facultatif. Détails : [audit 0.4.0/0.4.1](AUDIT_0.4.0_0.4.1.md), [ADR 0003](../decisions/0003-connection-reliability-and-workspace.md), [performance](PERF_0.4.2.md), [terrain AX201](FIELD_0.4.2.md).

## Package officiel

Construit par le CI Windows x64 ([run 44820353677](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44820353677), succès) depuis `60cfb936403772e440ff76ef1cd70e550d4c4aa7`, branche `release/0.4.2` : clang 20.1.8 (cible MSVC), VC 14.44.35207, SDK 10.0.26100.0, SDL 3.2.30, Dear ImGui 1.92.5, Release, arbre propre.

```text
94910199668edef689eae689ff85bcff28e93e4cfd7cbac2a4b49fa49de4d6fe  CAMPUSLINK-0.4.2-windows-x64-portable.zip  (2 530 231 octets)
3d000b7e9686977904c1247a7f08e481de62da1250c8d98d114de106061fbf04  manifest.json
```

L'empreinte a été recalculée localement après téléchargement de l'artefact. Le ZIP est rangé à la racine du dossier de projet (non suivi par Git), à côté des ZIP 0.4.0 et 0.4.1 inchangés.

## Vérifications

| Contrôle | Environnement | Résultat |
|---|---|---|
| Debug + Release, tests unitaires | CI Windows Server, chaîne officielle | 56/56 chacun |
| Catalogue FR/EN, CLP1 loopback, packaging, extraction dans un chemin avec espaces, smokes | CI Windows Server | PASS |
| `--version`, `--self-test`, `--ui-smoke`, `--inventory-smoke` du binaire officiel | PC de référence (Windows 11 Pro 26200, AX201) | PASS ; 58 interfaces |
| CPU fenêtre réduite, binaire officiel | PC de référence | **0,3 %** d'un cœur (0.4.1 : 99,7–100 %) |
| Connexion normale / assistée réelle, réseau hors de portée avec restauration | PC de référence, point d'accès mobile | PASS (voir [FIELD](FIELD_0.4.2.md)) |
| Liste de réseaux sur site dense sans notification de scan | PC de référence, campus | 5 réseaux affichés (0.4.x : aucun) |
| Rendu de 13 pages et 7 états, FR/EN, clair/sombre, 4 tailles/DPI | Rendu logiciel hors écran | PASS ; [captures](evidence/v0.4.2/) (données d'exemple) |

## Non vérifié (`NOT_TESTED_ON_HARDWARE`)

DHCP lent réel, mauvais mot de passe, portail captif, bascule vers un second point d'accès du même réseau, second adaptateur physique, rythme d'affichage GPU en fenêtre visible. Procédure : [WINDOWS11_VALIDATION](../WINDOWS11_VALIDATION.md#042-additions).

## Limites connues

Voir [KNOWN_LIMITATIONS](../KNOWN_LIMITATIONS.md). Aucune agrégation, relais ou passerelle n'est déployé ; Dual-STA reste en lecture seule.
