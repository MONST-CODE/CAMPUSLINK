# CAMPUSLINK 0.1.0 — Rapport de livraison

## Résultat

Le dépôt contient une application desktop C++20 intégrée, le laboratoire hors ligne, un adaptateur Windows natif, des diagnostics et un handoff bornés, un protocole de mesure contrôlé, une suite de tests et une chaîne de build/package directe. Aucune release GitHub publique ni aucun tag n’a été créé. La cible Windows 11 client et le Dell Precision 7750 nécessitent toujours une validation de terrain.

## État initial et environnement

Le commit de base `e20746adb62a8cd269d291038c8df8eca57e7c41` ne suivait que le README. La branche `arena/4ad88df5-campuslink` portait déjà, dans l’arbre de travail, le lanceur et les scripts de fondation, le smoke SDL minimal, `VERSION`, `deps.lock` et le [rapport précédent](phase-0-1.md). Ils ont été conservés ou étendus ; les deux cahiers des charges sont conservés intacts dans `docs/`.

La VM locale était Debian GNU/Linux 12 x86-64 (`Linux 6.1.158+`), GCC `12.2.0`, 2 CPU logiques KVM, environ 3,9 Gio présentés, sans WLAN/GPU Dell ni Windows SDK. SDL3 `3.2.30` et ImGui `1.92.5` viennent des archives officielles verrouillées. La CI a utilisé Windows Server `windows-2022` x64, Visual Studio Build Tools/Windows SDK détectés par `doctor`, et LLVM `20.1.8` installé depuis l’archive officielle vérifiée. Le run final annonce le SDK Windows `10.0.26100.0` et le toolset VC `14.44.35207` ; ces versions figurent aussi dans `BUILD_INFO.json`.

## Ce qui est livré

| Composant | Implémentation |
|---|---|
| Desktop | `src/ui/desktop.cpp` : onze écrans, rendu Vulkan via SDL GPU, repli SDL logiciel, worker unique, navigation, statut, instrumentation et exports. |
| Virtual Lab | `src/simulation/lab.cpp` : 16 scénarios, machine à états explicite, DHCP logique avec D/O/R/A, baux/renouvellement, scheduler déterministe, pause/arrêt, replay et CSV. `--profile` exécute le benchmark de capacité logicielle local ; aucun client simulé ne possède de radio. |
| Windows | `src/platform/windows/native.cpp` : IP Helper, SetupAPI, Native WLAN, scan explicite, PHY/radio/association/BSSID, requêtes et erreurs natives ; concurrence radio laissée inconnue sans preuve. |
| Diagnostics et sélection | `src/network/` : neuf étapes, validation stricte du périmètre, association avec profil Windows enregistré, validation du nouvel IPv4 de l’hôte, retour arrière optionnel, DNS autorisé optionnel. Score à données manquantes et décision sous hystérésis dans `src/performance/`. |
| Mesures | ICMP borné et TCP CLP1 lié à l’IPv4/interface choisie. `tools/clp1_server.py` : service de laboratoire limité à un client autorisé. Aucune sonde réelle n’a été envoyée à un réseau campus pendant cette livraison. |
| Build et distribution | `build.bat`, `scripts/build.ps1`, `setup-deps.ps1`, `package.ps1`, `verify-release.ps1`, `.github/workflows/windows.yml` : compilation directe, journaux UTF‑8, ZIP, inventaire d’imports, manifestes et SHA‑256, contrôle de l’extraction et smoke. |

## Validations exécutées et preuves

