# CAMPUSLINK — Spécification technique et cahier des charges

**Document de référence pour Arena AI**  
**Version du document :** 1.0  
**Version logicielle initiale visée :** 0.1.0  
**Cible initiale :** Windows 11 x64  
**Machine de référence :** Dell Precision 7750  
**Adaptateur de référence déclaré :** Intel Wi-Fi 6 AX201 160 MHz  
**Langage :** C++20  
**Interface :** SDL3 + Dear ImGui  
**Rendu privilégié :** Vulkan  
**Compilation Windows :** script natif `build.bat`, compilation directe, sans CMake/Make/Ninja/MSBuild pour le projet CAMPUSLINK  
**Stratégie OS :** Windows d'abord, Linux ensuite

---

## 0. Instruction principale à Arena AI

Tu agis comme architecte logiciel senior, ingénieur C++20, ingénieur réseau Windows et responsable qualité/release. Tu dois concevoir et implémenter CAMPUSLINK comme un véritable logiciel de recherche et de diagnostic de connectivité, robuste, mesurable, documenté et testable.

Tu as l'autorisation de créer et modifier les fichiers du dépôt nécessaires au projet : code source, tests, scripts de compilation, documentation, configurations, ressources et scripts de packaging. Tu dois cependant préserver les fichiers et fonctionnalités préexistants qui ne sont pas concernés. N'effectue aucune suppression massive, réinitialisation de dépôt, écrasement de données utilisateur ou opération destructive sans sauvegarde et justification explicite.

### Règles impératives de travail

1. **Commencer par l'audit du dépôt et de l'environnement.** Identifier l'état Git, les fichiers existants, le compilateur, le Windows SDK, SDL3, Vulkan SDK, les dépendances, les versions et les scripts présents. Ne pas supposer qu'un composant est installé.
2. **Établir un état initial vérifiable.** Noter les commandes réellement exécutées, les versions détectées, les erreurs préexistantes et les tests qui passent ou échouent avant toute modification.
3. **Ne jamais fabriquer de résultats.** Un test non exécuté doit être marqué `NOT_RUN`; une fonctionnalité non testée sur le matériel doit être `NOT_TESTED_ON_HARDWARE`. Ne jamais présenter une compilation supposée comme réussie.
4. **Procéder par phases verticales et cohérentes.** Chaque phase doit laisser le dépôt compilable ou expliquer précisément le blocage. Éviter les grands changements non vérifiés.
5. **Respecter C++20.** RAII, ownership explicite, types forts, `std::jthread`/`std::stop_token` lorsque pertinents, gestion systématique des erreurs, absence de data races et de blocage de l'interface.
6. **Séparer strictement le cœur, la plateforme, le réseau, la simulation et l'interface.** Le code SDL3/ImGui ne doit jamais appeler directement les API Wi-Fi Windows.
7. **Ne pas inventer les capacités matérielles.** Les capacités doivent être classées `CONFIRMED`, `DRIVER_REPORTED`, `INFERRED` ou `UNKNOWN`, avec la source de la mesure.
8. **Ne jamais confondre simulation et connexion physique.** Un client virtuel du simulateur est un objet logiciel. Il ne possède pas une radio indépendante et ne peut pas être présenté comme un ordinateur réellement associé au point d'accès.
9. **Respecter la sémantique DHCP.** Une adresse IPv4 obtenue par un client de test n'est pas un jeton transférable vers le PC hôte. Le handoff transmet le profil et les paramètres de sélection ; Windows établit ensuite sa propre connexion et obtient sa propre configuration IP par le mécanisme réseau normal.
10. **Tests réseau actifs limités au périmètre configuré.** En mode réel, utiliser uniquement les profils et cibles autorisés, un rythme borné, des sondes de faible volume et des serveurs de benchmark explicitement configurés. Les essais de saturation, les identités usurpées, les conflits IP provoqués, les demandes DHCP massives, les désauthentifications et les contournements d'authentification ne font pas partie du produit. Leur comportement peut être étudié dans le simulateur hors ligne ou un laboratoire isolé.
11. **Aucune escalade inutile.** L'application doit fonctionner en utilisateur standard pour l'inventaire, la découverte et les diagnostics disponibles. Si une opération exige des privilèges, l'indiquer et l'isoler ; ne pas demander systématiquement l'exécution en administrateur.
12. **Livrer les preuves.** Fournir logs de build, rapports de tests, captures si utiles, manifeste de package, sommes SHA-256 et rapport final avec les limites restantes.

À la fin de chaque phase, fournir : fichiers créés/modifiés, décisions prises, commandes réellement exécutées, résultats réels, défauts restants, risques et prochaine phase. Ne jamais annoncer « terminé à 100 % » sans critères d'acceptation exécutés.

---

# 1. Vision et objectifs

CAMPUSLINK est un **gestionnaire intelligent de connectivité et un laboratoire de recherche réseau**. Il inspecte les adaptateurs disponibles, découvre les réseaux Wi-Fi visibles, diagnostique les échecs de connexion, mesure la qualité des connexions autorisées, classe les candidats et demande au système d'exploitation de connecter l'ordinateur au meilleur candidat validé.

Il comprend deux environnements nettement séparés :

- **Virtual Lab :** simulation logicielle de nombreux clients, de leurs états réseau et de scénarios DHCP/connectivité. Ce mode fonctionne hors ligne, sans envoyer de trafic de simulation au réseau réel.
- **Physical Experiment :** découverte et diagnostic d'interfaces réelles, essais de connexion avec les profils autorisés, mesures bornées et validation de la connexion de l'hôte.

Le produit doit être utile avec un seul adaptateur Wi-Fi, puis s'adapter automatiquement à la présence éventuelle d'autres adaptateurs physiques.

## 1.1 Objectifs prioritaires

- Inventorier toutes les interfaces réseau physiques et virtuelles.
- Identifier le modèle, le fournisseur, l'identifiant matériel, le pilote et ses capacités exposées.
- Déterminer les capacités radio et la concurrence réellement annoncées par le pilote/système, sans déduire un nombre de connexions du seul 2x2, MU-MIMO, OFDMA ou débit PHY.
- Calculer un plafond de simulation logicielle par benchmark progressif des ressources.
- Découvrir les BSS/points d'accès visibles et leurs caractéristiques rapportées.
- Diagnostiquer précisément les étapes : découverte, association, authentification, configuration IP/DHCP, passerelle, DNS et Internet.
- Mesurer débit, latence, jitter, pertes, DNS et stabilité avec des sondes configurables.
- Classer les connexions candidates de façon explicable.
- Sélectionner une connexion et demander à Windows de s'y connecter automatiquement avec le profil autorisé.
- Valider la connexion réelle après sélection et revenir à un profil précédent en cas d'échec selon la politique configurée.
- Simuler des clients et des scénarios de saturation DHCP dans un environnement isolé, sans produire ce trafic sur le réseau de campus.
- Fournir des packages portables et des releases Windows versionnés et reproductibles via `build.bat`.

