# Audit comparatif CAMPUSLINK 0.4.0 / 0.4.1

Base de l'audit : 20 novembre 2026, poste de l'utilisateur (Windows 11 Pro 26200, Intel Wi‑Fi 6 AX201 160 MHz, Vulkan), réseau Wi‑Fi de campus réel et chargé.

## 1. Inventaire et provenance

| Élément | Constat |
|---|---|
| Dépôt local `Desktop\CAMPUSLINK` | Branche `main` à `b450aa4` = **0.2.1**. Aucune source 0.3.x/0.4.x. Deux ZIP non suivis à la racine. |
| Dépôt GitHub `MONST-CODE/CAMPUSLINK` | Tags `v0.2.0`, `v0.2.1`, `v0.3.0`, `v0.4.0` (`7ad1e93`), `v0.4.1` (`c2275d0`). `main` = `018cb95` (0.4.0 + documentation). PR #5 ouverte (`2940f2a` = 0.4.1 + rapport d'audit 0.4.1). |
| Paquet 0.4.0 | `CAMPUSLINK-0.4.0-windows-x64-portable.zip` SHA-256 `4a5d8f9e…491c1`, identique à `Downloads\Compressed`. `BUILD_INFO.json` → commit `7ad1e93`, clang 20.1.8, MSVC 14.44, SDK 26100, Release. `CampusLink.exe` `0e5fd6ed…6fbb99`. |
| Paquet 0.4.1 | `…0.4.1…zip` SHA-256 `f557c366…b1cb8`, identique à `Downloads\Compressed`. Commit `c2275d0`. `CampusLink.exe` `68a0f515…6033`. |
| Autres paquets | 0.2.1 et 0.3.0 extraits dans `Downloads\Compressed`. |
| Paramètres utilisateur | `%APPDATA%\CAMPUSLINK\CampusLink\display-v1.conf` (fr, clair, Inter 15 px, 1920×1009). |
| Dépendances | SDL 3.2.30 (DLL VC), Dear ImGui 1.92.5 (source), LLVM 20.1.8, polices Inter 4.1 et JetBrains Mono 2.304 (OFL). Pas d'autre dépendance. |
| Outils locaux | Pas de MSVC ni de SDK Windows. MSYS2 UCRT64 : clang 22.1.8, SDL3 3.4.20, Python 3.14. |

Les sources 0.4.0 ont été reconstruites exactement à partir de la 0.4.1 et du diff de l'API GitHub `compare/v0.4.0...v0.4.1` (29 fichiers, 3 commits), appliqué en sens inverse sans conflit.

## 2. Différences 0.4.0 → 0.4.1

Code modifié : `network.hpp` (+38/−7), `diagnostics.cpp` (+108/−32), `native.cpp` (+67/−11), `desktop.cpp` (+164/−13), `ui_strings.inc` (+43/−4), `path_lab.{hpp,cpp}` (nouveaux), tests (+59).

