# ADR 0001 — Fondations Windows x64

**Statut :** adopté pour les phases 0–1 ; validation native Windows en attente.

## Build et distribution

- `build.bat` reste l'entrée native ; le batch gère la page de code et préserve le code retour. Windows PowerShell 5.1 implémente l'orchestration, les logs UTF-8, les couleurs, les durées et les contrôles. Aucun téléchargement, installateur, CMake, Make, Ninja ou MSBuild dans le build CAMPUSLINK.
- Compilation et lien séparés avec le driver LLVM `clang++`, cible `x86_64-pc-windows-msvc`, linker LLD COFF ou MSVC. SDL3 est une DLL VC x64. Le code CAMPUSLINK sélectionne `/MT` en Release, `/MTd` en Debug ; la portabilité réelle dépend encore des imports transitifs de la DLL SDL3. Ne pas faire traverser au runtime des allocations devant être libérées de l'autre côté de la frontière DLL ; respecter les destructeurs SDL.
- `VERSION` est la source de la version. `deps.lock` fixe les révisions et empreintes des archives. `third_party/` contient des dépendances locales non suivies, préparées explicitement, avec leurs licences. La validation du ZIP se fait avant extraction. Toute mise à jour doit changer un seul composant à la fois, vérifier son origine/empreinte et repasser les tests de référence. La reproductibilité des binaires n'est pas démontrée : il faudra fixer le SDK, LLVM et le toolset de release, contrôler les chemins/timestamps et comparer des builds propres.
- `build/obj` et `build/bin` sont générés et nettoyables ; `build/logs` n'est pas effacé. `dist/` est réservé à la phase 11. La production du ZIP doit être bloquée tant que les vrais tests et les contrôles de licence, des imports, du manifeste, du hash et du lancement ne sont pas exécutés.

## Architecture et conventions à appliquer au fil des phases

| Répertoire | Responsabilité et dépendances |
|---|---|
| `src/main.cpp` | Point d'entrée SDL3 du jalon ; racine de composition quand les services existent. |
| `src/ui`, `src/application` | UI SDL3/ImGui/Vulkan, commandes et snapshots. L'UI ne connaît ni WLAN ni les handles natifs. |
| `include/campuslink`, `src/core` | Types forts, `Result<T, Error>` et contrats ; pas de SDL ni de Win32. Préserver code natif, opération et contexte des erreurs. |
| `src/hardware`, `src/network`, `src/platform/windows` | Adaptateurs natifs isolés ; RAII pour handles, notifications et mémoire WLAN. Corréler GUID/LUID/PnP sans inférer « physique » du seul nom de l'interface. |
| `src/simulation` | Horloge contrôlée, files bornées, clients purement en mémoire ; aucune API réseau réelle. Le profiler utilisera une première boucle représentative du moteur pour éviter de mesurer une charge factice. |
| `src/performance`, `src/intelligence` | Mesures horodatées par interface/source, classement pur et explicable ; aucune modification IP ou WLAN dans le moteur de score. |
| `tests`, `scripts`, `docs` | Tests locaux/contrat, outils opérateur et preuves ; répertoires supplémentaires créés seulement avec leur contenu. |

RAII pour tous les objets externes, arrêt coopératif (`std::jthread` / `std::stop_token`), workers bornés et snapshots immuables pour garder le thread UI réactif. Mesurer les latences avec une horloge monotone ; journaliser les observations en UTC, minimiser les identifiants et exclure les secrets. Distinguer `CONFIRMED`, `DRIVER_REPORTED`, `INFERRED` et `UNKNOWN` à la source ; une API non supportée n'est ni une erreur fatale ni la preuve d'une capacité nulle.

Windows d'abord. Avec une interface STA exploitable, pas de parallélisme radio présumé : sérialiser les essais autorisés, distinguer le chemin de trafic réel de l'interface mesurée, vérifier l'état après notification et après attribution IP, puis procéder au rollback si la politique l'autorise. Le handoff transmet une référence de profil et une sélection, jamais un bail. Les diagnostics hors périmètre ne déclenchent pas de sondes actives ; pas de repli silencieux vers Internet. Les simulations d'épuisement DHCP n'émettent aucun trafic.
