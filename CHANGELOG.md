# EXPBG GM Tools changelog

## 0.1.4

Fixes from the first full client test on a dedicated server with the production
modset (ACE, RHS, CDF GameMaster Save, Game Master Enhanced and 110+ other mods).

- Unit Caching: casualty cleanup now simply deletes the body, with everything
  it carries, and its dropped weapon in the same tick. There are no per-item
  checks any more, so modded kit (entrenching tool, ACE Overheating weapons,
  character-mod heads) never blocks a body. Before, kit was removed piece by
  piece and a body that failed a check was left stripped. Items a player picked
  up or stored are never touched; bodies with an EBG keep component or valuable
  intel are still kept. ACE's overheating helper attachment and character-mod
  heads are accepted in Full cache snapshots. The zone status shows the native-save pause only while Full
  groups are actually absent, the admin cache monitor is hidden outside the GM
  editor, the GM entity list no longer throws when entities are deleted while
  paging, and a blocked Prepare for save names the group, its position and its
  zone.
- Garrison: "EXPBG Add Garrison" (renamed so it is distinct from Game Master
  Enhanced's "Add Garrison") now opens its squad picker on dedicated-server
  clients; the access check used a server-only editor lookup and always failed on
  clients. Every refusal now logs one `[EXPG GARRISON]` reason, and feedback also
  appears in chat for players with hints disabled. Window guards stand directly
  at the window (nearest window posts are chosen first).
- Intel Items: reading opens a dedicated window (title, scrollable text, Close /
  Escape) instead of a hint, so it works with hints disabled. One read per press
  (repeats within 1 s are ignored). `[br]` in the intel text makes a line break.
- Ambient Destruction: a CDF save of destroyed buildings loads after a server
  restart even when native mission persistence already removed the building;
  ambiguous matches are still refused. Building collapses are paced at least 3 s
  apart across all zones to avoid client freezes. Diagnostics is an Off /
  Lifecycle / Counts selector and sliders show whole numbers. Debug rings no
  longer draw huge wedges near the camera. Removed an invalid material remap
  from the Afghan truck wreck.
- Ambient Sounds: the crowd is now "EXPBG Ambient Crowd Sound". Radio, TV, crowd,
  war and sound modules share the same "On/Off" first and "Debug" last attribute
  order; saved settings are unaffected. All sound items use one speaker mini icon
  and the EXPBG badge. The debug legend moved to the right edge and is removed
  when no debug module remains and when the world ends. Failed-start retries for
  long-range sounds need at most 50 m of approach. Removed an unused sample.
- Ambient Civilians: traffic works again. A car's destination town had to be
  500-5000 m away and also inside the 400 m module radius, which no town can be;
  journeys may now leave the module radius (exclusion zones still apply), the car
  is freed from the radius once it departs and is removed out on the road, far
  from players. Neighbours visit each other's tables: the join range is 150 m, a
  nearby idle resident is called when a spot opens, and visitors walk back home.
  Deleting the module no longer leaves an empty civilian group. The debug summary
  sits at the top centre and the legend on the right, clear of the GM panels and
  the sound legend, and both are removed when the world ends. Civilian entities
  are excluded from CDF and vanilla saves with the editor's "not saved" flag
  instead of the obsolete `Serialize` override.
- No Game Master Budget: the switch is now "EXPBG Enable Game Master Budgets".
- Persistent Battlefield: weapon safety on spawn is applied only by the owning
  peer once the character is registered, removing the RPC errors on mission load
  and on clients.

## 0.1.3

- Ambient Sounds: 28 new placeable sounds under the EXPBG Sounds browser category,
  covering every sound from Vinny - Sounds by Vinuesa (Workshop 61D358A07E15C5FE,
  APL-SA): radio static, Apache, Russian, Chinese and Arab radio chatter, Hanoi
  Hannah, close and distant firefight, distant shelling, two jet flybys, a drone,
  market, market seller, street singing, Muslim prayer, traffic, church bell, car
  alarm, police car, Nokia ringtone, distant barking and distant sheep. They are
  labelled Radio transmissions, War sound effects, Crowd or Sound effects.
- Each sound is a new invisible EXPBG Ambient Sound module with Game Master
  On/Off, Recording, Volume, Audible distance (30-1500 m), Loop, Pause between
  repeats and Debug. New placements start Off at 50% volume; looping sounds
  default to Loop on, one-shots (jet flybys, drone, church bell, car alarm,
  police car, ringtone, barking) to Loop off. Settings are saved with the session.
- The sounds share the radio/crowd/TV playback limits (at most four of these
  voices at once, 32 active sources) and wake within 1000 m or their audible
  distance plus 50 m, whichever is larger. Radios, crowds, TVs and the war module
  are unchanged.
- Vinny - Sounds is not required and Game Master FX is not used. Thirteen
  recordings are included unchanged; the other fifteen reuse the radio, jet,
  firefight and shelling recordings Ambient Sounds already ships.

## 0.1.2

- New module: No Game Master Budget. A Game Settings switch, "Enable Game Master
  Budgets" (ON by default), lifts the Game Master placement budgets for props,
  AI, vehicles, waypoints and systems when OFF; campaign building budgets are
  unchanged. Same behaviour as the Disable Game Master Budgets mod, rebuilt from
  scratch (no code reused): toggling now applies and restores the budget limits
  immediately, and the switch is read from the replicated game mode so late
  joiners and new missions never act on a stale value.

- Unit Caching Group cleanup now works per casualty. A managed AI soldier's body
  and owned kit are deleted once that casualty reaches the Minimum corpse age and
  no player has come within the wake radius of the zone, the squad or the remains
  for the clear delay. The rest of the squad no longer has to die, and cleanup
  also runs while survivors are Simulation-cached or Full-cached. Survivors are
  never deleted or refilled; caching and restoration are unchanged. Bodies of an
  eliminated squad now go one by one as each reaches its own corpse age.
- A soldier killed by script while his group is Simulation-cached (hidden and
  untargetable) is not confirmed dead while the group sleeps; his body is kept
  and never deleted by the cleanup. Known limit; normal play cannot reach it.
- Remains that cannot be verified as safe to delete (unregistered or transferred
  contents, unapproved modded items, or a refused deletion) are retried at most
  three times, a minute apart, then handed back to the game's body cleanup
  (vanilla or Persistent Battlefield) with one `[EBG CLEANUP RELEASE]` log line.
  Remains protected by an EBG cleanup keep component or holding valuable intel
  stay protected as before (`[EBG CLEANUP KEEP]`), also when the same body or
  container holds other unverifiable items. Other casualties keep
  draining meanwhile.
- Debug group lines and zone markers show casualty cleanup progress for groups
  with survivors.

## 0.1.1

- First Workshop publication of the EXPBG GM Tools pack; content identical to
  0.1.0. The 0.1.0 publish attempt ended before any upload (the Workbench
  publishing window closed; the Workshop item did not exist afterwards), and
  that version stays retired under the release guard.

## 0.1.0

- EXPBG GM Tools becomes one modpack: Garrison, Unit Caching (formerly GM Optimizer
  0.1.31), Intel Items 0.0.4, Ambient Civilians 0.0.23, Ambient Destruction 0.0.9,
  Ambient Sounds 0.2.9 and Persistent Battlefield 1.0.5. Each module keeps its own
  folder; one assembled project, base game the only dependency.
- Do not combine with the standalone items or their CDF companions.
- Garrison: Full caching is the default and restores survivors at exact world
  transforms with their posts; casualties are never refilled. Freshly spawned
  squads are trimmed to the safe indoor capacity. Eight-neighbour interior
  planning, door-aware entrances and a native speed cap hold guards and blocked
  patrols in place.
- Unit Caching save guards now recognise the pack identity and a reserved pack
  CDF companion.

## 0.0.1

- Initial Garrison feature: one-squad building garrisons, reachable fixed posts,
  interior overflow patrol and independent activation policy using GM Optimizer.
- First-version caching is Off or Simulation with adjustable wake/sleep distances.
  Original actors are retained; Full caching and cold garrison save/load are unsupported.
- Native and multiplayer acceptance are pending; this is not a released version.