| Vérification | Résultat observé |
|---|---|
| GCC Debug et Release sur la VM | `PASS` : 30/30 tests du cœur par configuration. Dans chacune : SDL hors écran, rendu ImGui logiciel pendant trois frames et fermeture, lancement du profiler, argument invalide (`2`) et pilote inexistant (`1`), résultats vérifiés depuis un chemin avec espaces. |
| ASan + UBSan + détection de fuites | `PASS` : 30/30 tests du cœur sur cette même VM, sans diagnostic des sanitizers. |
| Protocole CLP1 en boucle locale | `PASS` : 2/2 (transfert exact et refus du dépassement de volume). Il ne s’agit pas d’un test Wi-Fi. |
| Contrôle supplémentaire Clang x64 MinGW | `PASS` sur huit unités C++ de la révision `c9f5ffa` ; génération d’objets COFF, pas un lien MSVC ni une exécution Windows. |
| CI Windows initiale | `FAIL` sur les [runs 44599936066](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44599936066) et [44600435672](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44600435672). Sur le second : `doctor`, code d’usage, boucle locale CLP1 et build/tests Release avant package ont réussi (`30 passed`, inventaire natif de 18 interfaces et un diagnostic). Le smoke extrait n’a pas produit un code de sortie exploitable via `Start-Process`. Le lanceur du smoke de package a été remplacé par `System.Diagnostics.Process`, avec collecte des flux et délai borné. |
| CI Windows, après correction du lanceur de smoke | `FAIL` au [run 44600843052](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44600843052) : l’étape de construction/extraction du ZIP a terminé, mais la seconde vérification cherchait `.sha256` au mauvais endroit. Cause : PowerShell confondait le paramètre `$Archive` et la variable `$archive`. La vérification utilise maintenant un objet fichier séparé. |
| CI Windows de la candidate | `PASS` au [run 44601203684](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44601203684) sur `589cb1c` : `doctor`, code d’usage, CLP1 en boucle locale, `package`, puis `release-all` ont réussi. Le runner a exécuté 30/30 tests du cœur dans chacun des trois builds demandés (Release pour `package`, Debug et Release pour `release-all`), ainsi que les smoke tests SDL/ImGui, l’inventaire natif et la mesure hors ligne. Les deux commandes de package ont vérifié les empreintes et relancé l’exécutable après extraction dans un dossier avec espaces. |
| CI Windows finale avec empreintes et nettoyage | `PASS` au [run 44601899605](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44601899605), commit `0090b8b` : mêmes validations, `clean` retire `build/bin` et `build/obj` en conservant l’archive, le dossier de release et les journaux. Le vérificateur indépendant a confirmé le ZIP et les 15 fichiers du manifeste. |
| Installation client Windows 11 propre, redimensionnement visible, Vulkan/device réel | `NOT_RUN` : ni session client graphique ni GPU de test accessible. |
| Dell Precision 7750, Intel AX201, scan avec et sans droits, bascule/rollback physiques, performances WLAN | `NOT_TESTED_ON_HARDWARE`. Les contrats ont été exercés avec mocks ; cela ne prouve pas les comportements du pilote. |

Le dernier palier effectué dans la VM atteignait **10 000 clients logiques / 110 000 transitions**, soit le plafond configuré, pas une saturation mesurée. Dans le lancement Release relevé, le palier a pris 16,420 ms de temps mural, 16,419 ms CPU et un p95 de 22,869 µs par lot de 128 événements. Mémoire comptabilisée : 3 640 000 octets, une estimation conservatrice et non un RSS. En Debug : 168,233 ms / 163,694 ms CPU et p95 de 219,366 µs, sur le même palier. Ces résultats ne sont ni des performances Dell, ni une mesure radio.

### Commandes réellement exécutées

