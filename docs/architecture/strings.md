# Player-facing text

Every word tandem shows the player comes from one keyed table,
`scripts/tandem/Text.reds`, in English and French. Names (players, NPCs,
places) and what comes from another game are data, not text.

## Inside the plugin

- **The table** (`Text.reds`): one package per language, a
  `ModLocalizationPackage` of Codeware's (`Codeware.Localization`, 1.21.4 on
  the game machine). Keys are `Tandem-Area-Name`; the scripts name them
  without the prefix (`"Revive-Down"`). `TandemTextProvider` gives the
  package of the game's on-screen language (`/language OnScreen`: `en-us`,
  `fr-fr`), English for any other. Codeware merges the packages into the
  game's own on-screen texts as a language loads (its native side calls
  `ModLocalizationProvider.GetOnScreenEntries`), so a key also works where
  the game takes one (`GetLocalizedTextByKey`, `LocKey#` fields).
- **Reading a text**: `TandemText.Get(key)`, `F(key, a, b)` with `{1}` and
  `{2}` filled (names, addresses, numbers), `AnyOf(names)` for "A, B or
  C". A key the merged texts do not know (texts loaded before Codeware saw
  us) falls back to the English package, so a text is never empty.
- **Notices** (`TandemText.Notice`, `NoticeAs` with a type and a duration,
  `NoticeReason`): the HUD's warning line, the game's own short message
  (`UI_Notifications.WarningMessage`). Every notice is logged in English
  with its key, `coop notice: NAME joined the session
[Session-PlayerJoined]`: scenarios read the log whatever the game's
  language. The other logs stay English too.
- **Input hints** (`TandemText.Hint`): the game's hint bar
  (`UpdateInputHintEvent` to `GameplayInputHelper`) with an input action;
  the game draws the key or button the player bound to it, for the device
  in their hands. Never a key name in a text. In use: a ride's question
  (`PhoneInteract` press: ride along, `PhoneReject` hold: follow), a regroup
  offer (`PhoneInteract`), the world map's Join (`world_map_menu_fast_travel`
  hold, `WorldMapInputHints`), the host's scene offer and the scene replay's
  watch offer (`Choice1`, which also takes them). The revive prompt is the game's interaction hub
  (`InteractionChoiceHub`, `Choice1` held), which draws its own binding.
- **The plugin's words** (`core/src/endpoint.cpp`, `relay.cpp`,
  `plugin/src/net_thread.cpp`): a session's end, a refusal, a relay
  problem and the resync notice arrive in English. `TandemText.Reason`
  maps each one it knows to a key (`Reason-*`, `refused: …`, `protocol
version …`); an unknown one is shown as it came.
- **Over the wire**: a regroup offer's label is a key of ours when it is
  one (`Transit-RideOver`), shown by each game in its own language
  (`TandemText.OfWire`); a quest title from the host is shown as it came.
