# MASTER PROMPT — CAMPUSLINK
## Mission autonome de réalisation complète, intégration, validation, packaging et livraison

**Version du prompt :** 1.0  
**Projet :** CAMPUSLINK — Intelligent Wi-Fi Connectivity Research & Management System  
**Cible initiale :** Windows 11 x64  
**Machine de référence déclarée :** Dell Precision 7750 (à confirmer par inventaire matériel)  
**Adaptateur de référence déclaré :** Intel Wi-Fi 6 AX201 160 MHz (présence et pilote à confirmer sur la machine réelle)  
**Langage :** C++20  
**UI :** SDL3 + Dear ImGui  
**Rendu :** Vulkan prioritaire, solution de repli SDL3 si le backend Vulkan n'est pas disponible  
**Compilation Windows :** `build.bat`, compilation et édition de liens directes, sans CMake/Make/Ninja/MSBuild pour le projet CAMPUSLINK  
**Portabilité :** Windows d'abord ; architecture préparée pour Linux ensuite.

---

# 0. ORDRE DE MISSION — À EXÉCUTER JUSQU'À LA LIVRAISON

Tu es responsable de l'ingénierie logicielle, de l'architecture, de l'implémentation, de l'intégration, des tests, de la correction des défauts, de la documentation, du système de compilation, du packaging et de la livraison de CAMPUSLINK.

**La demande est une livraison de bout en bout, en une seule mission autonome.** Les phases décrites dans le cahier des charges sont des lots de travail internes, et non des étapes auxquelles tu dois t'arrêter pour demander une validation. Tu dois enchaîner l'audit, les corrections, l'implémentation de tous les modules, leur intégration, les tests, les corrections, le packaging et la préparation de la release sans attendre une approbation entre les lots.

Tu ne dois pas répondre uniquement par un plan, une architecture théorique, une liste de recommandations, une maquette, un prototype ou un rapport d'audit. Tu dois modifier réellement le dépôt et produire le logiciel, les tests, les scripts, les documents et les artefacts de livraison.

## 0.1 Autorité de travail

L'utilisateur t'autorise à travailler sur le dépôt CAMPUSLINK et à effectuer les opérations nécessaires à sa réalisation :

- lire et analyser l'ensemble du dépôt et son historique utile ;
- créer, modifier, déplacer et organiser les fichiers du projet ;
- installer ou récupérer les dépendances nécessaires depuis leurs sources officielles, en vérifiant leurs versions et empreintes ;
- écrire le code C++20, les scripts `.bat`, les scripts auxiliaires, les tests et la documentation ;
- compiler, exécuter les tests, analyser les erreurs, corriger et recommencer ;
- ajouter une CI Windows et les outils de validation nécessaires ;
- générer les packages et les releases locales versionnées.

Cette autorisation ne signifie pas qu'il faut détruire l'historique, écraser des données sans sauvegarde, publier des secrets, ou effectuer une publication externe non maîtrisée. Préserve le travail existant. N'utilise jamais `git reset --hard`, `git clean -fdx`, une suppression massive ou une réécriture de l'historique pour masquer un problème. Ne révèle, ne copie et ne journalise aucun secret, mot de passe ou jeton.

## 0.2 Règle de continuité — ne pas s'arrêter à une phase

- Ne t'arrête pas après l'audit.
- Ne t'arrête pas après la fondation du projet.
- Ne t'arrête pas après le premier écran SDL3/ImGui.
- Ne t'arrête pas après une compilation Linux.
- Ne laisse pas `package` ou `release-all` bloqués ou fictifs.
- Ne reporte pas les modules principaux à une « phase suivante ».
- Ne demande pas à l'utilisateur de choisir entre les travaux déjà spécifiés.
- Ne demande pas d'approbation pour les corrections normales, l'ajout de fichiers, les tests ou la création des artefacts locaux.

Tu dois poursuivre jusqu'à ce que tous les éléments réalisables soient effectivement implémentés, intégrés, testés et empaquetés.

Si une opération est impossible dans ton environnement (par exemple accès absent au Dell réel, au pilote AX201 ou à une session Windows native), continue tout le reste. Mets en place les tests automatisés, les mocks, les simulateurs, la CI Windows si le dépôt distant le permet, et les scripts de validation locale. Marque précisément ce qui n'a pas pu être exécuté. **Ne transforme jamais une limite d'environnement en faux résultat positif.**

## 0.3 Définition de « terminé »

Le projet n'est pas terminé parce que les fichiers existent ou parce que le compilateur a réussi à construire un simple écran. Il est terminé lorsque :

