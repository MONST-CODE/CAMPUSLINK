# CAMPUSLINK 0.4.0 — Assistant de connexion Wi-Fi

[Pull request #4](https://github.com/MONST-CODE/CAMPUSLINK/pull/4) · [Validation Windows x64](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44677454424) · [Guide utilisateur](../USER_GUIDE.md)

## Parcours livré

**Accueil → Me connecter à un réseau → sélectionner un point d’accès → choisir un mode.** Le scan reste explicite. La connexion normale est le premier choix : Windows utilise son profil enregistré et choisit le BSSID. **Connexion assistée directe** est disponible dès la sélection, même sans essai normal et sans `test_scope.conf`. Elle demande d’abord la connexion au BSSID affiché. Si celle-ci échoue, elle peut tenter une seule alternative pour le même SSID et le même profil Windows avec la même indication de confidentialité. Elle privilégie un autre adaptateur physique distinct, confirmé déconnecté par Windows ; à défaut, elle peut tenter un autre BSSID sur la carte choisie. Les alternatives de chaque groupe sont classées par RSSI, avec ordre déterministe en cas d’égalité. Le bit de confidentialité n’identifie pas la suite de sécurité : le pilote, le profil et Windows restent responsables de l’authentification.

Ni les diagnostics actifs de laboratoire, ni CLP1, ni les clients simulés ne sont utilisés dans ces deux modes. Les outils restent accessibles séparément. Sans profil compatible, la première connexion se fait dans les paramètres Wi-Fi de Windows, accessibles depuis l’application.

## Validation de la connexion et garde-fous

- Windows reste propriétaire de l’association, des identifiants, de la configuration IP et des routes. CAMPUSLINK vérifie l’identité de l’association et deux observations consécutives de l’adresse IPv4 et de la passerelle exploitables avant de valider une nouvelle connexion. Cette validation ne signifie pas que la passerelle a répondu.
- L’état Internet est lu dans Network List Manager pour **le GUID de l’adaptateur concerné**, pas pour la machine entière. C’est un instantané Windows, éventuellement retardé ; il ne garantit ni le routage des applications, ni le débit, ni la réussite d’une authentification de portail. Un lien local est conservé pour permettre la connexion au portail dans Windows. Le signal et les débits PHY affichés ne sont pas du débit mesuré.
- La sélection repose sur un scan récent (moins de deux minutes). Une opération à la fois, au moins 15 secondes entre les demandes, au plus deux AP en mode assisté, et 15 secondes d’attente d’état par demande WLAN, plus appels natifs et une éventuelle restauration. Pas de boucle d’essais ni d’association simultanée créée sur une seule radio.
- Une ancienne connexion exploitable est capturée avant mutation. En cas d’échec, une restauration est tentée au besoin et vérifiée avant les autres essais. Si la restauration échoue ou si Windows refuse l’annulation, les essais cessent. La suppression d’une demande est faite au niveau de l’adaptateur par Native Wi-Fi ; ce n’est pas une transaction exclusive avec les autres agents ou l’utilisateur. CAMPUSLINK ne déconnecte pas une autre association observée lors du nettoyage.

## Preuves et limites

Les tests du cœur couvrent les deux chemins indépendants, le BSSID laissé à Windows, le repli sur une carte distincte disponible, le rejet d’une carte occupée ou d’une cible incohérente, le maintien du lien local sans benchmark, l’arrêt après annulation incertaine et la restauration vérifiée. Les smokes SDL3/ImGui vérifient le rendu logiciel hors écran des 12 panneaux, en FR/EN et dans les deux thèmes, sans prétendre vérifier l’interaction Windows.

**`NOT_TESTED_ON_HARDWARE`** : Windows 11 interactif, GPU/Vulkan, Intel AX201 ou autre radio physique, association/IP et portail sur un réseau réel, gain de débit, essais sous saturation et comportement d’une deuxième carte physique. La procédure terrain est dans [WINDOWS11_VALIDATION.md](../WINDOWS11_VALIDATION.md#wi-fi-connection-assistant-acceptance-040). Un réseau exclusivement IPv6 n’est pas validé par ce parcours.

### Résultats automatisés

| Vérification | Résultat |
|---|---|
| [Windows Server CI x64](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44677454424) | PASS. Debug et Release, 45/45 chacun, SDL/ImGui hors écran, CLP1 Winsock en boucle locale, ZIP et contrôle du lancement après extraction sous un chemin avec espaces. SDK 10.0.26100.0, VC 14.44.35207, LLVM 20.1.8. |
| [Linux Debug/Release](evidence/v0.4.0/linux-debug-release.log) | 45/45 chacun et smokes SDL. Captures de l’accueil [FR sombre](evidence/v0.4.0/home-fr-dark.png) et [EN clair](evidence/v0.4.0/home-en-light.png) hors écran, sans inventaire WLAN simulé. |
| [ASan/UBSan](evidence/v0.4.0/sanitizers.log) et compilation croisée | 45/45 pour le cœur commun ; backend Windows compilé en objet x64 GNU avec Zig 0.14.1, distinct du test MSVC-ABI de la CI. |
| [Catalogue FR/EN](evidence/v0.4.0/catalog.log) | 472 clés, traductions, formats et libellés vérifiés. |

### Distribution

[Préversion publiée v0.4.0](https://github.com/MONST-CODE/CAMPUSLINK/releases/tag/v0.4.0), sans modifier les versions précédentes ni la branche `main`. Le tag pointe sur `7ad1e93d943ed423b2474e3a591ed409643ade7d`. [ZIP Windows 11 x64](https://github.com/MONST-CODE/CAMPUSLINK/releases/download/v0.4.0/CAMPUSLINK-0.4.0-windows-x64-portable.zip) (2 441 140 octets) :

```text
4a5d8f9e7bf435ef4fee935a80150428e701808757c80701571f2fe7346491c1  CAMPUSLINK-0.4.0-windows-x64-portable.zip
0f10e0fdc165e432ba34e51fb6521925842320a148720299b70cef999883fd11  manifest.json
```

Les digests des fichiers publiés concordent avec les annotations CI. L’[artefact Windows 14602354799](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44677454424/artifacts/14602354799) contient les preuves générées par le runner ; [métadonnées de la release](evidence/v0.4.0/release.json) et [étapes Windows](evidence/v0.4.0/windows-jobs.json) sont conservées dans le dépôt.
