# User guide / Guide utilisateur

Extract the entire portable ZIP. Start `CampusLink.exe` next to `SDL3.dll`; Vulkan is tried first, software rendering is used if it fails. The software renderer is not a Wi-Fi simulation.

## Se connecter à un réseau Wi-Fi

L’application s’ouvre sur **Connexion Wi-Fi**.

1. **Connexion actuelle** (à droite) : nom du réseau, état Internet signalé par Windows (*Internet*, *Identification requise*, *Internet non confirmé*), signal, bande et canal, adresse IPv4, passerelle et carte. **Actualiser** relit l’état ; l’âge de l’information est affiché.
2. **Réseaux à proximité** : cliquez sur **Rechercher les réseaux**. Chaque réseau apparaît une seule fois, avec sa sécurité, ses bandes et son nombre de points d’accès. Le badge indique **Connecté**, **Enregistré** (profil Windows existant) ou **Non enregistré**. Si Windows ne confirme pas une nouvelle recherche à temps, CAMPUSLINK affiche les réseaux entendus au cours des deux dernières minutes et le signale.
3. Sélectionnez un réseau, puis choisissez :
   - **Connexion normale** — Windows utilise le profil enregistré et choisit le point d’accès.
   - **Connexion assistée directe** — CAMPUSLINK commence par le point d’accès de départ (présélectionné et marqué **Recommandé** : signal le plus fort, ou 5/6 GHz si son signal est suffisant ; vous pouvez en choisir un autre). Il n’en essaie d’autres **que si l’association échoue**, au plus trois au total. Aucun essai normal préalable n’est nécessaire.
4. Si la carte est déjà connectée à un autre réseau, une confirmation explique qu’elle sera déconnectée et que la connexion précédente sera rétablie en cas d’échec.
5. Pendant l’opération, les étapes **Préparation → Association → Adresse IP → Internet** reflètent ce que Windows a réellement confirmé. **Annuler** arrête l’opération.
6. Le résultat explique la situation et propose l’action utile :
   - **Connecté** : Windows signale un accès Internet.
   - **Identification requise** : un portail captif demande une connexion web. **Ouvrir la page d’identification** ouvre la page de test de Windows, qui redirige vers le portail. CAMPUSLINK ne contourne jamais cette étape.
   - **Connecté, Internet non confirmé** : association et passerelle fonctionnent, Windows n’a pas confirmé Internet (filtrage, saturation, portail non détecté).
   - **Associé, adresse en attente** : le réseau tarde à fournir une adresse IP ; l’association est **conservée** pour laisser le DHCP aboutir. **Vérifier à nouveau** relit l’état.
   - **Échec** : la cause est indiquée (authentification, profil, radio/association, adresse IP, demande refusée) avec le message de Windows lorsqu’il existe. Après un échec d’authentification ou de profil, changer de point d’accès ne servirait à rien : corrigez le mot de passe ou le profil dans les paramètres Wi-Fi de Windows.
   - **Détails** : durée, tentatives, raison du choix de chaque point d’accès, minutages d’association, d’adresse et d’Internet, code Windows.

Si le réseau n’a pas de profil, **Ouvrir les paramètres Wi-Fi de Windows**, connectez-vous une première fois, puis relancez la recherche. Les identifiants, l’adresse IP et les routes restent gérés par Windows.

### Mémoire par réseau (0.5)

CAMPUSLINK retient **localement** ce qui a fonctionné sur chaque réseau : le point d’accès qui a réussi, si une identification a été demandée, les délais typiques. Au retour sur un réseau connu, le point d’accès de départ **Recommandé** est celui qui marchait, et le badge **Réseau connu** apparaît. Rien ne quitte votre ordinateur. **Oublier ce qui a été appris** efface la mémoire d’un réseau. Sur un réseau public encombré, cochez **Essayer tous les points d’accès de ce réseau** pour que l’assistance les parcoure tous (nombre borné) au lieu de quelques-uns.

### Surveillance du lien (0.5, optionnelle)