| Domaine | 0.4.0 | 0.4.1 | Effet |
|---|---|---|---|
| Budget assisté | Sélection + **au plus 1** alternative (2 essais) | **1 à 4**, 3 par défaut (curseur dans la boîte de confirmation) | Plus d'essais = plus de coupures successives sur une carte unique. |
| Ordre des alternatives | RSSI ; carte distincte inoccupée prioritaire | Historique de session (succès 20 min, échec 5 min) > carte distincte > RSSI | Un BSSID qui a simplement été lent en DHCP est rétrogradé 5 min. |
| Fraîcheur du scan | Contrôlée au démarrage (UI) | Contrôlée **avant chaque essai** (TTL 2 min) | Un essai assisté long est interrompu en `invalid` et l'UI affiche « le réseau n'est plus admissible » après des tentatives déjà faites. |
| Nettoyage après délai | `WlanDisconnect` si non inactif | `WlanDisconnect` puis attente de 2 s de l'état inactif, sinon `request_unsettled` | Si Windows reconnecte automatiquement le même profil, l'opération entière s'arrête en « demande non soldée ». |
| SSID explicite | `pDot11Ssid = nullptr` | SSID choisi passé à `WlanConnect` | Correct (profil multi-SSID). Aller-retour d'échappement vérifié. |
| Lien « up » | Non exigé | `OperStatus up` exigé pour valider l'IP | Correct. |
| Dual‑STA | Taille brute | Décodage de `WLAN_INTERFACE_INFO_LIST` + synchronisation | Lecture seule, correct. |
| Chemins / multipath | — | Page 13 « Chemins et laboratoire multipath » (mesure CLP1 ciblée + simulateur d'ordonnanceurs) | Placée après « À propos » ; ajoute du bruit à la navigation. |
| UI résultat | Verdict + erreurs | + durée totale, raisons, minutages, historique | Écran plus technique. |
| Copie de la vue | Copie complète toutes les 80 ms | Copie uniquement si révision changée | Gain réel mais faible. |

## 3. Défauts communs aux deux versions

1. **Délai unique de 15 s pour association + DHCP + stabilisation**, puis `WlanDisconnect`. Sur ce réseau (passerelle 0,4–1,2 s, DNS intermittent), un bail DHCP peut dépasser 15 s : CAMPUSLINK coupe une association que Windows était en train de finaliser. En mode normal, Windows aurait réussi seul.
2. **Accès Internet lu une seule fois** dans Network List Manager juste après l'IP. NCSI n'a souvent pas encore conclu : résultat « lien local seulement » alors qu'Internet fonctionne quelques secondes plus tard.
3. **Aucune cause d'échec d'association** : ni le code de raison WLAN (`wlanReasonCode`) ni la notification `connection_attempt_fail` ne sont lus. Un échec d'authentification déclenche donc des essais sur d'autres BSSID — contraire à la règle « une erreur d'authentification ne se corrige pas par un autre BSSID ».
4. **Un échec IP déclenche aussi un autre BSSID** alors que le DHCP est commun au SSID.
5. **Pas de détection de portail captif** : `NA_InternetConnectivityV4` (WEBHIJACK) n'est pas lu.
6. **Parcours enfoui dans un atelier de 12–13 pages** : « Me connecter » ouvre la table technique de découverte (7 colonnes, une ligne par BSSID, jusqu'à 2048 lignes), un clic bascule vers « Gestionnaire de connexion » qui mélange les deux modes avec le laboratoire CLP1.
7. **SSID UTF‑8 affichés en échappements** (`Caf\xc3\xa9`).
8. **Rendu continu à l'inactivité** (vsync en Vulkan, 60 Hz en logiciel) sans attente d'événement.
9. Défauts visuels observés : barre de défilement parasite dans la barre d'outils, libellés de cases coupés en largeur étroite, valeurs « INCONNU » en capitales, titres de page à 1,65× sans hiérarchie, pas de couleurs d'état (succès/erreur) autres que « caution ».

## 4. Mesures reproductibles (poste utilisateur)

| Mesure | 0.4.0 | 0.4.1 |
|---|---|---|
| `--self-test` | PASS | PASS |
| `--inventory-smoke` | 58 interfaces, 0 diagnostic | identique |
| Fenêtre visible après lancement | 303–336 ms | 269–280 ms |
| Mémoire (working set / privée) à 3,5 s | 161 / 223 Mo | 161–162 / 224 Mo |
| `--ui-smoke` (rendu logiciel, 52 trames) | PASS 12 panneaux | PASS 13 panneaux, p50 49 ms, p95 79 ms |
| `.text` | 0x141000 | 0x14F000 |

Les connexions réelles n'ont pas été rejouées : elles couperaient la connexion de l'utilisateur. Statut `NOT_TESTED_ON_HARDWARE` pour l'association/IP avec chaque version.

## 5. Réponse à la question posée

**L'impression que la 0.4.0 fonctionne mieux est partiellement fondée.** Les deux versions partagent le défaut majeur (délai de 15 s suivi d'une déconnexion active, Internet lu trop tôt). La 0.4.1 l'aggrave sur un réseau lent : trois essais par défaut au lieu de deux, chaque échec IP enregistré comme « échec récent » qui rétrograde le meilleur point d'accès, interruption en cours d'essai par le TTL de 2 minutes, et arrêt « demande non soldée » si Windows se reconnecte pendant le nettoyage. Sur un réseau rapide, les deux versions se comportent pratiquement de la même façon et la 0.4.1 n'introduit pas de régression de démarrage, de mémoire ou d'affichage de l'accueil.

## 6. Registre

| ID | Version | Composant | Attendu | Observé / déduit du code | Gravité | Correction 0.4.2 | Test |
|---|---|---|---|---|---|---|---|
| R1 | 0.4.0, 0.4.1 | `native.cpp` `join()` | Laisser Windows finir le DHCP | Déconnexion après 15 s même associé | Critique | Budgets par phase ; association conservée si seul l'IP tarde | `slow_dhcp_keeps_association` |
| R2 | 0.4.0, 0.4.1 | `connect_wifi` | Internet confirmé quand NCSI conclut | Lecture unique | Majeure | Sondage NLM borné (≤ 12 s) | `internet_report_is_polled` |
| R3 | 0.4.1 | `wifi_plan` | 2 essais par défaut | 3 | Majeure | Budget fixe 3 BSSID, alternatives seulement après échec radio/association | `auth_failure_stops_plan` |
| R4 | 0.4.1 | `fresh_wifi` | Pas d'interruption au milieu | `invalid` après 2 min | Majeure | Fraîcheur vérifiée au départ uniquement | `long_run_not_invalidated` |
| R5 | 0.4.1 | Nettoyage | Reconnexion automatique acceptée | `request_unsettled` | Majeure | Reconnexion Windows au même profil = état connu, pas erreur | `autoreconnect_is_settled` |
| R6 | 0.4.0, 0.4.1 | Classification | Distinguer auth/radio/IP | Aucune | Majeure | Notifications ACM + `wlanReasonCode` | tests de classification |
| R7 | 0.4.0, 0.4.1 | Portail | Signaler un portail | Non détecté | Majeure | `NA_InternetConnectivityV4` | test mock |
| R8 | 0.4.0, 0.4.1 | UI | Parcours simple | Atelier de laboratoire | Majeure | Écran Connexion dédié, réseaux groupés par SSID | captures |
| R9 | 0.4.0, 0.4.1 | Affichage SSID | « Café » | `Caf\xc3\xa9` | Mineure | Décodage UTF‑8 pour l'affichage | `ssid_display_utf8` |
| R10 | 0.4.0, 0.4.1 | Boucle de rendu | 0 trame à l'inactivité | Rendu continu | Mineure | Attente d'événement bornée | mesure CPU |
| R11 | 0.4.1 | Navigation | Ordre logique | Page multipath après « À propos » | Mineure | Regroupement « Laboratoire » | captures |

## 7. Constats ajoutés pendant la réalisation de la 0.4.2

| ID | Version | Constat mesuré | Correction 0.4.2 |
|---|---|---|---|
| R12 | 0.4.0, 0.4.1 | Fenêtre réduite : 99,5 % / 100 % d'un cœur CPU (boucle sans attente quand la swapchain ne rend pas de texture) | Rendu piloté par événements : 0,6 % mesuré |
| R13 | 0.4.0, 0.4.1 | Sur le campus, `WlanScan` ne confirme pas en 5 s : tout le résultat est jeté, aucun réseau affiché alors que Windows en connaît 6 | Attente 8 s puis liste Windows récente (≤ 2 min), signalée |
| R14 | 0.4.2 (en cours de refonte) | Dialogues ouverts depuis une carte (fenêtre enfant) jamais affichés | Ouverture différée au niveau de la modale |
