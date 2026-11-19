# CAMPUSLINK 0.3.0 — refonte de l’interface et validation

**Base :** release portable `v0.2.1` (`40329e7`), intégrée à `main` par `b450aa4`. **Build candidat :** `7a447cf420aca14658331b14bb810257c2ebdd22`. [PR #3](https://github.com/MONST-CODE/CAMPUSLINK/pull/3). Les sources réseau, simulation, score, Win32 et serveur CLP1 sont inchangées par rapport au tag 0.2.1.

## Utilisation

- La barre supérieure affiche le sélecteur **FR / EN** (français au premier lancement) et le thème actif. Le changement est immédiat. En fenêtre étroite, un menu remplace la navigation latérale.
- **Paramètres** permet de choisir Inter, JetBrains Mono ou la police de secours ImGui, une taille de 13 à 24 px et l’espacement compact. L’interface suit l’échelle de fenêtre exposée par SDL. Langue, thème, police, taille, densité et dimensions sont conservés dans `display-v1.conf` (dossier de données SDL). Un fichier absent ou illisible déclenche les défauts ; cela ne modifie ni les périmètres d’essai, ni les exports.
- **À propos** regroupe les modules, un guide de démarrage cliquable, la distinction simulation/radio, les licences et les métadonnées produites par la compilation et les bibliothèques chargées. `CampusLink.exe --version` expose la même identité de build. Une boîte de confirmation liste les BSSID de l’essai physique avant toute demande de connexion.

## Captures avant/après (rendu logiciel hors écran)

Images réellement lues depuis le framebuffer SDL logiciel sous Linux. Données vides ou initiales : aucun point d’accès n’a été testé. Elles ne démontrent ni un rendu Vulkan, ni une interaction sur écran Windows 11.

| Référence 0.2.1 | Nouvelle interface 0.3.0 |
| --- | --- |
| [Vue d’ensemble d’origine (EN)](evidence/v0.3.0/021-overview-en-dark.png) | [Vue d’ensemble FR, clair](evidence/v0.3.0/030-overview-fr-light.png) |
| — | [À propos FR, sombre](evidence/v0.3.0/030-about-fr-dark.png) · [About EN, clair](evidence/v0.3.0/030-about-en-light.png) |
| — | [Settings EN, sombre et fenêtre étroite à 200 % injecté](evidence/v0.3.0/030-settings-en-dark.png) · [Laboratoire FR, clair](evidence/v0.3.0/030-virtual-lab-fr-light.png) |

## Validation

| Vérification | Résultat |
| --- | --- |
| Catalogue central | PASS : 441 clés avec entrées FR et EN, identité des arguments `printf`, contrôle des libellés statiques. Les noms d’API, profils saisis, protocoles et identifiants techniques restent intacts. |
| Compilation et rendu logiciel sous Linux | PASS : [40/40 en Debug et Release](evidence/v0.3.0/linux-debug-release.log). 48 passages de rendu par build (12 panneaux × FR/EN × clair/sombre), à 100/125/150/200 % injectés, quatre dimensions 820×600 à 1440×900. Inter/JetBrains Mono, tailles 13 à 18 px, caractères `é`, `à`, `ç`, `œ`, `’` vérifiés dans Inter, JetBrains Mono et la police de secours enrichie. La persistance teste lecture, remplacement et retour aux défauts en cas de fichier corrompu. |
| ASan/UBSan et CLP1 loopback | PASS : [40/40 avec détection de fuites](evidence/v0.3.0/asan-ubsan.log) ; [2/2 CLP1 en boucle locale](evidence/v0.3.0/clp1-loopback.log). |
| [CI Windows 44611192459](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44611192459) | PASS : LLVM 20.1.8 / SDK 10.0.26100.0 ; 40/40 en Debug et Release, rendu logiciel bilingue, CLP1 Winsock sur boucle locale, `package`, `release-all` et `clean`. [Étapes](evidence/v0.3.0/windows-ci-44611192459.json) · [Annotations](evidence/v0.3.0/windows-ci-annotations.jsonl). |
| Windows 11 interactif, DPI réel, Vulkan GPU, Dell/AX201 et WLAN physique | NOT_TESTED_ON_HARDWARE. La CI et les captures ne remplacent pas l’observation sur écran et matériel réels. |

Les fontes et leurs deux licences OFL 1.1 sont épinglées par SHA-256 dans `deps.lock`. Le package vérifie les ressources, les chemins ZIP à séparateur `/`, la cohérence du build et le rendu depuis un dossier d’extraction contenant des espaces. Les tests du catalogue sont reproductibles avec `python tests/integration/ui_catalog.py`. Pour les images, définir `CAMPUSLINK_CAPTURE_DIR` vers un dossier existant puis lancer `CampusLink --ui-smoke` (rendu logiciel et vidéo dummy, sans opération WLAN).

L’essai Windows initial [44610371552](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44610371552) a échoué au contrôle Clang `-Wformat-security` : quatre textes traduits sans arguments étaient passés comme formats. Le candidat utilise maintenant un format littéral `%s` pour ces textes ; le contrôle statique et la compilation Linux avec `-Wformat-security` passent.

Le [deuxième essai Windows](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44610770046) a passé les 40 tests Release mais s’est arrêté à l’ouverture de l’archive : Windows PowerShell ne chargeait pas implicitement `System.IO.Compression`. Le script importe explicitement cette assembly avant de créer les entrées ZIP.

## Package portable Windows x64

- [Préversion v0.3.0](https://github.com/MONST-CODE/CAMPUSLINK/releases/tag/v0.3.0) et [ZIP portable](https://github.com/MONST-CODE/CAMPUSLINK/releases/download/v0.3.0/CAMPUSLINK-0.3.0-windows-x64-portable.zip), 2 416 443 octets. Le tag correspond à `7a447cf` ; la référence 0.2.1 est inchangée.
- ZIP SHA-256 : `038f0e1da75d271ad7f03b43ea3cfa06d5c90e3017f526bb07e5359162900eb1`.
- Manifeste SHA-256 : `56754f0b20db3bf60f6e6d2ed638f54c093846c22d3b2e981daddde754405253`.
- 25 fichiers de charge utile vérifiés par le runner, dont les polices et les notices ; séparateurs ZIP `/`, contrôle `--version`, lancement SDL et ImGui depuis le ZIP extrait dans un dossier avec espaces. Les empreintes des fichiers attachés concordent avec le journal du runner. [Métadonnées GitHub des assets](evidence/v0.3.0/release-assets.json) · [Artefact CI](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44611192459/artifacts/14572294473).
- L’exécution du binaire Windows et les contrôles d’extraction ont eu lieu sur le runner. Les prises de vue avant/après sont issues du rendu logiciel Linux, pas d’un poste Windows.