1. les principaux modules définis dans le cahier des charges sont réellement implémentés ;
2. ils sont intégrés dans une application utilisable ;
3. les états d'erreur et les capacités non disponibles sont gérés proprement ;
4. les tests automatisés disponibles passent ;
5. la compilation Windows x64 est validée sur un environnement Windows réel ou une CI Windows effective ;
6. `build.bat package` et `build.bat release-all` produisent des artefacts réels et vérifiés ;
7. le package peut être extrait et l'application lancée sur une machine compatible ;
8. la documentation décrit les commandes, les prérequis, les limites et les résultats de tests ;
9. le rapport final distingue clairement `PASS`, `FAIL`, `BLOCKED` et `NOT_RUN`.

Si l'accès au matériel Dell n'est pas disponible, tu peux achever le code et les packages, mais la validation de l'AX201 sur cette machine doit rester `NOT_TESTED_ON_HARDWARE`. Il est interdit de déclarer la validation matérielle terminée sans observation réelle.

---

# 1. CONTEXTE ET ÉTAT CONNU DU DÉPÔT

Un premier audit a déjà été rédigé dans `docs/reports/phase-0-1.md`. Il indiquait notamment :

- le dépôt de départ était presque vide, avec essentiellement un README ;
- l'environnement d'exécution de l'agent était une VM Debian x64, et non le Dell Windows 11 ;
- une fondation C++20 et un smoke test SDL3 hors écran ont été compilés et exécutés sous Debian en Debug et Release ;
- le traitement de fermeture d'une fenêtre SDL cachée a été testé dans les deux configurations ;
- des tests de codes de retour pour des arguments et pilotes invalides ont été réalisés ;
- l'analyse syntaxique de scripts PowerShell a été faite, mais leur exécution Windows native n'était pas validée ;
- `build.bat` proposait notamment `help`, `doctor`, `debug`, `release`, `test` et `clean` ;
- les commandes `package` et `release-all` étaient encore bloquées ; aucune archive de livraison n'avait été produite ;
- l'archive binaire SDL3 Windows VC x64 n'avait pas pu être récupérée dans cet environnement ; les empreintes des archives sources SDL3 et ImGui avaient été vérifiées ;
- le Dell Precision 7750, Windows 11, l'AX201 et le pilote n'avaient pas été testés physiquement.

Des fichiers de fondation peuvent déjà exister :

- `CAMPUSLINK_SPEC_ARENA.md` — cahier des charges de référence ;
- `docs/reports/phase-0-1.md` — rapport d'audit précédent ;
- `docs/decisions/0001-foundations.md` — décisions d'architecture ;
- `VERSION` — version du projet ;
- `deps.lock` — versions et empreintes de dépendances ;
- `build.bat` — chaîne de compilation initiale.

**Commence par inspecter l'état actuel réel de ces fichiers, de Git et du dépôt.** Le présent résumé n'est pas une permission d'ignorer les changements plus récents. Ne remplace pas aveuglément un fichier existant et ne suppose pas que le rapport précédent décrit encore exactement le dépôt.

Le produit visé est un gestionnaire intelligent de connectivité et un banc de recherche réseau. Il doit inventorier les capacités matérielles, simuler de nombreux clients dans un laboratoire logiciel, diagnostiquer les connexions Wi-Fi réelles, mesurer les performances et aider l'utilisateur à sélectionner une connexion utilisable par son ordinateur principal.

---

# 2. CONTRAINTES TECHNIQUES NON NÉGOCIABLES

| Élément | Décision |
|---|---|
| Langage principal | C++20 |
| Première cible | Windows 11 x64 |
| Machine de référence | Dell Precision 7750, modèle réel à détecter |
| Adaptateur de référence | Intel Wi-Fi 6 AX201, à détecter et confirmer |
| Fenêtrage et événements | SDL3 |
| Interface utilisateur | Dear ImGui |
| Rendu | Vulkan prioritaire ; repli SDL3 documenté si nécessaire |
| Compilation Windows | `build.bat` comme point d'entrée unique |
| Compilation du projet | commandes directes du compilateur et du linker |
| Générateurs interdits pour le projet | CMake, Make, Ninja, MSBuild |
| Sortie | application, tests, package ZIP, manifestes, sommes SHA-256, release locale |
| Portabilité | séparation stricte plateforme / cœur ; Linux ensuite |

Tu peux utiliser PowerShell comme outil auxiliaire Windows pour les tâches adaptées (inventaire, empreintes, création/validation ZIP, tests de scripts), mais `build.bat` reste l'interface de commande principale. Ne remplace pas la chaîne de compilation directe du projet par un générateur de build interdit.

Les dépendances doivent être verrouillées dans `deps.lock` avec version, source officielle, architecture, licence et SHA-256. N'utilise pas de paquet binaire dont la provenance ou l'empreinte ne peut pas être vérifiée. Respecte les licences et fournis `THIRD_PARTY_NOTICES.md`.

---

# 3. PRODUIT À LIVRER

CAMPUSLINK est une application desktop de recherche et de gestion de connectivité Wi-Fi. Elle doit proposer deux environnements explicitement séparés.

## 3.1 Virtual Lab — simulation logicielle