## 1.2 Hors périmètre de la version 0.1.0

- Créer de véritables ordinateurs Wi-Fi indépendants à partir d'une seule carte physique.
- Faire passer plusieurs clients logiciels pour autant de stations radio indépendantes.
- Transférer un bail DHCP ou une adresse IP obtenue par un client de test vers l'hôte.
- Générer un trafic de saturation DHCP/association sur un réseau de production.
- Usurper des adresses MAC/IP, contourner un portail captif ou l'authentification du réseau.
- Capturer/déchiffrer le trafic d'autres utilisateurs.
- Prétendre mesurer simultanément plusieurs connexions radio avec un seul adaptateur qui n'expose qu'une interface STA active.
- Support Linux complet dans la première release Windows.

---

# 2. Plateforme matérielle et hypothèses

## 2.1 Machine de référence

Le nom commercial retenu est **Dell Precision 7750**. « E7750 » semble être une désignation approximative ; Arena doit identifier le modèle réel avec les informations système et ne doit pas figer de données non vérifiées.

Le support Dell de la Precision 7750 référence l'Intel Wi-Fi 6 AX201 comme configuration de module WLAN. Le matériel effectivement présent doit tout de même être découvert à l'exécution.

## 2.2 Intel Wi-Fi 6 AX201 — caractéristiques de référence

La fiche Intel annonce notamment :

- Wi-Fi 6 / IEEE 802.11ax ;
- 2x2 flux TX/RX ;
- bandes 2,4 GHz et 5 GHz ;
- largeur de canal jusqu'à 160 MHz sur 5 GHz ;
- débit PHY maximal annoncé jusqu'à 2,4 Gbit/s ;
- MU-MIMO et OFDMA ;
- interface système CNVio2.

**Ces valeurs sont des caractéristiques du produit, pas une mesure du débit Internet réel ni une preuve de connexions STA simultanées.** Le débit réel dépend du point d'accès, de la configuration, du signal, du canal, du pilote, de la contention et du service distant.

## 2.3 Quatre métriques de capacité à ne jamais fusionner

1. `logical_simulation_capacity` : nombre de clients d'état logiciel que le moteur peut faire évoluer sous la charge CPU/RAM mesurée.
2. `physical_adapter_count` : nombre d'adaptateurs physiques réellement détectés.
3. `concurrent_sta_capability` : nombre/capacité de connexions station simultanées si le système et le pilote fournissent une preuve exploitable ; sinon `UNKNOWN`.
4. `supported_role_combinations` : combinaisons de rôles déclarées par le pilote/système (STA, STA secondaire, Wi-Fi Direct, etc.), avec source et niveau de confiance.

Ne jamais transformer `2x2`, MU-MIMO, OFDMA ou le nombre d'antennes en nombre de clients simulables ou de connexions STA simultanées.

---

# 3. Environnements d'exécution et modes

L'interface doit afficher clairement le mode actif et ses limites.

## 3.1 Observe Mode

- Lecture des adaptateurs et états système.
- Scan/discovery via les API natives disponibles.
- Aucune tentative de connexion déclenchée par le moteur.
- Affichage des BSSID, SSID, RSSI, canal/fréquence, sécurité et données disponibles.
- Les données indisponibles sont affichées comme `N/A`, pas inventées.

## 3.2 Physical Experiment Mode

- Utilise les interfaces physiques réellement détectées.
- Connexion uniquement avec un profil Windows déjà enregistré ou une configuration d'accès explicitement fournie par l'utilisateur autorisé.
- Respecte un délai minimal configurable entre tentatives et un nombre maximal d'essais.
- Réalise des diagnostics réseau de faible volume.
- Effectue un benchmark uniquement sur une cible de test configurée et autorisée.
- Ne crée pas de clients fictifs sur le réseau de production.
- N'essaie pas des plages d'adresses IP arbitraires et ne réutilise pas un bail appartenant à un autre client.

## 3.3 Virtual Lab Mode

- Simule des clients en mémoire avec des identifiants logiques propres au simulateur.
- Simule les états association/authentification/DHCP/IP/passerelle/DNS/Internet.
- Possède un serveur DHCP simulé, un pool fictif, une table de baux, des durées de bail, des expirations et des erreurs simulées.
- Permet des scénarios de pool épuisé, de serveur indisponible, de conflit logique, de bail expiré ou de passerelle non joignable.
- Aucun paquet de ces scénarios ne doit être envoyé sur l'interface campus.
- Peut exécuter des centaines ou milliers de clients logiques selon le benchmark mémoire/CPU.

## 3.4 Mode de test encadré

Les cibles de tests actifs doivent être inscrites dans un fichier de configuration explicite, par exemple `config/test_scope.json`, avec :

- identifiant du périmètre ;
- SSID/BSSID ou profils concernés ;
- cibles de benchmark autorisées ;
- fenêtre temporelle ;
- intervalle minimal entre tests ;
- nombre maximal de tentatives ;
- taille maximale des transferts de test ;
- responsable et commentaire du périmètre.

Si aucun périmètre valide n'est configuré, le mode actif reste limité aux diagnostics locaux et aux profils que l'opérateur sélectionne manuellement. Le moteur doit afficher les limites de la configuration et journaliser chaque essai.

---

# 4. Architecture générale

Architecture en couches et contrats indépendants :

```text
+--------------------------------------------------------------+
|                    PRESENTATION LAYER                        |
|          SDL3 Window / Events / Dear ImGui / Charts           |
+------------------------------+-------------------------------+
                               |
+------------------------------v-------------------------------+
|                    APPLICATION LAYER                         |
| ApplicationState | Commands | ViewModels | User Preferences |
+------------------------------+-------------------------------+
                               |
+------------------------------v-------------------------------+
|                       CAMPUSLINK CORE                        |
| EventBus | TaskScheduler | ThreadPool | Result/Error | Config |
+---------------+--------------+----------------+--------------+
                |              |                |
      +---------v----+ +-------v--------+ +-----v------------+
      | HARDWARE     | | NETWORK        | | SIMULATION       |
      | INTELLIGENCE | | DIAGNOSTICS    | | VIRTUAL LAB      |
      +--------------+ +----------------+ +------------------+
                |              |                |
      +---------v--------------v----------------v-------------+
      |                 SERVICES / DOMAIN                      |
      | Benchmark | Ranking | Connection Selection | Handoff  |
      +------------------------------+-------------------------+
                                     |
      +------------------------------v-------------------------+
      |              PLATFORM ADAPTER INTERFACES               |
      | Windows Native Wi-Fi / IP Helper / WinHTTP             |
      | Future: Linux NetworkManager / nl80211                  |
      +------------------------------+-------------------------+
                                     |
      +------------------------------v-------------------------+
      |             WINDOWS NETWORKING STACK                   |
      +--------------------------------------------------------+
```

