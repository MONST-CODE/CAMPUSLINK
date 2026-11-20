# ADR 0004 — « Connexion d'abord », qualité du lien unique, et agrégation différée

**Statut :** proposé (conception, non implémenté). Cible : 0.5.x. Fait suite à [ADR 0002](0002-connectivity-evidence.md) et [ADR 0003](0003-connection-reliability-and-workspace.md).

## Contexte et priorité

Demande produit : sur les réseaux publics et universitaires, **réussir la connexion est la priorité**. Le débit vient ensuite. Le lien obtenu doit être **bon, stable et fluide**, mais **sans dépendre d'un second chemin** (un téléphone partagé par câble USB reste un cas occasionnel, pas une dépendance). L'agrégation de plusieurs chemins et l'hébergement d'une passerelle sont **documentés ici mais reportés**.

Le produit de référence a **une seule radio** (Intel AX201). Cette contrainte décide de tout :

- On ne peut pas additionner des débits sur une seule radio ni au même point d'accès (temps d'antenne partagé).
- Un « client virtuel » logiciel n'a pas de radio : il ne peut pas s'associer réellement. Il ne peut que **décider**.
- Une association et un bail IP ne se transfèrent pas : la vraie carte fait sa propre association et son propre DHCP.

Donc « bon, stable, fluide » **sur une radio** ne signifie pas sommer des chemins : cela signifie **obtenir le meilleur lien possible et y rester proprement**. C'est là que se place l'intelligence.

## Idée directrice réinterprétée

La vision initiale — « plusieurs clients explorent, le meilleur gagne, puis on connecte la vraie carte » — est conservée, mais déplacée là où elle fonctionne : les « clients virtuels » deviennent des **stratèges logiciels** qui choisissent pour la carte réelle. Aucun ne s'associe ; **seule la carte physique se connecte**, au candidat gagnant.

## Décisions

### A. Couche décision — stratèges et mémoire par environnement

1. **Espace de candidats.** Pour le réseau choisi (SSID), énumérer chaque point d'accès du scan : (BSSID, bande, canal, adaptateur), compatibilité du profil enregistré, sécurité. Sur un réseau public, un même nom porte souvent plusieurs points d'accès — c'est la matière première de la réussite.
2. **Modèle de réussite par candidat.** Un score explicable, borné, combinant :
   - signal (RSSI) et préférence de bande (déjà présent : `recommend_start`) ;
   - fraîcheur de l'historique : ce BSSID a-t-il associé / échoué récemment (déjà présent : `history_tier`) ;
   - compatibilité profil/sécurité ;
   - charge du canal observée (nombre de BSS sur le canal) comme indice de congestion ;
   - attente de portail/auth tirée de la mémoire (ce SSID a demandé une identification la dernière fois).
   Chaque composante est une **observation**, jamais une estimation déguisée en fait.
3. **Stratèges (temporaires / permanents).** Chaque stratège est une politique qui propose un **plan ordonné de tentatives** :
   - *le plus fort d'abord* (signal) ;
   - *bande la moins chargée* (5/6 GHz, moins de BSS sur le canal) ;
   - *connu-bon d'abord* (rejoue ce qui a marché ici — voir mémoire) ;
   - *balayage des points d'accès* (si un AP sature, essayer systématiquement un autre AP du même SSID).
   Ils « concourent » par réussite prédite, à coût nul ; le meilleur plan est remis au moteur réel. C'est la réalisation honnête de « plusieurs clients, le meilleur gagne ». *Temporaire* = exploration d'une session ; *permanent* = politique retenue par environnement.
4. **Mémoire par environnement (le « permanent »).** Persister, **par identité de réseau** (SSID + sécurité ; on ne connaît pas la localisation), ce qui a réussi : BSSID/bande retenus, portail nécessaire ou non, temps typiques d'association et de DHCP. Au retour sur le même réseau (un campus utilisé chaque jour), **partir de ce qui a marché**. C'est le plus grand levier pour la réussite répétée. Stockage **local**, sans secret, effaçable par l'utilisateur. Aujourd'hui l'historique est seulement de session (effacé à la fermeture) : cette persistance est **nouvelle**.

### B. Exécution robuste sur réseau public instable (socle 0.4.2, prolongé)

- Essais **bornés et classés** (déjà en 0.4.2) : pas d'autre BSSID après un échec d'authentification ou de profil ; essai d'un autre AP après un échec radio/association.
- **Portail captif** comme résultat de première classe (déjà) : « connecté, identification requise » est une réussite en attente, pas un échec ; on guide la connexion dans Windows et on **retient** que ce SSID a un portail.
- **Association conservée en DHCP lent** (déjà) : sur réseau public chargé, ne pas couper.
- **Balayage multi-AP borné (nouveau)** : si le meilleur AP n'associe pas ou ne donne pas d'adresse, parcourir **systématiquement** les AP suivants du même SSID avant d'abandonner — réaliste sur un campus où un AP est saturé. Borné, mesuré, interruptible.

### C. « Bon, stable, fluide » sans agrégation

Puisqu'on ne dépend pas d'un second chemin, la qualité = **obtenir et garder le meilleur lien unique** :

1. **Meilleur AP au moment de se connecter** (bande + signal + congestion) : couche décision ci-dessus.
2. **Rester sur un bon AP, éviter les changements inutiles** (hystérésis, déjà `should_switch`). La fluidité, c'est **ne pas osciller**.
3. **Surveillance légère et optionnelle de la qualité du lien actif (nouveau)** : suivre RSSI, joignabilité de la passerelle et réactivité DNS **du lien courant**. Si le lien se dégrade nettement **et** qu'un AP clairement meilleur du même réseau enregistré est disponible, **proposer** (jamais imposer) un changement. C'est de l'assistance au roaming, pas de l'agrégation.
4. **Étiquetage honnête** : le signal et les débits PHY ne sont pas du débit mesuré. Toute « qualité » affichée est soit mesurée (latence/gigue/pertes vers un endpoint autorisé), soit un rapport Windows clairement identifié.