Créer des clients virtuels autonomes, pilotés par le moteur de simulation interne. Chaque client possède un identifiant, un profil, un état, une machine à états, des métriques et un historique.

La simulation doit modéliser au minimum :

- découverte d'un réseau ;
- tentative d'association ;
- acceptation ou refus d'association ;
- authentification réussie ou échouée ;
- échange DHCP simulé ;
- attribution, refus, expiration et renouvellement de bail simulés ;
- pool DHCP simulé limité ;
- conflit d'adresse simulé ;
- configuration IP invalide ;
- passerelle indisponible ;
- résolution DNS en échec ;
- perte de paquets, latence, jitter et débit simulés ;
- connexion stable, instable ou interrompue.

Les scénarios doivent être déterministes lorsqu'une seed est fournie, reproductibles, exportables et testables. Le système doit permettre de créer des lots de clients virtuels et de visualiser leur cycle de vie.

**Ces clients virtuels sont des objets logiciels du laboratoire.** Ils ne doivent pas être présentés comme des ordinateurs radio indépendants. La simulation DHCP et les scénarios de saturation se déroulent dans le moteur local, sans envoyer de rafales de demandes DHCP au réseau universitaire.

## 3.2 Physical Experiment — expérience réseau réelle

Cette partie utilise les véritables interfaces et identités de la machine, à travers les API du système d'exploitation et les profils Wi-Fi enregistrés. Elle doit pouvoir :

- inventorier les interfaces réelles ;
- détecter les réseaux et BSSID visibles ;
- lire les informations de signal et les capacités exposées par le pilote ;
- examiner l'état de l'association et de l'authentification ;
- diagnostiquer l'obtention ou l'absence d'une configuration IP ;
- vérifier passerelle, DNS et connectivité ;
- mesurer la qualité d'une connexion à l'aide de sondes contrôlées ;
- effectuer une connexion ou un changement de réseau à travers les API natives, avec les profils autorisés de Windows ;
- valider à nouveau la connexion réelle après sélection.

Pour les réseaux réels, respecte une portée d'expérimentation explicitement configurée : SSID/BSSID autorisés, serveur de benchmark autorisé, cadence et volume de tests. Les tests de charge DHCP réels doivent utiliser un point d'accès, un VLAN et un serveur DHCP de laboratoire isolés, dédiés à cet usage.

Ne développe pas de fonctions de désauthentification, de déconnexion forcée d'autres clients, de DHCP starvation, d'usurpation d'adresse MAC/IP, de collecte de baux appartenant à des tiers, de contournement d'authentification ou de portail captif, de balayage agressif de sous-réseaux, ni de récupération d'identifiants. Les scénarios correspondants restent simulés dans le Virtual Lab ou exécutés sur un laboratoire isolé dont les équipements sont dédiés à l'essai.

---

# 4. HARDWARE INTELLIGENCE ENGINE

C'est un module central et non une simple fenêtre affichant le nom de la carte.

## 4.1 Inventaire matériel

Détecter et exposer, lorsque Windows les fournit :

- nom commercial et description de l'adaptateur ;
- fabricant, identifiant matériel/PnP, identifiant d'interface et GUID ;
- type d'interface : physique ou virtuelle ;
- état : présent, activé, désactivé, connecté, déconnecté ;
- fournisseur et version du pilote, date si disponible ;
- capacités PHY et normes annoncées par le pilote ;
- bandes, canaux et largeur de canal rapportés ;
- puissance/signal RSSI et BSSID courant ;
- débit de liaison courant lorsqu'il est accessible ;
- informations de bus ou de périphérique lorsque le système les expose ;
- fonctionnalités Wi-Fi Direct et rôles secondaires lorsqu'elles sont réellement annoncées.

Utiliser les API Windows adaptées, notamment WLAN, SetupAPI/Configuration Manager et IP Helper. Encapsuler les API dans des adaptateurs testables et gérer proprement les erreurs, les pointeurs et les allocations spécifiques Windows.

## 4.2 Analyse des capacités et de la concurrence

Le moteur doit distinguer quatre catégories :

- `CONFIRMED` : capacité effectivement observée et vérifiée ;
- `DRIVER_REPORTED` : capacité annoncée par le pilote ou le système ;
- `MEASURED` : capacité observée lors d'un test réel ;
- `UNKNOWN` : information non exposée ou non vérifiable.

Examiner les informations de capacité WLAN, les PHY déclarés et les interfaces secondaires STA si l'OS et le pilote exposent cette information. Étudier séparément les rôles STA, STA secondaire et Wi-Fi Direct. Construire une matrice de concurrence et documenter les combinaisons réellement supportées.

**Ne déduis jamais un nombre de connexions simultanées à partir du 2x2, de MU-MIMO, d'OFDMA, du débit PHY ou du nombre d'antennes.** Si l'API ne fournit pas de plafond fiable, indique `UNKNOWN` et explique ce qui manque. Ne code aucun plafond matériel fixe pour l'AX201.