## 4.1 Règles de dépendance

- `ui` dépend de `application` et de modèles de lecture ; il ne dépend pas de `platform/windows`.
- `core` ne dépend ni de SDL3, ni d'ImGui, ni des API Windows.
- `network` dépend d'interfaces abstraites fournies par `platform`.
- `simulation` ne dépend pas de la pile réseau Windows et ne crée aucun trafic externe.
- `performance` reçoit des mesures, il ne gère pas directement les interfaces Wi-Fi.
- `intelligence` reçoit les candidats et leurs mesures ; il ne modifie pas directement l'adresse IP.
- Toute opération longue est asynchrone et annulable.

## 4.2 Structure de dépôt proposée

```text
CAMPUSLINK/
|-- build.bat
|-- VERSION
|-- README.md
|-- CHANGELOG.md
|-- THIRD_PARTY.md
|-- deps.lock
|-- LICENSES/
|-- assets/
|   |-- fonts/
|   |-- icons/
|   |-- themes/
|-- config/
|   |-- default.json
|   |-- test_scope.example.json
|-- include/campuslink/
|   |-- core/
|   |-- hardware/
|   |-- network/
|   |-- simulation/
|   |-- performance/
|   |-- intelligence/
|   |-- application/
|   |-- platform/
|   |-- ui/
|-- src/
|   |-- core/
|   |-- hardware/
|   |-- network/
|   |-- simulation/
|   |-- performance/
|   |-- intelligence/
|   |-- application/
|   |-- platform/windows/
|   |-- ui/
|   |-- main.cpp
|-- third_party/
|   |-- sdl3/                 (headers + import library/runtime package)
|   |-- imgui/                (source + selected backends)
|   |-- optional/
|-- tests/
|   |-- unit/
|   |-- contract/
|   |-- simulation/
|   |-- integration/
|   |-- hardware/
|-- scripts/
|   |-- package.ps1
|   |-- verify_release.ps1
|-- docs/
|   |-- architecture/
|   |-- decisions/
|   |-- reports/
|-- build/
|   |-- obj/
|   |-- bin/
|   |-- logs/
|-- dist/
|   |-- packages/
|   |-- releases/
```

La structure peut être ajustée après audit du dépôt existant, sans déplacer ou supprimer inutilement des fichiers déjà valides.

---

# 5. Hardware Intelligence Engine

Ce module est prioritaire et doit produire un rapport détaillé à chaque démarrage et à la demande.

## 5.1 Inventaire des adaptateurs

Pour chaque adaptateur, collecter lorsque disponible :

