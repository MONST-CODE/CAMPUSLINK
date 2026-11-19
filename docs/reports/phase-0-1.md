# CAMPUSLINK — Audit initial et rapport des phases 0–1

## Phase 0 — État initial observé

Branche `arena/4ad88df5-campuslink`, base `e20746adb62a8cd269d291038c8df8eca57e7c41` (`fix:readme`). L'arbre initial est propre. `git ls-tree -r --name-only HEAD` ne liste que `README.md` (53 octets). Aucun code, build, workflow, fichier de licence ou test à inventorier ; le test de référence initial est **`NOT_RUN` — aucun test disponible**, pas un échec préexistant. Le cahier des charges de 1 127 lignes est conservé à l'identique dans `docs/`, SHA-256 `49273083343110ac1d757f64ac822f0c8db0c8ce7c0e2700d8c0870ad5374e4e`.

| Vérification | Observation réelle / blocage |
|---|---|
| OS et CPU | `uname -srm` : Linux `6.1.158+`, `x86_64`. `/etc/os-release` : Debian 12. `lscpu` : KVM, 2 CPU logiques présentés, Xeon à 2,60 GHz. `free -m` : 3 939 MiB présentés, pas de swap. Ce ne sont pas les caractéristiques du Dell. |
| Matériel joignable | `/sys/class/net` : `eth0` (virtio) et `lo` uniquement. Entrées DMI `sys_vendor`/`product_name` et `/dev/dri` non disponibles. Pas de Wi-Fi ni de GPU de référence exposé. |
| Outils présents | Git `2.39.5`, GCC/G++ `12.2.0`, GNU Make, `ld`, `readelf`, Python `3.11.2`, `pip`, curl `7.88.1`. |
| Outils absents au départ | `command -v` ne trouve ni `cmd.exe`, ni PowerShell, Wine, LLVM (`clang++`/`clang-cl`), `cl`, MinGW x64, CMake, Ninja ou `pkg-config`. Aucun montage `/mnt/c` trouvé. Impossible d'inspecter MSVC, le SDK Windows ou `VsDevCmd` sur le Dell depuis ce bac à sable. |
| Dépendances | Pas d'en-têtes SDL3/Vulkan détectés dans `/usr/include`, pas de dépendances dans le dépôt. Archives sources SDL3 `3.2.30` et ImGui `1.92.5` récupérées via `codeload.github.com` et hachées. Tags/commits confirmés via l'API GitHub. |
| ZIP SDL3 VC x64 | Le téléchargement a échoué (`curl` 35) au redirect `release-assets.githubusercontent.com`, en dehors du périmètre réseau du bac à sable. Le SHA-256 de `deps.lock` pour ce ZIP vient du champ `digest` de l'API officielle GitHub ; le ZIP Windows n'a pas été haché localement. |
| Dell / Windows réellement installés | Modèle, BIOS, Windows 11/édition/build, CPU, RAM, stockage, GPU(s)/pilotes/ICD Vulkan, AX201 ou autre WLAN, pilotes, état radio et services : **`NOT_TESTED_ON_HARDWARE`**. Les références Dell/Intel du cahier des charges décrivent des configurations possibles, pas un relevé de la machine. |

Commandes initiales : `git status --short --branch`, `git log -5 --oneline`, `git ls-files`, `git ls-tree -r --name-only HEAD`, `git --version`, `uname -srm`, `cat /etc/os-release`, `lscpu`, `free -m`, `command -v ...`, `g++ --version`, `pkg-config --modversion sdl3 vulkan`, `ls -l /sys/class/net`, `cat /sys/class/dmi/id/{sys_vendor,product_name}`, `sha256sum` sur le cahier des charges et les archives sources. `pkg-config` et `clang++` renvoient « command not found » ; la lecture DMI renvoie « No such file or directory ». Ne pas extrapoler ces absences à l'hôte Windows.

### Risques et décisions bloquantes