## 4.3 Simulation Capacity Profiler

Estimer séparément la capacité logicielle du Virtual Client Engine par un benchmark progressif local :

- mesurer le coût mémoire et CPU de chaque client simulé ;
- augmenter progressivement le nombre d'instances ;
- mesurer les événements traités par seconde, la latence de simulation et la stabilité ;
- arrêter proprement le benchmark lorsqu'un budget CPU, mémoire ou temps est atteint ;
- permettre de configurer les budgets ;
- préserver l'interface et le système ;
- enregistrer la méthode, la machine, les paramètres et les résultats.

Afficher deux plafonds séparés : `LOGICAL_SIMULATION_CAPACITY` et `PHYSICAL_WIRELESS_CONCURRENCY`. Ne jamais présenter le premier comme le second.

---

# 5. NETWORK DISCOVERY & DIAGNOSTICS

Implémenter une découverte fiable et non bloquante : SSID, BSSID, sécurité annoncée, fréquence/bande, canal, RSSI et informations disponibles. Distinguer plusieurs BSSID qui diffusent le même SSID.

Le moteur de diagnostic doit fournir des étapes séparées et un statut pour chacune :

1. adaptateur disponible ;
2. réseau détecté ;
3. association radio ;
4. authentification ;
5. configuration IP/DHCP ;
6. passerelle/ARP ;
7. DNS ;
8. connectivité IP ;
9. connectivité applicative.

Ne résume pas tous les échecs par « Pas d'Internet ». Produis une cause probable, le niveau de confiance, les preuves observées et les vérifications recommandées. Distingue un échec d'une étape non vérifiable.

Prendre en charge les cas : pas de réponse DHCP, adresse automatique/APIPA, passerelle absente, DNS indisponible, conflit IP suspecté, perte de paquets, filtrage ou isolation réseau. N'affirme pas une cause certaine lorsque les observations ne permettent qu'une hypothèse.

---

# 6. VIRTUAL CLIENT ENGINE

Créer une architecture extensible et testable :

- `VirtualClient` ;
- `VirtualClientProfile` ;
- `ClientStateMachine` ;
- `VirtualNetworkBackend` ;
- `DhcpSimulator` ;
- `ScenarioEngine` ;
- `SimulationScheduler` ;
- `SimulationMetrics` ;
- `ScenarioRecorder` et `ScenarioReplay`.