- nom convivial ;
- description ;
- GUID d'interface ;
- identifiant PnP / matériel ;
- fournisseur et modèle ;
- classe : physique, virtuel, tunnel, loopback, inconnu ;
- état : actif, désactivé, absent, déconnecté ;
- adresse MAC locale telle que rapportée par le système (ne pas l'altérer) ;
- pilote, fournisseur, version et date si accessibles ;
- adresses IPv4/IPv6 et routes pertinentes ;
- interface métrique et vitesse de lien rapportée.

Détecter les changements à chaud : ajout/retrait USB, désactivation, changement de pilote ou changement d'état.

## 5.2 Capacités Wi-Fi

Collecter les informations disponibles :

- types PHY pris en charge ;
- bandes/fréquences et canaux lorsque l'API les expose ;
- BSSID, SSID et RSSI pour les BSS visibles ;
- informations de sécurité rapportées ;
- état radio ;
- état de connexion courant ;
- qualité de connexion temps réel si supportée ;
- capacités de rôles secondaires lorsque l'API les expose.

API Windows à examiner et encapsuler :

- `WlanOpenHandle`, `WlanCloseHandle` ;
- `WlanEnumInterfaces` ;
- `WlanGetInterfaceCapability` ;
- `WlanGetAvailableNetworkList` ;
- `WlanGetNetworkBssList` ;
- `WlanQueryInterface` ;
- `WlanRegisterNotification` / `WlanUnregisterNotification` ;
- `WlanConnect` et `WlanDisconnect` dans le module de connexion ;
- `GetAdaptersAddresses` et autres API IP Helper utiles.

Pour la concurrence, interroger les propriétés/notifications disponibles, notamment les informations liées à `wlan_intf_opcode_secondary_sta_interfaces` et `wlan_intf_opcode_secondary_sta_synchronized_connections` lorsque supportées. Une erreur `NOT_SUPPORTED` est un résultat valide ; elle ne doit pas être transformée en capacité zéro ni en capacité supposée.

## 5.3 Modèle de confiance des données

Chaque capacité doit inclure :

```text
CapabilityValue<T> {
    value: optional<T>
    confidence: CONFIRMED | DRIVER_REPORTED | INFERRED | UNKNOWN
    source_api: string
    observed_at: timestamp
    notes: string
}
```

Le tableau de bord doit distinguer visuellement les valeurs mesurées, rapportées par le pilote et inconnues.

## 5.4 Estimation de la capacité de simulation

Le benchmark de simulation doit augmenter progressivement le nombre de clients logiques : 1, 8, 16, 32, 64, 128, etc. Les paliers exacts sont configurables.

Mesurer à chaque palier :

- mémoire moyenne et maximale par client ;
- CPU total et CPU par client ;
- temps de mise à jour d'un cycle ;
- latence interne de l'EventBus ;
- files d'événements en attente ;
- taux d'événements traités ;
- stabilité sur une durée configurable.

Arrêter automatiquement lorsque le seuil de CPU/RAM, de latence interne ou de temps de cycle est dépassé. Le résultat est une capacité logicielle estimée dans les conditions du benchmark, jamais une garantie absolue.

Le rapport doit présenter au minimum :

- `recommended_simulation_clients` ;
- `maximum_tested_clients` ;
- `cpu_peak_percent` ;
- `memory_peak_mb` ;
- `cycle_p95_ms` ;
- `benchmark_duration_seconds` ;
- configuration matérielle et version du logiciel.

---

# 6. Virtual Client Engine et simulateur réseau

## 6.1 Objet VirtualClient

Chaque client est une machine à états logique, pas un adaptateur radio réel.

```text
VirtualClient {
    ClientId id;
    VirtualNetworkProfile profile;
    ClientState state;
    AuthenticationScenario authentication;
    DhcpLeaseState lease;
    IpConfiguration simulated_ip;
    GatewayState gateway;
    DnsState dns;
    InternetState internet;
    MetricsSnapshot metrics;
    SimulationHistory history;
}
```

Les adresses MAC et IP simulées doivent rester dans l'espace de données du simulateur et être explicitement étiquetées `SIMULATED`. Elles ne doivent jamais être appliquées à l'interface physique.

## 6.2 États du client

```text
CREATED
  -> DISCOVERING
  -> ASSOCIATION_PENDING
  -> ASSOCIATED
  -> AUTHENTICATION_PENDING
  -> AUTHENTICATED
  -> DHCP_DISCOVER
  -> DHCP_OFFER
  -> DHCP_REQUEST
  -> DHCP_ACK
  -> CONFIGURED
  -> GATEWAY_CHECK
  -> DNS_CHECK
  -> INTERNET_CHECK
  -> ONLINE
```

États terminaux ou d'erreur :

- `NO_AP_FOUND`
- `ASSOCIATION_REJECTED`
- `AUTHENTICATION_FAILED`
- `DHCP_TIMEOUT`
- `DHCP_POOL_EXHAUSTED` (simulateur uniquement dans la version initiale)
- `INVALID_IP_CONFIGURATION`
- `GATEWAY_UNREACHABLE`
- `DNS_FAILURE`
- `INTERNET_UNREACHABLE`
- `CANCELLED`
- `ERROR`

Chaque transition possède un horodatage, une durée, une cause et un code d'erreur éventuel.

## 6.3 DHCP Simulator

Le simulateur doit reproduire les concepts et états des échanges DHCP sans envoyer de demandes de simulation sur le réseau réel :

- pool d'adresses fictif ;
- attribution de baux ;
- renouvellement ;
- expiration ;
- réservation simulée ;
- pool vide ;
- serveur indisponible ;
- réponse retardée ;
- bail expiré ;
- conflit de bail logique ;
- configuration incorrecte.

L'interface doit permettre de paramétrer la taille du pool, la durée de bail simulée, les délais et le nombre de clients. Les scénarios de saturation sont exécutés exclusivement dans le Virtual Lab.

## 6.4 Scheduler

Le scheduler doit gérer un grand nombre de clients logiques sans créer un thread par client. Utiliser un pool de workers borné, une file de tâches et une horloge simulée lorsque c'est pertinent.

Critères : déterminisme des scénarios avec seed fixée, arrêt propre, pause/reprise, annulation et journalisation des transitions.

---

# 7. Network Discovery et diagnostics réels

## 7.1 Découverte Wi-Fi

Pour chaque BSS visible, afficher les propriétés effectivement disponibles :

- SSID ;
- BSSID ;
- RSSI ;
- fréquence/bande et canal si déductibles de façon fiable ;
- type PHY ;
- mode de sécurité ;
- canal occupé/largeur si l'API le fournit ;
- horodatage du scan ;
- interface ayant effectué la découverte.

Un SSID identique peut correspondre à plusieurs BSSID. Les conserver comme candidats distincts dans la découverte, mais les regrouper visuellement lorsque cela améliore la lisibilité.

Gérer explicitement les autorisations Windows nécessaires à la découverte Wi-Fi/localisation selon la version de Windows. En cas de refus ou de donnée inaccessible, afficher l'instruction système correspondante ; ne pas contourner les permissions.

## 7.2 Diagnostic en couches

Le diagnostic doit différencier au minimum :

1. adaptateur absent/désactivé ;
2. radio désactivée ;
3. réseau non détecté ;
4. association non établie ;
5. authentification refusée/échouée ;
6. configuration IP absente ou invalide ;
7. DHCP en attente ou en échec ;
8. passerelle non joignable ;
9. résolution DNS en échec ;
10. Internet non joignable ;
11. portail captif ou validation Web potentielle ;
12. connexion fonctionnelle mais instable/lente.

Ne pas conclure « DHCP saturé » uniquement parce qu'aucune adresse IP valide n'est visible. Le diagnostic doit préciser `DHCP_FAILURE_SUSPECTED` tant que la cause exacte n'est pas prouvée.

## 7.3 Diagnostic d'adresse

Collecter la configuration IP via les API système : adresse, masque/préfixe, passerelle, DNS, état DHCP, bail si disponible. Utiliser les informations de Windows et les tests de connectivité adaptés.

Ne jamais appliquer automatiquement une adresse IP arbitraire découverte dans un autre contexte. Ne jamais transférer le bail d'un client virtuel ou d'un client de test vers l'hôte.

---

# 8. Connection Manager et Host Handoff

## 8.1 Principe du handoff

Le meilleur candidat transmet au gestionnaire hôte :

- interface physique à utiliser ;
- SSID et éventuellement BSSID cible ;
- référence du profil Windows autorisé ;
- mode de sécurité rapporté ;
- RSSI et mesures observées ;
- score et raison du classement ;
- horodatage et durée de validité de l'observation.

Le candidat **ne transmet pas son adresse IP** comme adresse à réutiliser. Windows connecte l'ordinateur réel avec son profil, puis la pile réseau obtient la configuration IP qui lui est attribuée.

## 8.2 Cas d'un seul adaptateur

Avec une seule interface Wi-Fi AX201, CAMPUSLINK doit effectuer les tests réels de manière séquentielle lorsque le pilote ne fournit pas plusieurs interfaces STA utilisables.

Le moteur ne doit pas afficher de faux parallélisme. Dans ce cas :

1. scanner les candidats ;
2. filtrer selon les profils autorisés ;
3. tester un candidat avec l'interface réelle ;
4. collecter les mesures ;
5. passer au suivant selon la politique de test ;
6. classer les candidats ;
7. demander au système de se connecter au gagnant ;
8. vérifier la nouvelle configuration et la connectivité ;
9. surveiller la connexion et déclencher un nouveau classement si nécessaire.

Le « client test » est alors un agent logiciel de mesure utilisant la pile réseau de l'hôte ; il ne s'agit pas d'un second ordinateur indépendant.

## 8.3 Cas de plusieurs adaptateurs physiques

Chaque adaptateur peut être testé indépendamment si le matériel, le pilote et les conditions radio le permettent. Chaque interface acquiert sa propre configuration réseau. Ne jamais partager un bail entre interfaces.

## 8.4 Machine à états du gestionnaire

```text
IDLE
 -> DISCOVERING
 -> CANDIDATE_FILTERING
 -> TEST_QUEUED
 -> CONNECTING
 -> WAITING_FOR_IP
 -> GATEWAY_VALIDATION
 -> DNS_VALIDATION
 -> INTERNET_VALIDATION
 -> BENCHMARKING
 -> RANKING
 -> SELECTED
 -> HOST_HANDOFF
 -> HOST_VALIDATION
 -> MONITORING
```

Sorties possibles : `NO_CANDIDATE`, `AUTH_REQUIRED`, `SCOPE_BLOCKED`, `CONNECTION_FAILED`, `VALIDATION_FAILED`, `CANCELLED`, `ROLLED_BACK`.

## 8.5 Rollback

Si la connexion sélectionnée échoue après le handoff :

- enregistrer le résultat et la cause ;
- restaurer le profil précédent si celui-ci est encore disponible et si la politique l'autorise ;
- ne pas basculer en boucle ;
- appliquer un délai de refroidissement ;
- présenter le choix à l'utilisateur après un nombre d'échecs configurable.

La connexion automatique doit rester contrôlable : arrêt, pause, sélection manuelle et mode observation doivent être accessibles depuis l'interface.

---

# 9. Performance Intelligence Engine

## 9.1 Métriques

- RSSI (dBm), présenté comme indicateur radio et non comme débit Internet ;
- latence médiane, p95 et p99 si assez d'échantillons ;
- jitter ;
- perte de paquets ;
- DNS resolution time ;
- débit descendant ;
- débit montant ;
- disponibilité de la passerelle ;
- stabilité dans le temps ;
- horodatage, cible et méthode de mesure.

## 9.2 Sources de mesure

- ICMP lorsque disponible ;
- mesures TCP/HTTPS vers une cible de test configurée ;
- résolution DNS via les API système ;
- transfert vers un serveur de benchmark explicitement autorisé, idéalement administré par le responsable réseau (par exemple un endpoint `iperf3` contrôlé) ;
- statistiques d'interface rapportées par Windows.

Un endpoint public ne doit pas être sollicité à répétition ni recevoir des transferts volumineux. Le volume, la durée et la fréquence des mesures sont limités et configurables.

## 9.3 Score initial

Normaliser toutes les composantes sur [0, 100], où 100 est toujours meilleur. Pondération initiale configurable :

- stabilité : 30 % ;
- débit descendant : 25 % ;
- latence : 20 % ;
- pertes : 15 % ;
- jitter : 10 %.

`score = 0.30*S + 0.25*D + 0.20*L + 0.15*P + 0.10*J`

Les fonctions de normalisation doivent être documentées, bornées et testées. Une valeur manquante n'est pas égale à zéro : appliquer une stratégie explicite de poids redistribué ou marquer le score comme incomplet. Présenter les composantes séparément afin que le score soit explicable.

## 9.4 Mesure de stabilité

Ne pas classer sur une seule mesure. Utiliser plusieurs fenêtres temporelles et calculer :

- dispersion de latence ;
- taux de succès des sondes ;
- variation du débit ;
- dérive du RSSI si disponible ;
- nombre de déconnexions observées.

Le système doit afficher la taille de l'échantillon et la confiance du score. Les candidats avec mesures insuffisantes doivent être marqués `LOW_CONFIDENCE`.

---

# 10. Interface utilisateur SDL3 + Dear ImGui

## 10.1 Bibliothèques

- SDL3 : fenêtre, événements, clavier/souris, cycle de vie.
- Dear ImGui : interface immédiate et widgets.
- Backend plateforme : `imgui_impl_sdl3.cpp`.
- Backend rendu prioritaire : `imgui_impl_vulkan.cpp`.
- Vulkan : rendu graphique.
- Graphiques : widgets ImGui personnalisés basés sur `ImDrawList` dans la première version. Une bibliothèque supplémentaire comme ImPlot ne sera intégrée que si son besoin, sa licence, sa version et sa compatibilité sont vérifiés.

Dépendances à versionner/pinner dans `deps.lock`. Ne pas télécharger automatiquement `master` pendant chaque build.

## 10.2 Direction visuelle

Créer une interface sombre, professionnelle, technique, lisible et non surchargée. Palette proposée : fond graphite, surfaces gris bleuté, accent cyan, vert pour succès, ambre pour avertissement, rouge pour erreur. Les couleurs doivent être accompagnées d'icônes/labels pour ne pas être le seul moyen de transmettre l'état.

Typographie claire, espacement cohérent, tableaux denses mais lisibles, graphiques avec axes/unités, animations discrètes et désactivables.

## 10.3 Navigation

1. **Overview** : état général, connexion courante, qualité et alertes.
2. **Hardware Lab** : inventaire, pilotes et capacités.
3. **Radio Capabilities** : bandes, PHY, RSSI, rôles et concurrence annoncée.
4. **Network Discovery** : SSID/BSSID et caractéristiques détectées.
5. **Virtual Client Lab** : création, contrôle et visualisation des clients simulés.
6. **DHCP Simulator** : pool, baux et scénarios fictifs.
7. **Diagnostics** : étapes et causes potentielles d'échec.
8. **Performance Lab** : historique et graphiques de mesures.
9. **Candidate Ranking** : scores explicables et niveau de confiance.
10. **Connection Manager** : sélection, connexion, rollback et suivi.
11. **Logs & Reports** : événements, export de rapport et journaux.
12. **Settings** : cadence, seuils, endpoints, profils et limites de test.

## 10.4 Responsivité

- aucun scan, test, appel Windows bloquant ou transfert de données sur le thread UI ;
- les workers publient des événements ou snapshots immuables ;
- mise à jour UI à fréquence bornée ;
- fermeture propre et annulation des tâches ;
- tous les boutons doivent avoir une action réelle, un état désactivé pertinent et un retour utilisateur.

---

# 11. Architecture C++ et conventions

## 11.1 Standard et compilation

- C++20 uniquement ;
- Windows x64 pour la première release ;
- `UNICODE` et `_UNICODE` ;
- exceptions maîtrisées aux frontières de modules ;
- warnings élevés ;
- aucun warning nouveau ignoré sans justification ;
- builds Debug et Release ;
- warnings élevés et ciblés selon le compilateur ;
- options de compatibilité MSVC et runtime explicites pour la cible `x86_64-pc-windows-msvc` ;
- options spécifiques au driver documentées dans le script plutôt que mélangées arbitrairement entre syntaxe GCC et MSVC.

## 11.2 Gestion des erreurs

Créer un type `Result<T, Error>` ou un résultat équivalent cohérent. Toute API Windows doit convertir les codes Win32/WLAN en erreurs typées et préserver le code natif, le module, l'opération et le contexte.

Utiliser RAII pour les handles WLAN, mémoire WLAN, handles système, ressources Vulkan et objets SDL. Toute allocation retournée par WLAN API doit être libérée avec la fonction Windows correspondante (`WlanFreeMemory`).

## 11.3 Threading

- un thread principal UI ;
- un ordonnanceur et pool de workers bornés ;
- aucun thread par client virtuel ;
- annulation via `std::stop_token` ou mécanisme équivalent ;
- files bornées et stratégie de backpressure ;
- aucune donnée mutable partagée sans synchronisation ;
- pas de callback vers l'UI depuis un worker.

## 11.4 Journalisation

Logs structurés : timestamp UTC/local explicite, niveau, module, événement, identifiant de tâche, interface concernée et code d'erreur. Ne pas écrire de mots de passe, secrets Wi-Fi, jetons ou données personnelles dans les logs.

Niveaux : `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`. Rotation et taille maximale configurables.

---

# 12. Compilation Windows : build.bat

Le script `build.bat` est un livrable majeur. Il doit être aussi soigné que l'application : présentation ASCII/UTF-8, couleurs, étapes numérotées, diagnostics clairs, résumé final et codes de sortie fiables.

## 12.1 Politique d'outillage

Pour le projet CAMPUSLINK, ne pas imposer CMake, Make, Ninja ou MSBuild.

Chaîne canonique proposée :

- LLVM Clang `clang++.exe` comme compilateur C++ prioritaire, ciblant l'ABI Windows MSVC (`x86_64-pc-windows-msvc`) ;
- Visual Studio Build Tools / MSVC toolchain et Windows SDK pour les headers, bibliothèques système et runtime ;
- le driver `clang++` pour l'édition de liens directe, avec `lld-link.exe` ou `link.exe` détecté dans l'environnement ;
- `clang-cl.exe` peut être utilisé comme mode de compatibilité de la même distribution LLVM si une dépendance ou un backend l'exige, mais ne doit pas imposer un changement de famille de compilateur ;
- SDL3 précompilé pour la même ABI/architecture ;
- Dear ImGui compilé directement depuis les sources vendues dans `third_party/imgui` ;
- Vulkan SDK installé localement, détecté via `VULKAN_SDK`.

Le script doit détecter LLVM et l'environnement Visual Studio via `vswhere`, puis initialiser `VsDevCmd.bat` si nécessaire afin que `clang++` trouve les headers, bibliothèques et outils de linkage Windows. Il ne doit jamais appeler `MSBuild.exe`. Le mode cible MSVC doit être explicite et cohérent pour tous les modules et dépendances. Si la chaîne requise est absente, afficher les éléments manquants et les étapes d'installation ; ne pas tenter de télécharger/exécuter des installateurs silencieusement.

Une seconde chaîne (par exemple clang++ MinGW-w64) peut être ajoutée ultérieurement, mais ne doit pas compliquer la première release ni mélanger les ABI MSVC et MinGW.

## 12.2 Commandes attendues

```bat
build.bat                 rem menu interactif ou build Release par défaut documenté
build.bat doctor          rem vérification des outils et dépendances
build.bat debug           rem compile Debug
build.bat release         rem compile Release
build.bat test            rem compile et exécute les tests non destructifs
build.bat package         rem construit le package portable
build.bat release-all     rem tests + build Release + package + hashes + rapport
build.bat clean           rem supprime uniquement les répertoires générés du projet
build.bat help            rem aide
```

`clean` ne doit jamais supprimer `third_party`, `assets`, `config`, les sources ou les rapports historiques.

## 12.3 Ergonomie du terminal

- définir la page de code UTF-8 (`chcp 65001`) avec repli propre si indisponible ;
- activer les séquences ANSI lorsque le terminal les accepte ;
- couleurs : cyan pour titres, bleu pour étapes, vert pour succès, jaune pour avertissement, rouge pour échec, gris pour détails ;
- utiliser des symboles Unicode simples si le terminal les affiche ; sinon basculer en ASCII (`[OK]`, `[WARN]`, `[FAIL]`) ;
- ne pas dépendre de glyphes exotiques ou de polices spécifiques ;
- afficher la durée de chaque étape et le code retour ;
- chemins toujours quotés et support des chemins avec espaces ;
- préserver le code d'erreur réel ;
- créer un log complet dans `build/logs/` ;
- résumer les outils détectés et les versions.

Exemple de présentation indicative :

```text
+==============================================================+
|                       CAMPUSLINK                              |
|                 BUILD & RELEASE CONSOLE                      |
|                   Windows x64 / C++20                         |
+==============================================================+

  [01/07] ENVIRONMENT CHECK       .........  OK
  [02/07] DEPENDENCY VALIDATION   .........  OK
  [03/07] SOURCE COMPILATION      .........  OK
  [04/07] LINKING                 .........  OK
  [05/07] UNIT & SIMULATION TESTS .........  OK
  [06/07] PACKAGE GENERATION      .........  OK
  [07/07] RELEASE VERIFICATION    .........  OK

  BUILD   : SUCCESS
  VERSION : 0.1.0
  TARGET  : Windows x64 / Release
  OUTPUT  : dist/packages/CampusLink-0.1.0-win-x64-portable.zip

+==============================================================+
```

Le contenu ci-dessus est un exemple de rendu, pas un résultat de build réel.

## 12.4 Phases internes du build

1. Vérifier architecture Windows x64 et répertoire racine.
2. Détecter/initialiser LLVM, MSVC Build Tools et Windows SDK.
3. Détecter SDL3, Vulkan SDK, sources ImGui et versions verrouillées.
4. Vérifier cohérence x64, ABI, configuration Debug/Release et fichiers requis.
5. Compiler les sources de CAMPUSLINK et les sources ImGui requises en objets séparés dans `build/obj/<config>/`.
6. Éditer les liens dans `build/bin/<config>/CampusLink.exe`.
7. Copier les DLL runtime nécessaires dans le dossier staging.
8. Exécuter tests unitaires et simulateur en mode offline.
9. Exécuter contrôles de packaging et licences.
10. Générer le package et son manifeste.
11. Calculer SHA-256 et vérifier l'archive produite.

Le script doit s'arrêter sur la première erreur critique et afficher le nom de l'étape ainsi que le code d'erreur. Les étapes optionnelles explicitement désactivées doivent apparaître comme `SKIPPED`, pas comme `OK`.

## 12.5 Sorties build

```text
build/
  obj/debug/...
  obj/release/...
  bin/debug/CampusLink.exe
  bin/release/CampusLink.exe
  logs/build-YYYYMMDD-HHMMSS.log

dist/
  staging/CampusLink-0.1.0-win-x64/
  packages/CampusLink-0.1.0-win-x64-portable.zip
  releases/v0.1.0/
      CampusLink-0.1.0-win-x64-portable.zip
      SHA256SUMS.txt
      manifest.json
      RELEASE_NOTES.md
```

## 12.6 Package portable

Le package doit contenir uniquement les éléments nécessaires à l'exécution et à l'information utilisateur :

- `CampusLink.exe` ;
- DLL SDL3 si build dynamique ;
- ressources utilisées ;
- configuration par défaut sans secret ;
- `README.txt` ou `README.md` ;
- `LICENSES/` et notices des dépendances ;
- exemple de périmètre de test ;
- fichier de version.

Ne jamais embarquer : clés, mots de passe Wi-Fi, profils personnels exportés, certificats privés, logs de l'environnement de développement, fichiers temporaires, objets `.obj` ou données de test utilisateur.

## 12.7 Release

La commande `release-all` doit :

- vérifier que le dépôt est dans un état cohérent ;
- lire la version depuis `VERSION` ;
- effectuer une compilation Release propre ;
- exécuter les tests obligatoires ;
- générer le package portable ;
- générer `manifest.json` avec version, commit, architecture, compilateur, versions de dépendances, date et liste de fichiers ;
- calculer SHA-256 ;
- vérifier l'archive ;
- générer les notes de release ;
- produire un rapport de release.

Une release ne doit pas être déclarée validée si les tests obligatoires échouent, si des fichiers requis manquent ou si le hash ne correspond pas.

---

# 13. Dépendances et gestion des versions

## 13.1 Dépendances principales

- SDL3 ;
- Dear ImGui ;
- Vulkan SDK/loader de développement ;
- Windows SDK ;
- LLVM Clang ;
- MSVC Build Tools (headers/libs système et linker selon installation).

## 13.2 Gestion des versions

- pinner une version ou un commit exact pour SDL3 et Dear ImGui ;
- enregistrer URL officielle, version, commit/tag et SHA-256 dans `deps.lock` ;
- conserver licences et notices ;
- ne pas utiliser une branche `master` flottante pour une release reproductible ;
- documenter la procédure de mise à jour des dépendances ;
- ne pas changer simultanément plusieurs dépendances majeures sans tests.

## 13.3 Bibliothèques système à prévoir

Les sources Windows peuvent nécessiter notamment `wlanapi.lib`, `iphlpapi.lib`, `ws2_32.lib`, `winhttp.lib`, `dnsapi.lib`, `ole32.lib` et `vulkan-1.lib`. Arena doit vérifier les besoins réels du code et ne linker que ce qui est nécessaire.

---

# 14. Tests et qualité

## 14.1 Tests unitaires

- transitions de la machine à états VirtualClient ;
- gestion des baux du DHCP Simulator ;
- calcul des métriques et normalisations ;
- score et classement ;
- gestion des valeurs inconnues/manquantes ;
- logique de retry/rollback ;
- parseurs et validations de configuration ;
- limites du scheduler et annulation.

## 14.2 Tests de simulation

Scénarios obligatoires :

1. client reçoit un bail valide ;
2. serveur simulé ne répond pas ;
3. pool simulé épuisé ;
4. bail expiré puis renouvelé ;
5. passerelle simulée indisponible ;
6. DNS simulé défaillant ;
7. débit variable et pertes simulées ;
8. clients annulés au milieu d'une transition ;
9. grand nombre de clients logiques ;
10. répétabilité avec seed fixe.

Tous ces tests doivent pouvoir tourner sans carte Wi-Fi et sans accès Internet.

## 14.3 Tests Windows

- inventaire de plusieurs interfaces si disponibles ;
- adaptateur désactivé/activé ;
- adaptateur absent ;
- scan Wi-Fi avec permissions disponibles ;
- refus de permission ;
- API non supportée par le pilote ;
- connexion à un profil valide de test ;
- absence d'adresse IP ;
- passerelle non joignable ;
- DNS non joignable ;
- déconnexion pendant benchmark ;
- changement d'interface pendant exécution.

## 14.4 Tests UI

- ouverture/fermeture répétée ;
- redimensionnement ;
- pertes/recréations de device Vulkan ;
- erreurs d'initialisation Vulkan ;
- annulation des tâches à la fermeture ;
- aucun blocage UI lors d'un scan ou benchmark ;
- boutons, menus et tableaux connectés à de véritables actions.

## 14.5 Tests de release

- compilation Debug ;
- compilation Release ;
- tests unitaires ;
- tests de simulation ;
- vérification de l'arborescence du package ;
- vérification des licences ;
- lancement du package depuis un chemin contenant des espaces ;
- vérification du manifeste et des hashes ;
- lancement sur un environnement Windows 11 propre si disponible.

## 14.6 Critères de performance initiaux

À mesurer, pas à prétendre :

- UI stable à 60 FPS sur la machine de référence avec dashboard normal ;
- scan et benchmark exécutés hors thread UI ;
- aucun thread par client virtuel ;
- croissance mémoire mesurée et plafonnée ;
- tests de 100, puis 500 et 1 000 clients logiques en Virtual Lab, selon capacité réelle ;
- temps de réaction UI et p95 du cycle de simulation consignés.

Les objectifs numériques définitifs seront fixés après une première mesure réelle sur la Precision 7750.

---

# 15. Sécurité, confidentialité et contrôle opérateur

- Pas de stockage de mots de passe Wi-Fi en clair dans la configuration CAMPUSLINK.
- Préférer les profils Windows existants et les mécanismes sécurisés du système.
- Ne pas exporter de clé WLAN dans les logs ou rapports.
- Les rapports doivent masquer les identifiants personnels non nécessaires.
- Les tests actifs doivent être clairement signalés dans l'interface.
- Ajouter un bouton Stop global qui annule les tâches en cours proprement.
- Ajouter des limites par défaut conservatrices : fréquence, durée, volume et nombre de tentatives.
- Les paramètres de laboratoire réel doivent être séparés des paramètres de simulation.
- Aucune modification d'adresse IP, de MAC ou de route sans action explicite et explication de l'effet.
- Aucun contournement de portail captif, de contrôle d'accès ou d'authentification.
- Les scénarios DHCP de saturation restent simulés dans le Virtual Lab ou utilisés dans un laboratoire isolé et administré, pas dans le mode campus standard.

---

# 16. Plan d'implémentation par phases

## Phase 0 — Audit et fondations

Livrables : rapport d'audit, arborescence validée, décisions d'architecture, `VERSION`, conventions, `deps.lock` initial et stratégie de build.

Acceptation : état initial documenté ; dépendances requises identifiées ; aucune fonctionnalité existante supprimée.

## Phase 1 — Build system et application minimale

Livrables : `build.bat` avec menu/help/doctor/debug/release/clean, compilation directe, application SDL3 minimale, logs et contrôles des dépendances.

Acceptation : `build.bat debug` et `build.bat release` produisent réellement l'exécutable ; les erreurs sont claires ; aucun CMake/Make/Ninja/MSBuild n'est invoqué.

## Phase 2 — SDL3 + Dear ImGui + Vulkan

Livrables : fenêtre, boucle d'événements, backend SDL3, backend Vulkan, thème, navigation et panneau de diagnostic de démarrage.

Acceptation : démarrage/fermeture/redimensionnement stables ; erreur Vulkan affichée proprement si indisponible ; aucun appel réseau dans le thread UI.

## Phase 3 — Hardware Intelligence Engine

Livrables : inventaire Windows, profil matériel, capacités PHY, état radio, RSSI et statut de confiance des capacités.

Acceptation : l'AX201 est identifié si présent ; modèle/driver/versions sont collectés ; les données inconnues restent inconnues ; rapport exportable.

## Phase 4 — Capacity Profiler

Livrables : benchmark CPU/RAM/scheduler du Virtual Client Engine ; rapport du plafond logiciel testé ; rapport distinct des rôles radio annoncés.

Acceptation : test reproductible, limites configurables, arrêt sûr sur seuil dépassé ; aucun chiffre radio inféré depuis 2x2.

## Phase 5 — Virtual Client Engine

Livrables : clients logiques, scheduler, états, événements et historique.

Acceptation : scénarios déterministes, annulation, pause/reprise, charge progressive et tests sans réseau.

## Phase 6 — DHCP Simulator et diagnostics

Livrables : pool, baux, renouvellement, expiration, erreurs et scénarios ; modèle de diagnostic par couches.

Acceptation : toutes les situations de la section 14.2 passent ; les scénarios restent offline.

## Phase 7 — Windows Network Discovery

Livrables : scan, liste BSS, SSID/BSSID/RSSI, état interface et permissions.

Acceptation : résultats comparés aux outils Windows ; erreurs de permission/driver exposées correctement ; aucune donnée inventée.

## Phase 8 — Physical Connection Manager

Livrables : profils Windows, connexion contrôlée, état DHCP, passerelle, DNS, validation et rollback.

Acceptation : test sur réseau de laboratoire ou profil autorisé ; jamais d'adresse IP empruntée ; connexion hôte validée avec sa propre configuration.

## Phase 9 — Performance Engine et Ranking

Livrables : latence, jitter, pertes, DNS, débit, stabilité, score explicable et historique.

Acceptation : tests unitaires des normalisations ; cible autorisée ; taille de transfert bornée ; classement reproductible sur données identiques.

## Phase 10 — Host Handoff et automatisation

Livrables : sélection du candidat, connexion Windows, validation, surveillance et rollback.

Acceptation : bascule testée sur matériel disponible ; si un seul adaptateur existe, flux séquentiel clairement affiché ; état réel confirmé après handoff.

## Phase 11 — Packaging et release 0.1.0

Livrables : package portable, manifeste, SHA-256, notes, logs et rapport de tests.

Acceptation : `build.bat release-all` exécute les validations, génère une archive vérifiée et ne marque aucune étape ignorée comme réussie.

## Phase 12 — Préparation Linux

Ne commencer qu'après stabilisation Windows. Définir des interfaces de plateforme, implémenter l'adaptateur Linux (NetworkManager/D-Bus ou interfaces adaptées), puis tester sur une distribution réelle. Ne pas simuler un support Linux complet tant qu'il n'est pas testé.

---

# 17. Critères globaux de livraison 0.1.0

La version 0.1.0 est livrable uniquement si :

- [ ] Windows 11 x64 est détecté comme plateforme supportée.
- [ ] Le build Debug et Release fonctionne avec `build.bat`.
- [ ] SDL3, Dear ImGui et Vulkan démarrent proprement ou une erreur explicite est affichée.
- [ ] L'inventaire matériel identifie correctement l'adaptateur réel.
- [ ] Les capacités sont accompagnées de leur niveau de confiance.
- [ ] Le plafond logiciel de simulation est calculé par benchmark.
- [ ] Les connexions radio réelles et les clients virtuels sont distingués dans tous les écrans et rapports.
- [ ] Le Virtual Lab simule les scénarios DHCP sans trafic campus.
- [ ] La découverte Wi-Fi et les diagnostics gèrent les erreurs/permissions.
- [ ] Les mesures de performance présentent méthode, cible et taille d'échantillon.
- [ ] Le classement explique son score.
- [ ] Le handoff utilise le profil de connexion Windows et la pile réseau normale.
- [ ] La configuration IP de l'hôte est obtenue/validée indépendamment du client de test.
- [ ] Stop global, annulation et rollback fonctionnent.
- [ ] Les tests unitaires et simulateurs passent.
- [ ] Le package portable démarre sur l'environnement de validation.
- [ ] Le manifeste et les sommes SHA-256 sont générés et vérifiés.
- [ ] Le rapport final distingue `PASS`, `FAIL`, `NOT_RUN` et `NOT_TESTED_ON_HARDWARE`.

---

# 18. Sources techniques de référence

Les versions exactes doivent être épinglées au moment de l'implémentation. Les références suivantes cadrent les capacités annoncées et les API à vérifier :

1. Dell, Precision 7750 — Communications / module WLAN : https://www.dell.com/support/manuals/fr-fr/precision-17-7750-laptop/precision_7750_specs/communications
2. Intel, AX201 — caractéristiques produit : https://www.intel.com/content/www/us/en/products/sku/130293/intel-wifi-6-ax201-gig/specifications.html
3. Microsoft, `WlanGetInterfaceCapability` : https://learn.microsoft.com/windows/win32/api/wlanapi/nf-wlanapi-wlangetinterfacecapability
4. Microsoft, `WlanQueryInterface` : https://learn.microsoft.com/windows/win32/api/wlanapi/nf-wlanapi-wlanqueryinterface
5. Microsoft, `WlanGetAvailableNetworkList` : https://learn.microsoft.com/windows/desktop/api/wlanapi/nf-wlanapi-wlangetavailablenetworklist
6. Microsoft, `WlanGetNetworkBssList` : https://learn.microsoft.com/windows/win32/api/wlanapi/nf-wlanapi-wlangetnetworkbsslist
7. Microsoft, `GetAdaptersAddresses` : https://learn.microsoft.com/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses
8. Dear ImGui, backends : https://github.com/ocornut/imgui/blob/master/docs/BACKENDS.md
9. Dear ImGui, SDL3 backend : https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_sdl3.cpp
10. SDL3, documentation Windows : https://wiki.libsdl.org/SDL3/README-windows

---

# 19. Prompt d'exécution final pour Arena AI

À partir de ce cahier des charges :

1. Audite d'abord le dépôt et l'environnement réellement disponibles.
2. Présente un diagnostic factuel des blocages, dépendances et risques techniques.
3. Propose un plan d'implémentation adapté à l'état réel du dépôt, sans réécrire inutilement les composants valides.
4. Commence par la Phase 0 et la Phase 1. Ne développe pas tous les modules dans un seul changement massif.
5. Implémente, compile et teste chaque phase avant de la déclarer achevée.
6. Crée un `build.bat` remarquable, robuste, lisible, coloré, UTF-8 avec repli ASCII, qui détaille réellement chaque étape et gère correctement les erreurs.
7. Ne produis pas de faux résultats de benchmark. Les données de démonstration doivent être explicitement marquées comme simulées.
8. Vérifie le fonctionnement réel sur Windows 11 ; si le matériel n'est pas accessible dans l'environnement de développement, marque les tests matériels `NOT_TESTED_ON_HARDWARE` et fournis la procédure de validation locale.
9. À chaque jalon, ajoute/actualise la documentation et les tests.
10. N'annonce une release que lorsque tous les critères obligatoires de cette spécification ont été vérifiés. Fournis le package, le manifeste, les hashes, les logs et le rapport final.

**Priorité absolue : une version plus petite mais réellement fonctionnelle, testée et reproductible vaut mieux qu'une interface riche qui présente des fonctions fictives.**