| Domaine | Risque technique / validation exigée |
|---|---|
| ABI / outillage | Un LLVM installé seul ne fournit pas le STL/CRT MSVC ni le SDK. Confirmer SDK, `INCLUDE`/`LIB`, linker COFF, import SDL x64 et absence de dépendances Debug dans les imports Release. Les nomenclatures LLVM/VC et MinGW sont incompatibles pour cette distribution. |
| Windows / droits | Certaines requêtes WLAN dépendent du build Windows, de la stratégie de localisation, du pilote et de `WlanSvc`. Tester utilisateur standard, permission acceptée/refusée, service arrêté et `NOT_SUPPORTED`, sans modifier automatiquement les réglages. |
| AX201 et concurrence | Une AX201 est une référence déclarée ; CNVio2 limite la substituabilité matérielle. Wi-Fi 6, 2x2, débit PHY nominal et largeur 160 MHz ne donnent ni débit Internet, ni nombre de stations. Compter séparément adaptateurs physiques, rôles annoncés, STA concurrentes et clients logiques. Les deux propriétés STA secondaires demandent preuve et gestion par SKU/SDK/pilote. |
| GPU / Vulkan | Le SDK ne prouve pas la présence d'un périphérique Vulkan utilisable. Tester l'ICD, la sélection GPU en mode hybride, la présentation, les recréations de swapchain (DPI, taille, minimisation) et l'échec propre. Phase 2 seulement. |
| Mesures physiques | Avec un seul STA exploitable, la bascule peut couper une session distante. Valider localement avec des profils de laboratoire et un opérateur présent. Vérifier le chemin interface–route–DNS (VPN, IPv6, split DNS) avant d'attribuer une mesure ; appliquer des délais et annulation explicites ; un échec DHCP ne prouve jamais une saturation. |
| Simulation / capacité | Le plafond dépend de la charge réelle, du mode d'alimentation et de la mémoire disponible ; ne pas extrapoler depuis les 2 vCPU de la VM. Mesurer CPU/RAM, p95 et backpressure sur le Dell, avec scénario/seed et limites inscrits au rapport. Aucun trafic de simulation ne peut être envoyé au réseau. |
| Confidentialité / release | Tout rapport partageable doit exclure secrets, numéros de série et identifiants réseau non nécessaires. La licence propre de CAMPUSLINK et les notices restent à établir avant distribution. Vérifier portable Win11 propre, DLL/imports, archives/manifeste/hashes, lancement depuis un chemin avec espaces. |

**Modifications Phase 0 :** import exact de la spécification, `VERSION`, `deps.lock`, `.gitignore`, `.gitattributes`, `.editorconfig`, README et [ADR 0001](../decisions/0001-foundations.md). Critère Phase 0 satisfait pour le dépôt et l'environnement accessible ; audit de la machine physique reste ouvert.

## Phase 1 — Pipeline et SDL minimal

**Modifications :** `build.bat` (lanceur et repli ASCII), `scripts/build.ps1` (doctor, compilation directe Debug/Release, link, staging, test, clean, journal), `scripts/vs-env.bat` (initialisation VC), `scripts/audit-windows.ps1` (collecte locale en lecture seule), `src/main.cpp` (cycle de vie SDL3/RAII), `tests/integration/sdl-host-smoke.sh` (test complémentaire hors Windows). Pas de fausses fonctionnalités, ni archive de release.

**Commandes et résultats réellement observés :**

