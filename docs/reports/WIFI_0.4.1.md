# CAMPUSLINK 0.4.1 — Rapport de livraison

[Audit, architecture et sources](../decisions/0002-connectivity-evidence.md) · [PR #5](https://github.com/MONST-CODE/CAMPUSLINK/pull/5) · [CI Windows x64](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44682966768)

## Résultats et arbitrages

- **Connexion** : l’utilisateur choisit toujours entre normal et assisté direct. Le BSSID est laissé à Windows en mode normal ; le SSID demandé reste explicite. En assisté, le premier candidat reste celui choisi. Le budget total est réglable de 1 à 4 (3 par défaut) et les options incompatibles ou dupliquées sont écartées. Une carte supplémentaire doit être physiquement distincte et confirmée inoccupée. Résultats ciblés récents > état d’inoccupation indépendant > RSSI, sans inférence de débit. Les échecs récents décotent une alternative ; les observations expirent. Au plus une restauration sur la durée de l’opération ; une demande non terminée bloque le repli. La prochaine tentative revalide la fraîcheur.
- **Diagnostic** : distinction entre association non confirmée et échec de configuration IPv4 après association ; pas d’attribution fictive à DHCP, au mot de passe ou au portail. Les temps d’association et de lien exploitable sont échantillonnés ; le signalement Internet par interface est celui de Windows, pas la mesure de CAMPUSLINK. Sans profil, Windows demeure l’interface de consentement et d’identification.
- **Capacités et chemins** : GUID et état des interfaces secondaires listés avec vérification de taille ; synchronisation interrogée sans activation. Mesures CLP1, ICMP et éventuellement DNS pour un profil/BSSID actuellement associé et un endpoint autorisé **sans reconnexion**. L’interface, la source, la méthode et la validité de l’échantillon sont distinctes du score de connexion.
- **Multipath** : architecture comparée et simulation déterministe livrée. Pas de transport multipath, relais, passerelle, proxy ni installation de pilote. La suite proposée est un transfert applicatif HTTPS Range contre un endpoint contrôlé, certifié et instrumenté, puis une comparaison sur deux interfaces effectivement distinctes. Un relais nécessiterait un opt-in authentifié, révocation et quotas, sans transfert de bail ni partage de portail. Une passerelle auto-hébergée n’est nécessaire que pour les architectures qui en dépendent ; elle n’existe pas dans ce package.
- **Documentation** : le README principal et le README embarqué dirigent maintenant vers la connexion Wi-Fi. Le tag et le ZIP précédents restent inchangés. La licence des polices et les dépendances verrouillées sont conservées.

## Banc hors ligne

128 segments de 16 KiB, fenêtre de 16, deux latences/capacités et jitter déterministe. « Seul » compare les deux exécutions monochemin et retient la meilleure référence dans le scénario. Les chiffres sont des sorties de modèle, pas des mesures réseau. Le scénario partagé impose 1 500 Kio/s ; le scénario dégradé injecte une perte tous les sept envois sur le premier chemin et une interruption de 220 à 1 350 ms. Une perte déclenche un nouvel essai borné, par l’autre chemin quand possible. Le modèle n’implémente ni équité TCP, ni chiffrement, ni contrôle de congestion Internet.

| Scénario | Ordonnanceur | Durée modèle (ms) | Débit modèle (Mbit/s) | Reprises | Hors ordre |
|---|---|---:|---:|---:|---:|
| Capacités indépendantes | Seul | 1 106 | 15,17 | 0 | 0 |
| | Tourniquet | 1 178 | 14,25 | 0 | 63 |
| | Fin estimée | 984 | 17,05 | 0 | 22 |
| Goulot partagé | Seul | 1 391 | 12,06 | 0 | 0 |
| | Tourniquet | 1 482 | 11,32 | 0 | 63 |
| | Fin estimée | 1 391 | 12,06 | 0 | 0 |
| Pertes/coupure | Seul | 1 528 | 10,98 | 0 | 37 |
| | Tourniquet | 2 438 | 6,88 | 9 | 77 |
| | Fin estimée | 1 627 | 10,31 | 4 | 53 |

Interprétation : la meilleure stratégie change selon le scénario. La référence monochemin peut être réordonnée par le jitter simulé indépendant par segment ; ce n’est pas une propriété d’un flux TCP livré à l’application. Le gain de 12,4 % dans un seul scénario de modèle n’est pas un gain produit. Aucune statistique d’incertitude n’est calculable pour un scénario déterministe exécuté sans variation des paramètres.

## Validation : matrice et limites

| Contrôle | Niveau |
|---|---|
| Budget, déduplication, données périmées, faux candidats, repli, arrêt, restauration, lien local | C++ sur mocks ; aucune association réelle déduite |
| Mesure sans reconnexion, refus d’un scope incorrect et rejet après changement de BSSID | C++ sur mocks + CLP1 sur boucle locale (transport), pas de capture egress |
| Fenêtre de réassemblage, goulot partagé et reprise après perte | Simulation déterministe ; pas de liaison physique |
| FR/EN, quatre combinaisons thème/langue, polices et 13 panneaux | SDL3/ImGui hors écran ; ce n’est pas un test Windows interactif |
| Windows 11 standard, Dual-STA physique/AX201, GPU, portail, tests sous saturation, débit réel par chemin | `NOT_TESTED_ON_HARDWARE` ; suivre [la procédure terrain](../WINDOWS11_VALIDATION.md#041-additions) |
| Débit agrégé réel, équité, nœuds distants et agrégation système | Non implémentés ; aucune validation ni gain annoncé |

Les taux de connexion/récupération réels, temps avant Internet vérifié et mesures RF ne peuvent pas être tirés des mocks, de la simulation ou de Windows Server CI. Les preuves réseau Windows du sandbox ne portent que sur le protocole autorisé en boucle locale.

### Coût logiciel — comparaison locale

Mesures hors écran SDL dummy, même SDL3/ImGui et même hôte, un passage par configuration. Pour la référence 0.4.0, seul le chronométrage du smoke a été ajouté dans une extraction Git temporaire, sans affecter le tag. Les séries ne sont pas parfaitement appariées : 48 images côté référence contre 52 côté candidat, suite à l’ajout du panneau Chemins. Les compilations et smokes locaux ont partagé la machine ; valeurs descriptives, pas une validation d’absence de régression sur GPU.

| Mesure | 0.4.0 | 0.4.1 |
|---|---:|---:|
| Temps image p50 / p95 Debug (ms) | 25,459 / 42,512 | 29,918 / 46,426 |
| Temps image p50 / p95 Release (ms) | 25,366 / 50,443 | 30,710 / 47,872 |
| CPU de simulation à 10 000 clients, Release (ms) | 13,748 | 14,569 |
| RSS échantillonné à 10 000 clients, Release (octets) | 6 541 312 | 6 549 504 |

La médiane du smoke est plus élevée ; le p95 varie dans les deux sens. La suppression des copies de snapshots inchangés n’est pas une preuve de gain de rythme d’affichage. Refaire l’essai en série appariée à plusieurs répétitions sur le même GPU Windows, avec inventaire chargé et interaction réelle. Les chiffres de simulation ne décrivent pas des sessions STA.

## Distribution et preuves

Candidat `c2275d06b5757f7bac4fe068e313cca81825ee3b`. [ZIP portable](https://github.com/MONST-CODE/CAMPUSLINK/releases/download/v0.4.1/CAMPUSLINK-0.4.1-windows-x64-portable.zip) (2 467 311 octets) et [artefact CI](https://github.com/MONST-CODE/CAMPUSLINK/actions/runs/44682966768/artifacts/14604758914). La CI a vérifié le manifeste, l’archive, l’extraction dans un chemin contenant des espaces et les smokes sur le binaire extrait. Les digests GitHub concordent avec les annotations du runner :

```text
f557c366021c2421dc6b552d80d9ac9610a42c431bbb6db3ae0378bb7d8b1cb8  CAMPUSLINK-0.4.1-windows-x64-portable.zip
222fa0ec065d3031dc39937f71b832659e63569a5490834bc56a8f9a3266a404  manifest.json
```

- Windows x64 Debug et Release 49/49, CLP1 natif en boucle locale, SDL hors écran et packaging : PASS. SDK 10.0.26100.0, LLVM 20.1.8, VC 14.44.35207.
- [Linux Debug et Release](evidence/v0.4.1/linux.log) 49/49 ; [référence 0.4.0](evidence/v0.4.1/baseline-linux.log) : 45/45. [ASan/UBSan](evidence/v0.4.1/sanitizers.log) : 49/49. Catalogue FR/EN : 511 clés. Backend compilé aussi en objet Windows GNU x64 (Zig 0.14.1).