Ce qui **n'est pas** promis : augmenter le débit brut d'un lien Wi-Fi unique — impossible. Ce qui est promis : mettre l'utilisateur sur le **meilleur** lien et l'y maintenir — ce qui, sur un campus encombré, fait souvent la différence entre inutilisable et fluide.

### D. Agrégation et hébergement — documentés, reportés

Reporté par décision explicite ; consigné pour ne pas fermer la porte.

- L'agrégation exige **au moins deux chemins réellement indépendants** (uplinks distincts) — pas la situation par défaut du poste de référence.
- Chemins possibles quand ils existent : téléphone en partage par câble USB (cellulaire), seconde carte Wi-Fi USB, Ethernet, Dual-STA si le pilote l'expose.
- Deux modes, jamais un seul :
  1. **Niveau application, sans privilège** : routage par interface + proxy local répartissant ce qui est répartissable (gros transfert découpé, connexions multiples). Pas de pilote. Gain sur des transferts précis, pas sur « tout Windows ».
  2. **Passerelle auto-hébergée, opt-in, privilégiée** : l'hôte émet sur plusieurs chemins vers une passerelle qui réassemble et sort vers Internet (seule voie pour agréger *tout* le trafic de façon transparente). Exige un serveur (coût, bande passante d'egress) et un adaptateur virtuel (TUN) à installer **à part**. Hors produit de base.
- **Hébergement** : composant futur, opt-in, **auto-hébergeable** ; aucune dépendance obligatoire à un cloud tiers ; l'opérateur de la passerelle paie son egress et applique sa politique. « À voir plus tard » — piste non bloquante.
- Toute agrégation est **toujours mesurée** avant/après, jamais présentée comme réelle sans gain prouvé, et ne doit pas pénaliser les autres usagers d'un réseau public.

## Machine à états (couche décision au-dessus du moteur 0.4.2)

```text
réseau choisi
   │
   ▼
construire l'espace de candidats (scan + profils + mémoire par environnement)
   │
   ▼
stratèges proposent des plans ─▶ fusion ─▶ plan ordonné unique (borné)
   │
   ▼
moteur 0.4.2 : pour chaque candidat du plan
      associer ─▶ adresse ─▶ Internet/portail        (inchangé, déjà livré)
      échec auth/profil ─▶ stop + explication
      échec radio/assoc ─▶ candidat suivant du plan (balayage multi-AP)
   │
   ├─ réussi ─▶ enregistrer le succès dans la mémoire par environnement
   │              │
   │              ▼
   │        surveillance optionnelle du lien ─▶ dégradation + meilleur AP dispo ─▶ PROPOSER un changement
   │
   └─ tous échoués ─▶ expliquer la cause dominante + actions (paramètres Windows, portail)
```

Toute transition est décidée à partir d'observations du backend ; l'interface ne reçoit que des états, elle n'en invente aucun (principe 0.4.2 conservé).

## Limites assumées

- Un seul radio ⇒ pas d'association parallèle réelle ni de gain de débit par multiplication de clients.
- La mémoire par environnement n'est pas une géolocalisation : deux réseaux de même nom et même sécurité sont confondus ; on l'assume et on l'affiche.
- La charge de canal est un indice, pas une mesure de débit disponible.
- La surveillance du lien est un échantillonnage léger ; elle peut manquer des creux transitoires.

## Frontières (inchangées, non négociables)

- Jamais de contournement d'authentification, de portail captif ou de politique d'un réseau non administré par l'utilisateur. Maximiser la réussite **dans l'autorisé** uniquement.
- Jamais présenter un client simulé comme une radio, ni additionner des débits théoriques, ni annoncer un gain non mesuré.

## Étapes

- **Piste 1 (0.5, livrable sur un seul PC)** : couche décision + mémoire par environnement + balayage multi-AP + surveillance optionnelle du lien avec changement **proposé**. Tout « connexion qui réussit » + « garder le meilleur lien ». Aucun matériel supplémentaire.
- **Piste 2 (expérimentale, reportée)** : agrégation sur deux chemins réels, à commencer par le cas *Wi-Fi + téléphone USB*, mode application sans privilège, mesuré.
- **Piste 3 (future, documentée)** : passerelle auto-hébergée pour l'agrégation de tout le trafic.

## Conséquences

- La priorité « réussir d'abord » devient un moteur de décision explicite et testable, au-dessus du moteur de connexion déjà fiabilisé en 0.4.2.
- La qualité du lien est traitée honnêtement (meilleur AP + maintien + proposition de roaming), sans promesse de débit qu'une radio unique ne peut tenir.
- L'agrégation reste possible plus tard sans dette : l'architecture la prévoit, rien ne la simule entre-temps.

Références : [Native Wi-Fi API permissions](https://learn.microsoft.com/en-us/windows/win32/nativewifi/native-wifi-api-permissions), [WLAN_BSS_ENTRY](https://learn.microsoft.com/en-us/windows/win32/api/wlanapi/ns-wlanapi-wlan_bss_entry), [Dual-STA documented ABI](https://learn.microsoft.com/en-us/windows/win32/nativewifi/dual-sta-query-config), [RFC 8684 (MPTCP)](https://www.rfc-editor.org/rfc/rfc8684), [RFC 9110 §14 (HTTP Range)](https://www.rfc-editor.org/rfc/rfc9110.html#section-14), [Wintun](https://www.wintun.net/).