- **Vanilla texts reused** (`TandemText.Vanilla`, the game's keys):
  `UI-ScriptExports-Continue0`, `UI-ScriptExports-LoadGame0` (the co-op
  menu's save pages), `UI-UserActions-Yes`, `UI-UserActions-No` (the test
  choice hub), the NCART stations (`LocKey#95259`…`#95277`,
  `TandemTransit.StationName`), a fast travel point's `DisplayName`.
- **The lint** (`tools/check-strings.py`, `task strings:check`, run by
  `task scripts:check`, so by every deploy, and by `task check`): a literal
  with words in a player-facing place is refused (a notice, a widget's
  `SetText`, a popup, a hint, a choice, the progress bar, a regroup label,
  a variable named `text`, `label`, `header`, `bottom` or `title`); every
  key the scripts name exists; every English key has a French one with the
  same placeholders, and back. A line only for tests says
  `// text: test only` (the `story hub` and `story line` commands). It
  reads lines, not data flow: a text built in a variable with another
  name escapes it, so build texts with `TandemText.F`.

### ArchiveXL or a redscript table

Weighed 2026-11-19 (the user asked for a recommendation):

- **ArchiveXL**: an `.xl` file naming one JSON resource per language
  (`localization: onscreens: en-us: …`), packed into an `.archive` with
  WolvenKit (a Windows tool; no Linux build step we have), installed under
  `archive/pc/mod`. Its texts are native everywhere, TweakDB fields
  included, and translators get plain JSON.
- **Codeware's redscript table** (chosen): plain `.reds`, compiled and
  checked with the rest of the scripts (`task scripts:check`, the lint),
  no archive and no tool. Codeware is already required; it merges the
  texts into the native on-screen texts too, and offers gendered
  variants. What it lacks today: texts for TweakDB records (ours have
  none: a puppet's name fields are empty on purpose).

Recommended: the redscript table. Revisit if a TweakDB record needs a
text, or if translators want files: the table can be written out as
ArchiveXL's JSON by a script.

## Open problems

- **Texts from the host's game** (subtitles, dialogue choices, quest
  titles, a station or a shop's name a guest gets as text) come in the
  host's language. Showing them in a guest's own needs their keys on the
  wire instead (a line's or a choice's `LocKey`), message by message.
- **French for other players**: "(e)" where the agreement follows the
  other V; the puppet's body is known (`Body`), so the gendered variant
  could follow it (Codeware's `Text(key, female, male)` follows our own V).
- More languages: one package each, the lint keeps them complete.
- Not yet checked in game: that Codeware's merge reaches the main menu
  (else the English fallback shows there in every language), and the French
  texts' lengths in the menu and the hint bar.

## Inventory

Every text, with what shows it (`Text.reds` alone: built at run time
there, as the plugin's reasons). `{1}`, `{2}`: names, places, numbers.

### The co-op menu

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Menu-Coop` | Co-op | Coop | Coop.reds |
| `Menu-Host` | Host a game | Héberger une partie | Coop.reds |
| `Menu-Join` | Join a game | Rejoindre une partie | Coop.reds |
| `Menu-Solo` | Play solo | Jouer en solo | Coop.reds |
| `Menu-HostNow` | Host this game | Héberger cette partie | Coop.reds |
| `Menu-Invite` | Invite | Inviter | Coop.reds |
| `Menu-Leave` | Leave session | Quitter la session | Coop.reds |
| `Menu-EnterCode` | Enter a code or an address | Saisir un code ou une adresse | Coop.reds |
| `Menu-Back` | Back | Retour | Coop.reds |
| `Menu-Report` | Write a bug report | Écrire un rapport de bug | Coop.reds |
| `Popup-CoopOff` | Co-op is off | Coop désactivée | Coop.reds |
| `Popup-Report` | Bug report | Rapport de bug | Coop.reds |
| `Menu-Recent` | {1} ({2}) | {1} ({2}) | Coop.reds |
| `Menu-Settings` | Settings | Paramètres | Coop.reds |

### Its status panel

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Status-HostAtLoad` | You host when your game loads (port {1}) | Vous hébergez dès le chargement de votre partie (port {1}) | Coop.reds |
| `Status-HostFailed` | Could not host: {1} | Hébergement impossible : {1} | Coop.reds |
| `Status-Hosting` | Hosting on port {1} | Hébergement sur le port {1} | Coop.reds |
| `Status-Code` | Join code: {1} | Code d'invitation : {1} | Coop.reds |
| `Status-Relay` | Relay: {1} | Relais : {1} | Coop.reds |
| `Status-JoinAtLoad` | You join {1} when your game loads | Vous rejoignez {1} dès le chargement de votre partie | Coop.reds |
| `Status-InSessionOf` | In {1}'s session | Dans la session de {1} | Coop.reds |
| `Status-InSessionAt` | In the session at {1} | Dans la session {1} | Coop.reds |
| `Status-Retrying` | {1} not reached ({2}), trying again | {1} injoignable ({2}), nouvel essai | Coop.reds |
| `Status-Connecting` | Connecting to {1}... | Connexion à {1}... | Coop.reds |
| `Status-JoinFailed` | Could not join {1} | Impossible de rejoindre {1} | Coop.reds |
| `Party-Metres` | {1} m | {1} m | PartyHud.reds |
| `Party-Kilometres` | {1} km | {1} km | PartyHud.reds |
| `Session-NeedsExpansion` | Co-op is off: tandem needs Phantom Liberty on every player's game | Coop désactivée : tandem demande Phantom Liberty dans le jeu de chaque joueur | System.reds |
| `Status-None` | Not in a session | Aucune session | Coop.reds |
| `Status-Host` | {1} (host) | {1} (hôte) | Coop.reds |
| `Status-You` | {1} (you) | {1} (vous) | Coop.reds |
| `Status-Ping` | {1} - {2} ms | {1} - {2} ms | Coop.reds |
| `Status-Recorder` | Recorder: {1} | Enregistreur : {1} | Coop.reds |
| `Status-CodeTarget` | code {1} | code {1} | Coop.reds |
| `Status-ChooseHostSave` | Choose the save to host | Choisissez la sauvegarde à héberger | Coop.reds |
| `Status-ChooseJoinSave` | Choose the save your V comes from, to join {1} | Choisissez la sauvegarde de votre V pour rejoindre {1} | Coop.reds |
| `Status-PickHost` | Pick a host, or enter their join code or address | Choisissez un hôte, ou saisissez son code d'invitation ou son adresse | Coop.reds |

### Shared words

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Common-On` | on | activé | Settings.reds |
| `Common-Off` | off | désactivé | Settings.reds |

### Invites

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Invite-Code` | Join code {1}.  | Code d'invitation {1}.  | Coop.reds |
| `Invite-NoAddress` | Friends join you on port {1} (no network address found) | Vos amis vous rejoignent sur le port {1} (aucune adresse réseau trouvée) | Coop.reds |
| `Invite-Addresses` | Friends on your network join you at {1} | Vos amis sur votre réseau vous rejoignent à {1} | Coop.reds |

### Popups

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Popup-NeedsRelay` | Joining by code needs a relay: set one in the co-op settings | Rejoindre par code demande un relais : indiquez-le dans les paramètres coop | Coop.reds |
| `Popup-EnterCode` | The host's join code (K7Q-2M9) or address (192.168.1.20, or 192.168.1.20:27450), as their Co-op menu shows it | Le code d'invitation de l'hôte (K7Q-2M9) ou son adresse (192.168.1.20, ou 192.168.1.20:27450), comme l'affiche son menu Coop | Coop.reds |
| `Popup-NotATarget` | Not a join code or an address: {1} | Ni un code d'invitation ni une adresse : {1} | Coop.reds |

### The session

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Session-Hosting` | Hosting co-op. {1} | Partie coop hébergée. {1} | Coop.reds |
| `Session-JoinedOf` | Joined {1}'s session | Vous avez rejoint la session de {1} | Coop.reds |
| `Session-Joined` | Joined the co-op session | Vous avez rejoint la session coop | Coop.reds |
| `Session-YouLeft` | You left the co-op session | Vous avez quitté la session coop | System.reds |
| `Session-PlayerJoined` | {1} joined the session | {1} a rejoint la session | System.reds |
| `Session-PlayerLeft` | {1} left the session | {1} a quitté la session | System.reds |
| `Session-Ended` | Co-op session ended: {1} | Session coop terminée : {1} | System.reds, Text.reds |
| `Session-Resync` | Resyncing with the host... | Resynchronisation avec l'hôte... | Text.reds |

### The plugin's reasons (shown as `Reason-Refused`, `Session-Ended`, the status panel)

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Reason-Refused` | refused: {1} | refusé : {1} | Text.reds |
| `Reason-Full` | the session is full | la session est complète | Text.reds |
| `Reason-FullMax` | the session is full (4 players at most) | la session est complète (4 joueurs au plus) | Text.reds |
| `Reason-Version` | another version of tandem (protocol {1}) | une autre version de tandem (protocole {1}) | Text.reds |
| `Reason-HostGone` | the host is gone | l'hôte est parti | Text.reds |
| `Reason-Unreachable` | could not reach the host | hôte injoignable | Text.reds |
| `Reason-Connecting` | connecting | connexion | Text.reds |
| `Reason-RelaySilent` | the relay did not answer | le relais n'a pas répondu | Text.reds |
| `Reason-RelayToken` | the relay refused our host token | le relais a refusé notre jeton d'hôte | Text.reds |
| `Reason-RelayNoSession` | the host left | l'hôte est parti | Text.reds |
| `Reason-RelayBusy` | the relay has too many sessions open | le relais a trop de sessions ouvertes | Text.reds |
| `Reason-RelayTaken` | another host holds this session on the relay | un autre hôte tient cette session sur le relais | Text.reds |
| `Reason-RelayNoCode` | no session has this join code (wrong, or expired) | aucune session n'a ce code d'invitation (erroné ou expiré) | Text.reds |
| `Reason-RelayThrottled` | too many join attempts: try again in a minute | trop de tentatives : réessayez dans une minute | Text.reds |
| `Reason-RelaySocket` | could not open a socket to the relay | impossible d'ouvrir une connexion vers le relais | Text.reds |

### Players

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Player-Unnamed` | Player {1} | Joueur {1} | Text.reds |

### Lists

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `List-Or` |  or  |  ou  | Text.reds |

### Players elsewhere

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Elsewhere-Back` | {1} is back | {1} est de retour | Elsewhere.reds |
| `Elsewhere-Cyberspace` | {1} is in cyberspace | {1} est dans le cyberespace | Elsewhere.reds |
| `Elsewhere-Braindance` | {1} is in a braindance | {1} est dans une danse sensorielle | Elsewhere.reds |
| `Elsewhere-JohnnyMemory` | {1} is living one of Johnny's memories | {1} revit un souvenir de Johnny | Elsewhere.reds |
| `Elsewhere-Johnny` | {1} is Johnny Silverhand now | {1} est Johnny Silverhand pour l'instant | Elsewhere.reds |
| `Elsewhere-Somewhere` | {1} is somewhere else for now | {1} est ailleurs pour l'instant | Elsewhere.reds |
| `Elsewhere-Someone` | {1} is someone else for now | {1} est quelqu'un d'autre pour l'instant | Elsewhere.reds |

### Pause

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Pause-By` | {1} paused the game | {1} a mis le jeu en pause | Pause.reds |

### Calls

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Phone-Declined` | {1} declined the call | {1} a refusé l'appel | Phone.reds |
| `Phone-Answered` | {1} answered the call | {1} a répondu à l'appel | Phone.reds |

### Dialogue

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Dialogue-Suggests` | {1} suggests: {2} | {1} suggère : {2} | Dialogue.reds |
| `Dialogue-Suggested` | Suggested: {1} | Suggéré : {1} | Dialogue.reds |

### Story

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Story-NotRestored` | Co-op: your story could not be restored exactly. Load your last save | Coop : votre histoire n'a pas pu être restaurée à l'identique. Chargez votre dernière sauvegarde | Story.reds, Text.reds |
| `Story-SceneStarted` | {1} started a scene | {1} a lancé une scène | Story.reds, Text.reds |
| `Story-Busy` | {1} is busy | {1} est occupé(e) | Story.reds |
| `Story-Join` | Join {1} | Rejoindre {1} | Story.reds |

### Scene replay

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Scene-InScene` | {1} is in a scene | {1} est dans une scène | Scenes.reds, Text.reds |
| `Scene-Watch` | Watch the scene | Regarder la scène | Scenes.reds |

### Loot

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Loot-Taken` | Someone already took it | Quelqu'un l'a déjà pris | Loot.reds |
| `Loot-TakenBy` | {1} picked it up first | {1} l'a ramassé avant vous | Loot.reds |

### Markers

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Marker-Description` | Another V, in your city | Un autre V, dans votre ville | Markers.reds |
| `Marker-Distance` | {1}  {2} M | {1}  {2} M | Markers.reds |

### Down, revive, respawn

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Revive-Down` | You are down. {1} can revive you | Vous êtes à terre. {1} peut vous réanimer | Revive.reds, Text.reds |
| `Revive-DownAlone` | You are down | Vous êtes à terre | Revive.reds, Text.reds |
| `Revive-Out` | You are out. You will get up beside {1} | Vous êtes hors combat. Vous vous relèverez près de {1} | Revive.reds, Text.reds |
| `Revive-AllDown` | Everyone is down | Tout le monde est à terre | Revive.reds, Text.reds |
| `Revive-WaitHost` | Everyone is down. Waiting for {1} to load the last checkpoint | Tout le monde est à terre. En attente du dernier point de contrôle de {1} | Revive.reds, Text.reds |
| `Revive-RevivedBy` | {1} revived you | {1} vous a réanimé(e) | Revive.reds |
| `Revive-BackBeside` | Back beside {1} | De retour près de {1} | Revive.reds |
| `Revive-PlayerDown` | {1} is down | {1} est à terre | Revive.reds |
| `Revive-PlayerOut` | {1} is out | {1} est hors combat | Revive.reds |
| `Revive-PlayerUp` | {1} is back up | {1} s'est relevé(e) | Revive.reds |
| `Revive-Prompt` | Revive | Réanimer | Revive.reds |
| `Revive-Reviving` | Reviving {1} | Réanimation de {1} | Revive.reds |
| `Revive-KeepHolding` | Hold to revive | Maintenez pour réanimer | Revive.reds |
| `Revive-RevivingYou` | {1} is reviving you | {1} vous réanime | Revive.reds |
| `Revive-StayDown` | Stay down | Restez à terre | Revive.reds |
| `Revive-Done` | Revived | Réanimation réussie | Revive.reds |

### Rides and the NCART

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Transit-GotOffAt` | {1} got off the NCART at {2} | {1} est descendu(e) du NCART à {2} | Transit.reds |
| `Transit-GotOff` | {1} got off the NCART | {1} est descendu(e) du NCART | Transit.reds |
| `Transit-RidingToward` | {1} is riding the NCART toward {2} | {1} prend le NCART vers {2} | Transit.reds |
| `Transit-Riding` | {1} is riding the NCART | {1} prend le NCART | Transit.reds |
| `Transit-Skipping` | {1} is skipping the NCART ride | {1} passe le trajet en NCART | Transit.reds |
| `Transit-Leaving` | {1}'s ride is leaving: ride along, or follow | Le véhicule de {1} part : montez, ou suivez | Text.reds, Transit.reds |
| `Transit-RideWith` | Ride with {1} | Monter avec {1} | Transit.reds |
| `Transit-Follow` | Follow | Suivre | Transit.reds |
| `Transit-Following` | Following {1}: join them when the ride is over | Vous suivez {1} : rejoignez-le à la fin du trajet | Transit.reds |
| `Transit-YouRide` | You ride with {1} | Vous voyagez avec {1} | Transit.reds |
| `Transit-RidingAlong` | Riding along with {1} | En route avec {1} | Transit.reds |
| `Transit-RideOver` | the ride is over | le trajet est terminé | Transit.reds |

### Fast travel and regroup

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Travel-OnTheWay` | {1} is on the way | {1} arrive | Travel.reds |
| `Travel-Stays` | {1} stays where they are | {1} reste sur place | Travel.reds |
| `Travel-FastTravelledTo` | {1} fast travelled to {2} | {1} s'est déplacé(e) rapidement vers {2} | Text.reds, Travel.reds |
| `Travel-FastTravelled` | {1} fast travelled | {1} a utilisé le voyage rapide | Text.reds, Travel.reds |
| `Travel-Labelled` | {1}: {2} | {1} : {2} | Text.reds, Travel.reds |
| `Travel-Scene` | {1} is starting a scene | {1} lance une scène | Text.reds, Travel.reds |
| `Travel-Taken` | {1} was taken somewhere else | {1} a été emmené(e) ailleurs | Text.reds, Travel.reds |
| `Travel-Asks` | {1} asks you to join them | {1} vous demande de le rejoindre | Text.reds, Travel.reds |
| `Travel-Join` | Join {1} | Rejoindre {1} | Travel.reds |
| `Travel-Blocked` | You can't fast travel right now | Voyage rapide impossible pour l'instant | Text.reds, Travel.reds |

### The settings page

<!-- prettier-ignore -->
| Key | English | French | Shown by |
| --- | --- | --- | --- |
| `Settings-Saved` | Saved to tandem.ini as you change them | Enregistrés dans tandem.ini dès leur modification | Coop.reds |
| `Settings-Item` | {1}: {2} | {1} : {2} | Settings.reds |
| `Settings-NextSession` | {1} (from the next session) | {1} (à la prochaine session) | Settings.reds |
| `Settings-AfterRestart` | {1} (after a restart) | {1} (après un redémarrage) | Settings.reds |
| `Settings-HostsRule` | {1} (the host's rule) | {1} (règle de l'hôte) | Settings.reds |
| `Settings-Type` | Type the new value (empty: none) | Saisissez la nouvelle valeur (vide : aucune) | Coop.reds |
| `Settings-Invalid` | Not a value for this setting: {1} | Valeur refusée pour ce paramètre : {1} | Coop.reds |
| `Settings-NotSet` | not set | non défini | Settings.reds |
| `Settings-TokenSet` | set | défini | Settings.reds |
| `Settings-Name` | Name | Nom | Settings.reds |
| `Settings-FriendlyFire` | Friendly fire | Tirs alliés | Settings.reds |
| `Settings-Notices` | Notices | Notifications | Settings.reds |
| `Settings-Notices-All` | all | toutes | Settings.reds |
| `Settings-Notices-Important` | important only | importantes seulement | Settings.reds |
| `Settings-Notices-Minimal` | questions and problems | questions et problèmes | Settings.reds |
| `Settings-Markers` | Player markers | Marqueurs des joueurs | Settings.reds |
| `Settings-Nameplates` | Names over players | Noms au-dessus des joueurs | Settings.reds |
| `Settings-Recorder` | Recorder | Enregistreur | Settings.reds |
| `Settings-Relay` | Relay | Relais | Settings.reds |
| `Settings-RelayToken` | Relay host token | Jeton d'hôte du relais | Settings.reds |
| `Settings-Story` | Story sync | Histoire partagée | Settings.reds |
| `Settings-QuestFreeze` | Pause your own story (guest) | Mettre votre histoire en pause (invité) | Settings.reds |
| `Settings-ScenePrivacy` | Private scenes | Scènes privées | Settings.reds |
| `Settings-TimeHost` | Your Sandevistan (host) | Votre Sandevistan (hôte) | Settings.reds |
| `Settings-TimeHost-Full` | slows your whole game | ralentit tout votre jeu | Settings.reds |
| `Settings-TimeHost-Bubble` | slows the NPCs around you | ralentit les PNJ autour de vous | Settings.reds |
