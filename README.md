# CAMPUSLINK
Intelligent Wi-Fi Connectivity Manager

Cible : Windows 11 x64, C++20. Version visée : `0.1.0`. Ce jalon ouvre une fenêtre SDL3 redimensionnable et fournit un smoke test hors écran. Il n'inventorie pas encore le Wi-Fi, ne simule aucun client et ne mesure aucune performance réseau.

- [Cahier des charges](docs/CAMPUSLINK_SPEC_ARENA.md)
- [Audit initial et preuves du jalon](docs/reports/phase-0-1.md)
- [Décisions d’architecture](docs/decisions/0001-foundations.md)

## Installer la chaîne Windows

1. Installer **Visual Studio Build Tools** avec les outils C++ x64, le STL/CRT MSVC et un **SDK Windows 11** (`10.0.22000.0` ou ultérieur). `build.bat` détecte l’installation avec `vswhere` et appelle `VsDevCmd.bat` si nécessaire. Il ne lance pas MSBuild.
2. Installer **LLVM x64** (`clang++.exe`, de préférence avec `lld-link.exe`), accessible dans `PATH` ou dans l’emplacement LLVM standard. L’option de runtime statique et celle de sélection du linker ont été vérifiées dans les sources LLVM 20.1.8 ; la chaîne Windows elle-même reste à valider.
3. Télécharger le **ZIP VC** SDL3 depuis l’URL de `deps.lock` (pas le package MinGW). Vérifier son SHA-256 **avant** extraction. Par exemple, depuis la racine dans Windows PowerShell :

   ```powershell
   $lock = Get-Content .\deps.lock -Raw | ConvertFrom-Json
   $zip = 'C:\Downloads\SDL3-devel-3.2.30-VC.zip' # adapter le chemin
   if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $lock.sdl3.windowsVcSha256) {
       throw 'SHA-256 SDL3 incorrect'
   }
   ```

   Copier le contenu du dossier versionné extrait vers `third_party\sdl3` : `include\SDL3\SDL.h`, `lib\x64\SDL3.lib`, `lib\x64\SDL3.dll` et la licence doivent y être conservés. `doctor` vérifie l’en-tête et l’architecture de la DLL ; le linker vérifie la bibliothèque d’import et le smoke test compare la version SDL chargée à celle des en-têtes. Le build ne télécharge rien et ne revérifie pas le ZIP après extraction.
4. Dear ImGui et le SDK Vulkan sont requis à partir de la phase 2, pas pour cette fenêtre de démarrage. Le SDK sera détecté via `VULKAN_SDK`; le pilote graphique reste distinct du SDK.

## Commandes

Depuis `cmd.exe` :

```bat
build.bat help
build.bat doctor
build.bat debug
build.bat release
build.bat test
build\bin\release\CampusLink.exe --self-test
```

Sans argument, le script compile Release. `test` reconstruit Debug, puis démarre SDL3 avec le pilote vidéo `dummy`, crée une fenêtre cachée et vérifie le traitement de sa fermeture via la file d’événements SDL. Il ne valide pas l’affichage réel, ImGui, Vulkan ni le matériel. Pour le contrôle visuel, lancer `build\bin\release\CampusLink.exe` sans argument.

Les logs UTF-8 sont uniques sous `build\logs\` (nom horodaté UTC). `set CAMPUSLINK_ASCII=1` conserve la page de code et force des marqueurs ASCII. `set NO_COLOR=1` désactive la couleur ; elle est aussi désactivée si la sortie est redirigée. `clean` retire uniquement `build\obj` et `build\bin`, et conserve les logs. `package` et `release-all` sont bloqués (code `3`) tant que les critères de release ne sont pas implémentés ; aucun ZIP n’est généré. Un argument de build invalide renvoie `2`. Une erreur d’outil renvoie son code natif ; un contrôle interne en échec renvoie `1`.

Le batch délègue à Windows PowerShell 5.1. Sa politique `Bypass` ne vaut que pour le processus enfant, et ne modifie pas la politique de la machine. Une politique de groupe prioritaire peut empêcher l’exécution du script.