Cochez **Surveiller la qualité du lien** sous la connexion actuelle. Tant que c’est activé, si le lien devient faible **et** qu’un point d’accès nettement plus fort du même réseau est visible dans la dernière recherche, CAMPUSLINK **propose** d’y basculer — il ne le fait jamais tout seul. **Rester** écarte la proposition pour quelques minutes. Aucune sonde active n’est émise ; la bascule éventuelle peut couper brièvement la connexion.

## Connect to a Wi-Fi network

The app opens on **Wi-Fi connection**. **Current connection** shows the network, the Internet state reported by Windows, signal, band/channel, IPv4, gateway and adapter, with the age of the information. **Search for networks** lists each network once (security, bands, number of access points, *Connected* / *Saved* / *Not saved*). When Windows does not confirm a fresh scan in time, the networks it heard in the last two minutes are shown and labelled as such.

Select a network, then **Connect normally** (Windows picks the access point with the saved profile) or **Connect with assistance** (starts with the *Recommended* access point — strongest signal, or 5/6 GHz when its signal is sufficient — and tries others of the same network only when the association fails, at most three in total). Switching away from another network asks for confirmation and restores the previous connection on failure. Progress steps (**Preparation → Association → IP address → Internet**) follow what Windows actually confirmed; **Cancel** stops.

Results explain what happened and offer the next step: *Connected*; *Sign-in required* (captive portal — **Open the sign-in page**; never bypassed); *Connected, Internet not confirmed*; *Associated, address pending* (the association is kept while DHCP completes; **Check again**); or *Connection failed* with its cause (authentication, saved profile, radio/association, IP address, request refused) and Windows' own message. **Details** lists attempts, timings and Windows codes.

Neither mode requires `test_scope.conf`, sends CLP1 traffic or creates simulated clients. The Internet state is the Windows Network List Manager report for the connected adapter, read until Windows concludes (up to 12 s); it is not an active test by CAMPUSLINK. Signal and link rates are not throughput. IPv6-only links are not validated.

## Research lab / Laboratoire

The lab pages are grouped under **Research lab** and never run on their own.

- **Hardware Lab / Radio Capabilities**: inventory and driver-reported capabilities, with unknowns stated. Dual-STA information is read-only.
- **Network Discovery**: the raw per-BSSID table. Selecting a row opens **Authorized trials (CLP1)**.
- **Virtual Client Lab**: logical clients and DHCP scenarios, offline. Recording saves the scenario and a `SIMULATED` CSV under the folder shown in **Settings**. Replay uses the saved seed; it does not replay packets.
- **Experiment Center / Authorized trials (CLP1)**: put `test_scope.conf` in the user data folder (start from `config/test_scope.example.conf`), load it, select the exact profile+BSSID, authorize and confirm. This can interrupt the host connection: do not run it over a session that uses the interface being switched. Up to four exact authorized matches can be queued. Rollback makes one bounded attempt to restore the previous usable link, even after cancellation. See [network diagnostics](NETWORK_DIAGNOSTICS.md).
- **Performance Lab / Intelligence Engine**: endpoint-specific results and a configurable weighted score with coverage. Not public Internet tests or throughput guarantees.
- **Paths and multipath lab**: measure a currently associated link against the authorized endpoint **without reconnecting**, and compare scheduler models offline. No relay, gateway or real aggregation is deployed.
- **Logs & Diagnostics**: environment, step-by-step diagnosis with status badges, recent events and TSV export (no keys or raw network identifiers).

## Affichage / Display

La première ouverture est en français. La barre supérieure change la langue (**FR/EN**) et le thème (clair/sombre). **Paramètres** règle la police, la taille et la densité ; les changements s’appliquent immédiatement et sont enregistrés dans `display-v1.conf`. En fenêtre étroite, un menu remplace la barre latérale et la page de connexion passe sur une colonne. Fenêtre réduite, l’application ne redessine plus.

The interface starts in French; switch language and theme from the top bar. **Settings** controls font, size and density, applied immediately and saved in `display-v1.conf`. Narrow windows use a page menu and a single-column connection page. The app does not redraw while minimized.
