# CAMPUSLINK 0.5.0 — Rapport de livraison

## Ce qui est livré

« Connexion d'abord » (Piste 1 de l'[ADR 0004](../decisions/0004-connection-first-and-link-quality.md)), au-dessus du moteur fiabilisé en 0.4.2. Une seule radio, aucun matériel supplémentaire. Priorité : **réussir la connexion** ; rendre le lien unique meilleur et plus stable. L'agrégation multipath et une passerelle auto-hébergée restent **documentées et reportées** (Pistes 2–3).

- **Mémoire par réseau** (locale, effaçable) : le point d'accès qui a abouti, l'identification éventuelle et des délais lissés sont retenus par réseau et réutilisés. Badge « Réseau connu », indice d'identification, bouton « Oublier ce qui a été appris ».
- **Stratèges de sélection** : mémoire, preuve de session, signal/bande et densité de canal fusionnés en un ordre explicable ; chaque tentative affiche pourquoi son point d'accès a été choisi. Balayage multi-AP borné optionnel pour réseaux encombrés.
- **Surveillance du lien** (fil dédié, optionnelle, désactivée par défaut) : sur un lien faible, proposition — jamais imposée — d'un point d'accès nettement plus fort du même réseau, avec hystérésis et délai après refus. Aucune sonde active.

## Package officiel

Construit par le CI Windows x64 ([run 44856126642](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44856126642), succès) depuis `0b7f7d39c7ac…`, branche `feature/connection-first` : clang 20.1.8 (cible MSVC), VC 14.44.35207, SDK 10.0.26100.0, SDL 3.2.30, Dear ImGui 1.92.5, Release, arbre propre.

```text
e0c7ccb430d98065a7c6393fd84de3c1eaf860439ab4751d41964c6cb8644108  CAMPUSLINK-0.5.0-windows-x64-portable.zip
f7f89e524d45b1c6976fef44f5c5b4e2f35c6c7960296c59ae28f6f4f149180d  manifest.json
```

Empreinte recalculée localement après téléchargement de l'artefact. ZIP rangé à la racine du projet (non suivi par Git), à côté des 0.4.0, 0.4.1 et 0.4.2 inchangés.

## Vérifications

| Contrôle | Environnement | Résultat |
|---|---|---|
| Debug + Release, tests unitaires | CI Windows Server, chaîne officielle | 60/60 chacun |
| Catalogue FR/EN, CLP1 loopback, packaging, extraction (chemin avec espaces), smokes | CI Windows Server | PASS |
| Logique mémoire, stratèges, densité de canal, balayage borné, roaming/hystérésis, codec | Tests unitaires (60) sur mocks | PASS |
| `--version`, `--self-test`, `--ui-smoke` du binaire officiel | PC de référence (AX201) | PASS |
| CPU fenêtre réduite (surveillance éteinte), binaire officiel | PC de référence | **0,8 %** d'un cœur ; arrêt propre |
| Connexion normale réelle (non-régression 0.5.0) | PC de référence, point d'accès mobile | PASS (Internet confirmé) |
| Surveillance active, arrêt du fil | Build dev | 0,1 % d'un cœur ; `std::jthread` joint à la destruction |
| Rendu de 13 pages et 8 états (dont mémoire et proposition de roaming), FR/EN, clair/sombre, 4 tailles/DPI | Rendu logiciel hors écran | PASS ; [captures](evidence/v0.5.0/) (données d'exemple) |

## Non vérifié sur matériel (`NOT_TESTED_ON_HARDWARE`)

Le parcours mémoire/roaming **piloté par l'interface** (apprentissage sur plusieurs sessions réelles, bascule de roaming réelle sur lien faible), DHCP lent réel, mauvais mot de passe, portail captif, second adaptateur. La logique est couverte par les tests unitaires ; le comportement bout-en-bout sur réseau réel reste à accepter selon [WINDOWS11_VALIDATION](../WINDOWS11_VALIDATION.md#042-additions).

## Limites connues

Voir [KNOWN_LIMITATIONS](../KNOWN_LIMITATIONS.md). Mémoire par SSID+sécurité (pas de localisation) ; densité = nombre d'AP sur le canal, simple indice ; aucune agrégation, relais ni passerelle déployés.