Le cycle d'état doit être explicite, sans transitions implicites impossibles à tester. Les opérations de simulation doivent être découplées de l'UI. Le scheduler ne doit pas créer un thread par client par défaut : choisir une approche scalable (boucle d'événements, tâches ou pool borné) et la justifier par des mesures.

Proposer des scénarios préconfigurés :

- réseau sain ;
- pool DHCP simulé épuisé ;
- serveur DHCP simulé indisponible ;
- bail expiré ;
- configuration IP incorrecte ;
- passerelle inaccessible ;
- DNS lent ou indisponible ;
- signal faible ;
- congestion simulée ;
- pertes et jitter ;
- accès Internet fonctionnel mais instable.

Créer les tests déterministes de chaque scénario. Le mode simulation doit fonctionner sans matériel Wi-Fi et sans accès Internet.

---

# 7. CONNECTION MANAGER & HOST HANDOFF

Le candidat sélectionné doit être représenté par un objet contenant au minimum : SSID, BSSID lorsque disponible, profil Windows à utiliser, niveau de sécurité, résultats de diagnostic, résultats de benchmark, horodatages et score. Ne jamais stocker le mot de passe du réseau en clair ni le recopier dans les logs.

Le transfert vers l'ordinateur principal doit fonctionner ainsi :

1. sélectionner un candidat mesuré et admissible ;
2. utiliser le profil Wi-Fi enregistré et autorisé de Windows ;
3. demander à Windows de se connecter au SSID/BSSID sélectionné par l'API native ;
4. laisser la pile réseau de Windows obtenir sa propre configuration IP ;
5. vérifier association, authentification, adresse IP, passerelle, DNS et connectivité ;
6. confirmer le succès ou revenir au profil précédent connu comme fonctionnel ;
7. enregistrer la raison du succès ou de l'échec.

**Ne transfère pas l'adresse IP ou le bail DHCP d'un client test vers l'ordinateur hôte.** Un bail appartient à l'identité et au contexte du client qui l'a obtenu. Le candidat transféré est le profil/point d'accès et son contexte de qualité ; l'ordinateur réel doit recevoir sa propre configuration réseau via Windows. Si une adresse fixe est nécessaire dans un laboratoire, elle doit être explicitement configurée pour l'hôte ou réservée côté DHCP par l'administrateur.

Avec un seul adaptateur STA, ne prétends pas avoir plusieurs connexions physiques simultanées. Le test réel utilise l'interface réelle et le système hôte. Si plusieurs adaptateurs physiques indépendants sont détectés, les tests peuvent être parallélisés dans la limite des capacités observées. Si le pilote prend effectivement en charge plusieurs interfaces STA simultanées, utiliser uniquement les interfaces déclarées par Windows.

Prévoir :

- politique de retry bornée et backoff ;
- cooldown ;
- hystérésis pour éviter les basculements permanents ;
- amélioration minimale requise avant bascule ;
- durée minimale d'observation ;
- protection contre le network flapping ;
- bouton pause/arrêt immédiat ;
- mode manuel et mode automatique clairement identifiés ;
- conservation d'un profil de retour ;
- aucune boucle infinie de reconnexion.

---

# 8. PERFORMANCE INTELLIGENCE ENGINE

Mesurer séparément :

- débit descendant ;
- débit montant ;
- latence ;
- jitter ;
- pertes de paquets ;
- temps de résolution DNS ;
- qualité du signal ;
- stabilité et variations temporelles ;
- disponibilité de la passerelle et du serveur de test.

Utiliser un endpoint de test configurable et autorisé. Prévoir une intégration avec `iperf3` lorsque l'utilisateur dispose d'un serveur administré, ou un protocole de test documenté vers un serveur de benchmark contrôlé. Ne génère pas de trafic massif ni de tests non bornés. Configurer le nombre d'itérations, le volume, le timeout et la cadence ; afficher ces limites dans l'UI.

Créer un score de qualité versionné et configurable. Point de départ proposé : stabilité 30 %, débit descendant 25 %, latence 20 %, pertes 15 %, jitter 10 %. Normaliser les indicateurs avant pondération ; un indicateur absent ne doit pas être remplacé silencieusement par une valeur favorable. Afficher le détail des composantes et le niveau de confiance du score.

La sélection doit privilégier une connexion réellement fonctionnelle et stable, pas le débit instantané le plus élevé. Effectuer une validation post-handoff et recalculer le score avec les observations de l'ordinateur hôte.

---

# 9. INTERFACE SDL3 + DEAR IMGUI

Livrer une véritable interface desktop fonctionnelle, pas une image ou une maquette.

## 9.1 Intégration

- SDL3 pour fenêtre, événements, entrées et cycle de vie ;
- Dear ImGui avec backend SDL3 ;
- backend Vulkan prioritaire ;
- backend SDL3 de repli si Vulkan ou le pilote graphique est indisponible ;
- gestion propre du redimensionnement, DPI, fermeture, focus et perte/recréation du device ;
- boucle de rendu stable ;
- tâches réseau et benchmarks en arrière-plan, jamais dans le thread UI.

Le logiciel doit démarrer même si aucun adaptateur Wi-Fi compatible n'est détecté : l'UI doit expliquer la situation et le Virtual Lab doit rester utilisable.

## 9.2 Écrans obligatoires

1. **Overview** : état général, adaptateur actif, réseau, statut Internet, score courant.
2. **Hardware Lab** : inventaire et diagnostics matériel.
3. **Radio Capabilities** : capacités déclarées, mesurées, inconnues et matrice de concurrence.
4. **Virtual Client Lab** : création, configuration, démarrage, arrêt et suivi des clients virtuels.
5. **Network Discovery** : réseaux, BSSID, sécurité, bandes et RSSI.
6. **Experiment Center** : scénarios, portée autorisée, paramètres et contrôle des expériences.
7. **Performance Lab** : graphiques de débit, latence, jitter, pertes et historique.
8. **Intelligence Engine** : classement, pondération, confiance et justification du candidat.
9. **Connection Manager** : choix manuel/automatique, connexion, état du handoff, rollback et pause.
10. **Logs & Diagnostics** : événements structurés, filtres, export et détails d'erreurs.
11. **Settings** : budgets de simulation, endpoints, limites de benchmark, préférences et dossiers.

## 9.3 Direction visuelle

Interface professionnelle, mature et cohérente : hiérarchie typographique claire, espacement maîtrisé, tableaux lisibles, graphiques exploitables, états de santé non ambigus, animations sobres et navigation stable. Palette sombre par défaut avec accents subtils vert, rouge et jaune ; ne pas surcharger l'interface. Prévoir un thème clair si cela peut être réalisé sans fragiliser le produit.

Chaque écran doit afficher des données réelles ou signaler clairement `DEMO`, `SIMULATED`, `UNAVAILABLE` ou `NOT TESTED`. Ne remplis jamais le dashboard avec des valeurs fictives présentées comme des mesures réelles.

---

# 10. ARCHITECTURE C++ ET QUALITÉ

Séparer strictement :

- `core/` : cycle de vie, événements, tâches, erreurs, configuration, journalisation ;
- `hardware/` : inventaire et capacité ;
- `network/` : découverte, diagnostic, gestion des connexions ;
- `simulation/` : clients virtuels, DHCP simulé et scénarios ;
- `performance/` : mesures et scoring ;
- `intelligence/` : classement et décision ;
- `platform/windows/` : implémentations Windows ;
- `platform/linux/` : abstraction et emplacement d'implémentation ultérieure ;
- `ui/` : SDL3, ImGui, panels, widgets et thèmes ;
- `tests/` : tests unitaires, intégration, simulation et tests conditionnés par le matériel.

Exigences :

- C++20 propre et idiomatique ;
- RAII et gestion sûre des handles Windows ;
- pas de fuite mémoire ni de thread orphelin ;
- annulation propre des tâches et arrêt déterministe ;
- aucun appel système bloquant dans le thread UI ;
- erreurs structurées avec contexte et code ;
- pas de `catch (...)` silencieux ;
- validation des entrées et des fichiers de configuration ;
- logs horodatés et niveaux configurables ;
- configuration versionnée et migrations si nécessaire ;
- pas de données sensibles dans les journaux ;
- pas de fonctions vides, de TODO laissés pour les fonctions essentielles ou de valeurs codées en dur qui prétendent décrire le matériel.

Toute abstraction doit avoir un objectif concret. Évite le surdesign et les couches inutiles. Utilise des interfaces testables là où elles permettent de substituer le système réel par un mock.

---

# 11. BUILD.BAT — EXIGENCE PRIORITAIRE

Le fichier `build.bat` doit devenir une véritable chaîne de compilation et de livraison. Il doit être propre, robuste, lisible et professionnel.

## 11.1 Commandes obligatoires

```bat
build.bat help
build.bat doctor
build.bat debug
build.bat release
build.bat test
build.bat package
build.bat release-all
build.bat clean
```

Comportement :

- `help` : aide claire, exemples, prérequis et codes de sortie ;
- `doctor` : détecter OS, architecture, Clang, linker, Windows SDK, bibliothèques système, SDL3, Vulkan SDK/runtime, dépendances, versions et chemins ;
- `debug` : compiler et lier toute l'application en Debug ;
- `release` : compiler et lier l'application optimisée ;
- `test` : compiler et exécuter tous les tests disponibles ;
- `package` : exécuter les prérequis nécessaires, produire le package portable et le valider ;
- `release-all` : vérifier VERSION, compiler Release, exécuter les tests, packager, générer les empreintes et le manifeste, puis produire le rapport de release ;
- `clean` : nettoyer seulement les sorties générées par CAMPUSLINK, jamais les sources ou les données utilisateur.

Aucune commande de livraison ne doit être un simple message « bientôt disponible », un exit code de succès artificiel ou un stub.

## 11.2 Interface console

Le script doit présenter une interface console soignée :

- titre ASCII CAMPUSLINK ;
- version et architecture détectées ;
- étapes numérotées ;
- état `RUNNING`, `PASS`, `WARN`, `FAIL`, `SKIP` ;
- durée de chaque étape ;
- code de retour en cas d'échec ;
- résumé final ;
- couleurs ANSI si le terminal les accepte ;
- repli lisible sans ANSI ;
- UTF-8 activé proprement, avec repli ASCII si l'encodage n'est pas fiable ;
- logs détaillés dans `logs/build/`.

Le script doit fonctionner depuis un chemin contenant des espaces, quelle que soit la lettre du lecteur, et depuis un dossier dont le chemin contient des caractères non ASCII si l'environnement le permet.

## 11.3 Toolchain

Détecter et valider la toolchain réellement disponible. La cible Windows x64 doit être cohérente avec l'ABI et les bibliothèques utilisées. Utiliser Clang compatible avec l'écosystème MSVC, le Windows SDK et le linker requis. Ne suppose pas que la simple présence de `clang++` suffit. Vérifie les headers, bibliothèques, linker et runtime.

Documenter précisément les composants requis. Ne compile pas silencieusement une cible MinGW si le package attendu est MSVC x64. Ne masque pas un échec du linker.

---

# 12. PACKAGING ET RELEASES — À RÉALISER RÉELLEMENT

Les commandes `package` et `release-all` sont obligatoires et doivent produire des fichiers réels.

## 12.1 Package portable

Générer une archive au format ZIP, par exemple :

`dist/packages/CAMPUSLINK-<VERSION>-windows-x64-portable.zip`

Le package doit contenir au minimum :

- exécutable CAMPUSLINK ;
- DLL runtime nécessaires, dont SDL3 si linkage dynamique ;
- assets et ressources réellement utilisés ;
- configuration d'exemple sans secret ;
- README d'installation et de démarrage ;
- notes de version ;
- licences et `THIRD_PARTY_NOTICES.md` ;
- informations de build et de compatibilité.

Ne distribue pas de DLL de développement inutiles. Vérifie les dépendances dynamiques du binaire et documente celles qui doivent être fournies par le système ou le pilote graphique.

## 12.2 Manifestes et intégrité

Produire :

- manifeste JSON avec version, plateforme, architecture, date, commit, compilateur, dépendances et fichiers ;
- sommes SHA-256 ;
- informations de build ;
- notes de version ;
- package de symboles séparé si les PDB sont disponibles et utiles ;
- SBOM ou inventaire des dépendances.

Vérifier automatiquement que le ZIP s'ouvre, que son contenu correspond au manifeste et que les empreintes sont correctes. Effectuer un test d'extraction dans un dossier temporaire et un smoke test de lancement lorsque l'environnement le permet.

## 12.3 Release locale et publication distante

La release locale versionnée est obligatoire. Une publication GitHub distante ne peut être réalisée que si le dépôt distant, les permissions et la cible de publication sont déjà configurés. Ne publie aucun secret. Ne supprime aucune release ou tag existant. Ne crée pas de release publique en prétendant que les vérifications matérielles ont été faites si elles ne l'ont pas été.

---

# 13. TESTS ET VALIDATION

Créer une suite de tests réellement exécutée, pas seulement compilée.

## 13.1 Unit tests

Couvrir au minimum :

- machines à états des clients virtuels ;
- transitions valides et invalides ;
- simulateur DHCP ;
- pools limités, baux, expiration et conflits simulés ;
- métriques et normalisation ;
- score et classement ;
- scheduler et annulation ;
- retry/backoff/hystérésis ;
- validation de configuration ;
- sérialisation et lecture des scénarios ;
- gestion des erreurs et timeouts.

## 13.2 Integration tests

Utiliser des interfaces simulées/mocks pour valider :

- inventaire et capacité ;
- découverte réseau ;
- diagnostic par étape ;
- sélection de candidat ;
- handoff simulé ;
- retour arrière ;
- flux de données vers l'UI ;
- comportement sans adaptateur Wi-Fi ;
- comportement sans Vulkan ;
- fonctionnement du Virtual Lab sans réseau.

## 13.3 Windows tests

Sur un runner ou une machine Windows effective :

- compiler Debug et Release ;
- exécuter les tests ;
- lancer `doctor` ;
- lancer l'application ;
- valider les DLL et le package ;
- exécuter les scripts PowerShell ;
- vérifier les chemins avec espaces ;
- vérifier les codes d'erreur et les logs.

Créer une GitHub Actions Windows x64 si cela est possible dans le dépôt, en utilisant une toolchain reproductible et les dépendances verrouillées. Faire exécuter les commandes réelles `build.bat` par le runner, puis conserver les logs et artefacts.

## 13.4 Tests matériels

Sur le Dell réel, si accessible : identifier le modèle, la carte, la version du pilote, l'OS et les capacités annoncées. Mesurer seulement les opérations permises par le matériel et la portée de test configurée. Si le matériel n'est pas accessible, écrire `scripts/audit-windows.ps1` et les tests locaux nécessaires, puis indiquer précisément `NOT_TESTED_ON_HARDWARE`.

Ne transforme jamais un test simulé en preuve matérielle.

---

# 14. ROBUSTESSE, PERFORMANCE ET SÉCURITÉ OPÉRATIONNELLE

- L'application reste réactive sous forte charge de simulation.
- Le nombre de tâches et de threads est borné.
- L'arrêt d'une simulation ou d'un benchmark doit être immédiat et propre.
- Les tests de débit ont des plafonds de durée et de volume.
- L'application n'exige pas les droits administrateur pour les fonctions qui n'en ont pas besoin.
- Si une opération exige une élévation, demander uniquement cette élévation, expliquer sa raison et ne jamais l'activer en permanence.
- Aucune opération de test ne doit affecter des clients tiers ou épuiser les ressources du réseau partagé.
- Les fonctions de simulation et de test réel doivent être visuellement impossibles à confondre.
- L'application doit gérer proprement un adaptateur absent, désactivé, occupé ou incompatible.
- Gérer les pertes du service WLAN AutoConfig, les erreurs API, les timeouts et les déconnexions.
- Les logs ne doivent contenir aucun mot de passe Wi-Fi, jeton ou secret.

---

# 15. DOCUMENTATION OBLIGATOIRE

Mettre à jour ou créer :

- `README.md` : présentation, prérequis, installation, usage rapide ;
- `docs/ARCHITECTURE.md` : architecture réellement implémentée ;
- `docs/BUILD_WINDOWS.md` : préparation de Windows 11 et commandes `build.bat` ;
- `docs/BUILD_RELEASE.md` : packaging et release ;
- `docs/USER_GUIDE.md` : parcours de l'application ;
- `docs/HARDWARE_CAPABILITIES.md` : distinction capacités confirmées/annoncées/inconnues ;
- `docs/NETWORK_DIAGNOSTICS.md` : états et interprétation des diagnostics ;
- `docs/VIRTUAL_LAB.md` : scénarios et moteur de simulation ;
- `docs/TEST_PLAN.md` : tests et résultats ;
- `docs/KNOWN_LIMITATIONS.md` : limites connues et compatibilité ;
- `CHANGELOG.md` ;
- `THIRD_PARTY_NOTICES.md` ;
- `docs/reports/FINAL_DELIVERY_REPORT.md`.

La documentation doit décrire le code livré, pas une architecture qui n'existe pas encore.

---

# 16. MÉTHODE D'EXÉCUTION AUTONOME

Exécute la mission entière dans la session actuelle. Utilise cette séquence comme ordre de travail interne, sans pause de validation entre les étapes :

1. Inspecter Git, le dépôt, les fichiers déjà modifiés et les rapports existants.
2. Établir les capacités exactes de l'environnement de développement et les dépendances disponibles.
3. Résoudre les problèmes de fondation et de dépendances sans effacer le travail existant.
4. Finaliser la chaîne `build.bat` et faire fonctionner Debug/Release/Test.
5. Implémenter le Hardware Intelligence Engine.
6. Implémenter les abstractions réseau et Windows.
7. Implémenter Virtual Client Engine et les scénarios DHCP simulés.
8. Implémenter diagnostics, gestion des connexions et handoff.
9. Implémenter les benchmarks et le moteur de sélection.
10. Construire l'interface complète SDL3 + Dear ImGui.
11. Intégrer les modules et gérer tous les états d'erreur.
12. Écrire et exécuter les tests unitaires et d'intégration.
13. Créer/faire exécuter la CI Windows si possible.
14. Corriger tous les défauts détectés et relancer les tests pertinents.
15. Finaliser la documentation.
16. Implémenter `package` et `release-all`.
17. Générer le package, les empreintes, le manifeste et les notes de version.
18. Valider l'extraction et le lancement du package lorsque possible.
19. Rédiger le rapport final honnête avec les preuves.

Ne fais pas de commit artificiel à chaque étape. Si le dépôt utilise déjà une convention de branches ou de commits, respecte-la. Ne détruis pas les modifications préexistantes de l'utilisateur.

Si une dépendance est inaccessible, tente une source officielle alternative ou une méthode officielle reproductible. Vérifie systématiquement les empreintes. Si aucune méthode sûre ne fonctionne, implémente ce qui peut l'être, documente précisément le bloqueur et fournis le script de récupération reproductible. N'utilise jamais une DLL inconnue simplement pour faire passer le build.

---

# 17. RAPPORT FINAL OBLIGATOIRE

À la fin, produire `docs/reports/FINAL_DELIVERY_REPORT.md` avec :

1. résumé exécutif ;
2. état initial réellement observé ;
3. architecture livrée ;
4. modules effectivement implémentés ;
5. fichiers majeurs créés/modifiés ;
6. commandes exactes exécutées ;
7. environnement exact : OS, version, architecture, compilateur, SDK ;
8. résultats des tests avec nombre réussi/échoué/non exécuté ;
9. résultats Windows natifs et CI ;
10. statut matériel Dell/AX201 ;
11. limitations et raisons ;
12. chemins exacts des packages et releases ;
13. empreintes SHA-256 ;
14. procédure d'installation et de lancement ;
15. liste des points restant réellement à vérifier.

Pour chaque vérification, utiliser l'un des états suivants :

- `PASS` — exécuté et réussi, avec preuve ;
- `FAIL` — exécuté et échoué ;
- `BLOCKED` — impossible à exécuter en raison d'une dépendance ou d'un accès absent ;
- `NOT_RUN` — non exécuté ;
- `NOT_TESTED_ON_HARDWARE` — matériel réel non accessible.

Joindre les chemins des journaux et les sorties utiles. N'invente ni métrique, ni résultat de benchmark, ni succès de compilation, ni validation Windows, ni preuve matérielle.

---

# 18. MESSAGE DE DÉMARRAGE — ACTION IMMÉDIATE

Commence maintenant par inspecter l'état réel du dépôt et de l'environnement, puis exécute l'intégralité de cette mission. Le rapport `phase-0-1.md` est un point de départ, pas une raison de s'arrêter. Le fait que ton environnement soit une VM Debian ne t'autorise pas à livrer uniquement un prototype Linux : tu dois produire le code Windows, les scripts Windows, les tests, la CI Windows si elle est possible, le package et tous les documents réalisables.

**Ne me renvoie pas un nouveau plan de phases. Ne me demande pas de valider chaque phase. Ne considère pas l'audit ou le smoke test SDL3 comme une livraison. Implémente, intègre, teste, corrige, package et livre.**

À la fin, je veux une application CAMPUSLINK réellement utilisable, une chaîne `build.bat` professionnelle, des packages versionnés et un rapport d'acceptation transparent. La seule chose que tu ne dois jamais faire est de prétendre avoir testé sur Windows 11 ou sur le Dell si tu n'as pas réellement eu accès à cet environnement.