- `pip install --target /tmp/campuslink-audit/tools cmake==3.31.10` : succès. CMake n'a servi qu'à compiler **la dépendance SDL3 officielle pour le test Linux**, dans `/tmp`, pas à compiler CAMPUSLINK ni à préparer la chaîne Windows.
- Avec `PYTHONPATH=/tmp/campuslink-audit/tools`, `/tmp/campuslink-audit/tools/bin/cmake -S /tmp/campuslink-audit/SDL-release-3.2.30 -B /tmp/campuslink-audit/sdl-linux -DCMAKE_BUILD_TYPE=Release -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF`, puis `cmake --build /tmp/campuslink-audit/sdl-linux --parallel 2` via ce même binaire : `Built target SDL3-shared`, code `0`.
- `bash tests/integration/sdl-host-smoke.sh /tmp/campuslink-audit/SDL-release-3.2.30/include /tmp/campuslink-audit/sdl-linux` : code `0`. C++20 avec `-Wall -Wextra -Wpedantic -Werror`, compilation objet et link directs. GCC Debug et Release, dans un chemin contenant des espaces. Dans chaque configuration : `PASS: SDL 3002030 runtime, dummy window and quit event` (`0`), argument invalide `Usage: CampusLink [--self-test]` (`2`), pilote inexistant `SDL_Init(video): nonexistent not available` (`1`). Les deux codes non nuls sont attendus et vérifiés. `readelf -h` confirme ELF64 x86-64 ; `ldd` confirme qu'il s'agit de la vraie SDL3 partagée, pas d'un mock.
- Une première commande de vérification ELF a renvoyé `127` après les tests parce que `file` est absent ; remplacée par `readelf`/`ldd`. La première tentative de consultation des sources LLVM via `raw.githubusercontent.com` a échoué ; l'API GitHub a fourni le contenu, dont `Options.td`, `Clang.cpp` et `MSVC.cpp` de LLVM `20.1.8`. Leur lecture a confirmé le choix de CRT à la compilation et la nécessité d'éviter l'ajout d'office de `libcmt` au link Debug par le driver.
- `pip install --target /tmp/campuslink-audit/tools tree-sitter tree-sitter-powershell` puis `Parser(Language(tree_sitter_powershell.language()))` : analyse statique finale des deux scripts PowerShell sans erreur du parseur ; contrôle ASCII des batchs et vérification de `deps.lock`/copie exacte du cahier des charges. Cela ne remplace pas l'exécution via Windows PowerShell 5.1.

| Acceptation | État |
|---|---|
| Compilation C++20 et smoke SDL hors écran sous Debian (2 configurations) | `PASS` |
| `build.bat debug`, `release`, `doctor`, `test`, `clean`, codes de retour, couleurs et UTF-8 sous Windows | `NOT_RUN` : aucun Windows accessible |
| Fenêtre visible, redimensionnement, démarrage répété sous Windows / Vulkan | `NOT_TESTED_ON_HARDWARE` ; Vulkan non implémenté à ce jalon |
| Inventaire Dell/AX201, localisation et cas WLAN réels, FPS et capacité | `NOT_TESTED_ON_HARDWARE` |
| Tests de simulation, manifeste, SHA-256 du ZIP de release, portabilité Win11 propre | `NOT_RUN` (phases futures) |

**Phase 1 non acceptée côté Windows** tant que ses builds natifs n'ont pas été exécutés. Ne pas démarrer la phase 2 à partir du seul smoke Linux.

## Validation à faire sur la Precision 7750

1. Depuis un checkout dans un chemin tel que `C:\Dev\Campus Link`, avec un compte standard et un terminal *non* développeur, lancer :
   ```bat
   powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\audit-windows.ps1 > "%TEMP%\campuslink-audit-local.txt" 2>&1
   build.bat doctor
   build.bat debug
   build.bat release
   build.bat test
   build\bin\release\CampusLink.exe --self-test
   ```
   Relever `%ERRORLEVEL%` **après chaque** commande et conserver `build\logs` ; vérifier `SDL3.dll` à côté des deux exécutables. L'inventaire fourni ne lance pas de scan, n'exporte aucun profil et ne modifie pas le système. Pour la partie radio, compléter localement avec `netsh wlan show drivers` (sortie à relire avant partage) ; l'absence de donnée STA laisse la concurrence `UNKNOWN`.
2. Lancer l'exécutable Release sans argument, redimensionner et fermer à plusieurs reprises. Refaire `doctor` avec `set CAMPUSLINK_ASCII=1` puis avec `set NO_COLOR=1` et vérifier restauration de la page de code et diagnostics redirigés. Vérifier le chargement natif Vulkan à la phase 2 (par ex. `vulkaninfo --summary` si disponible) ; le script d'audit ne vérifie que la présence du loader.
3. Dans une copie de travail distincte, vérifier les cas négatifs : ZIP SDL absent, DLL d'une autre architecture, configuration Visual Studio incomplète, commande inconnue (`2`), packaging bloqué (`3`), absence d'archive, et `clean` préservant les logs et les documents (dont sécurité face à une jonction dans l'arbre généré). Tester aussi via l'invite développeur x64 pour couvrir les deux branches d'initialisation.
4. Transmettre seulement les journaux nécessaires après suppression des chemins personnels et identifiants non nécessaires. Une fois Debug/Release et ces cas validés, poursuivre avec le démarrage SDL3/ImGui/Vulkan, les scénarios d'erreur et les tests de swapchain, avant l'intégration WLAN.