```bash
# /tmp/campuslink-audit contient SDL3 officielle compilée lors du jalon précédent.
bash tests/integration/sdl-host-smoke.sh /tmp/campuslink-audit/SDL-release-3.2.30/include /tmp/campuslink-audit/sdl-linux
python3 tests/integration/clp1_loopback.py
g++ -std=c++20 -O1 -g -pthread -Iinclude -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/unit/core.cpp src/simulation/lab.cpp src/network/diagnostics.cpp src/network/dns.cpp src/performance/score.cpp -o /tmp/campuslink-audit/core-sanitized
ASAN_OPTIONS=detect_leaks=1 /tmp/campuslink-audit/core-sanitized
# Contrôle croisé parallèle, exemple pour la plateforme :
/tmp/campuslink-audit/tools/ziglang/zig c++ -target x86_64-windows-gnu -std=c++20 -D_LIBCPP_ENABLE_EXPERIMENTAL -DUNICODE -D_UNICODE -DCAMPUSLINK_VERSION=0.1.0 -Wall -Wextra -Wpedantic -Werror -Iinclude -isystem /tmp/campuslink-audit/SDL-release-3.2.30/include -isystem third_party/imgui -isystem third_party/imgui/backends -c src/platform/windows/native.cpp -o /tmp/campuslink-audit/windows-objects/native.cpp.obj
gh run list --branch arena/4ad88df5-campuslink --limit 5
gh api repos/MONST-CODE/CAMPUSLINK/check-runs/135835222680/annotations
```

Les commandes Windows exactes figurent dans le [workflow](../../.github/workflows/windows.yml). Les journaux locaux sont `logs/build/linux-host-final.log`, `logs/build/core-sanitized-final.log` et `logs/build/windows-cross.log`. Voir les copies [SDL/ImGui et profiler](evidence/linux-host.log), [sanitizers](evidence/core-sanitized.log) et [état des étapes CI finales](evidence/windows-ci-44601899605.json) et [annotations et empreintes](evidence/windows-ci-annotations.jsonl). La CI conserve les journaux Windows complets sous `logs/build/` avec les artefacts. Les redirections externes de `gh run view --log` et `gh run download` ont renvoyé `EOF` dans ce bac à sable ; le statut des étapes et les annotations sont vérifiables via l’API GitHub. Le premier workflow d’installation (avec `Invoke-WebRequest`) est antérieur à la correction du téléchargement ; la tentative d’annulation de ce job a reçu HTTP 403 sans bloquer les jobs suivants.

## Artefacts et installation

La CI a généré `dist/packages/CAMPUSLINK-0.1.0-windows-x64-portable.zip` et sa copie locale versionnée sous `dist/releases/v0.1.0/`, avec `SHA256SUMS.txt`, `manifest.json`, `BUILD_INFO.json`, `RELEASE_NOTES.md` et `RELEASE_REPORT.md`.

L’[artefact GitHub Actions `campuslink-windows-x64`](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44601899605/artifacts/14568242199) contient ces fichiers et les logs (expiration annoncée le 17 février 2027). Les valeurs suivantes sont celles du **dernier run**, et non celles d’un build précédent :

| Fichier | SHA-256 |
|---|---|
| `CAMPUSLINK-0.1.0-windows-x64-portable.zip` (ZIP versionné interne) | `65bd3c45917ef72b8a2b6df4ef9a18af9a44af3827060f32eb15a70f9b4b0fec` |
| `manifest.json` (copie versionnée indépendante) | `1add188782062dbf407ce5ab735acc27f75af8ae0141cf3bf9fb7329239c6f10` |
| Conteneur GitHub Actions (3 435 018 octets) | `1934e0078d1e371ba2aaf00b2b0aa7ec5dec813c17c9bd93d533767aa1ad271a` |

Le transfert du conteneur de GitHub vers la VM Arena est `BLOCKED` par la redirection sortante ; le runner a vérifié les fichiers et les a déposés dans l’artefact. Aucune copie binaire n’a été mise dans Git.

Après vérification des sommes, extraire l’archive et lancer `CampusLink.exe` à côté de `SDL3.dll`. Commencer par le laboratoire virtuel. N’activer un périmètre réel qu’avec des profils et un serveur expressément autorisés. Voir le [guide](../USER_GUIDE.md), les [instructions de release](../BUILD_RELEASE.md) et les [limites connues](../KNOWN_LIMITATIONS.md).

Les points ouverts sont la validation sur client Windows 11 propre et le Dell, la concurrence/rôles réellement exposés, les essais de terrain autorisés et la validation Vulkan avec un vrai device. La version ne prétend pas fournir une commutation automatique sans opérateur, un benchmark Internet général, une mesure RSS exacte ou une distribution Linux du module physique.
