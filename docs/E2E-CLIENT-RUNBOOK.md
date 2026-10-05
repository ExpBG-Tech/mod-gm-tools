# EXPBG GM Tools: client e2e runbook

How the full in-game test of the pack is run on a real dedicated server with the
production modset, written so a computer-use agent can repeat it quickly. Notes are
added while the session runs; observed results live in the session log at the end.

## 1. Setup

- Server config: `.local/e2e/make-config.mjs <dir>` writes `modset.json` (the production
  modset, 118 mods incl. ACE, RHS, CDF GameMaster Save, EXPBG GM Tools, EXPBG CDF Compat)
  and `server-local.json` (127.0.0.1:2001, no password, random admin password, scenario
  `{59AD59368755F41A}Missions/21_GM_Eden.conf`, `visible:false`). Never copy production
  secrets into it.
- Binaries must match: retail `ArmaReforgerServer.exe` with retail `ArmaReforgerSteam.exe`.
  A Diag server kicks a retail client with `IS_DEV_BINARY_MISMATCH`.
- Server: `Start-Process ArmaReforgerServer.exe -WorkingDirectory <server install dir>
  -ArgumentList -config <server-local.json> -profile <dir> -logsDir <dir>
  -addonsDir "Documents/My Games/ArmaReforger/addons" -addonDownloadDir "Documents/My Games/ArmaReforger" -maxFPS 60`.
  Without the working directory it fails with `Game addon 58D0FB3206B6F859 not found`.
  Ready when console.log shows `Direct Join Code: NNNNNNNNNN` (changes every start).
- Client: `ArmaReforgerSteam.exe -disableCrashReporter -addonsDir <same> -addons <ids> -logsDir <dir>`,
  standard profile (CVON needs it). Never pass `-client 127.0.0.1:2001`: it skips backend
  auth, the handshake times out and the game shows "Game Initialization Error".
- Window: the game renders at the size in `ReforgerEngineSettings.conf` (ScreenWidth/Height,
  WindowPosX/Y); resizing the window does not resize the render. Back the file up before
  changing it and restore it afterwards.

## 2. Join and become Game Master

1. Title screen: middle mouse (CloseSplashScreen is bound to mouse:button2 in the profile).
2. Multiplayer -> **Host** tab (empty list; Favorites auto-selects real servers, All may
   have a selected row) -> Direct join -> click field, ctrl+a, type code, Return -> Confirm
   -> High ping server -> Confirm.
3. Verify server console: `Authenticated player ... name=<player>`, `Players connected: 1 / 1`.
4. GM Eden: Pause (middle mouse) -> Players -> row `...` -> Start vote to become Game Master.
   Server logs `voteEnd: SCR_VotingResultData{flag_vote_success=1}`. The role survives a
   rejoin. Deployment Setup then shows a Game Master button.
5. Menu/dialog buttons: mouse down, 0.15-0.2 s, mouse up. **World placement and world
   selection: instant click.** A slow press in the world is a press-and-hold, so the GM
   silently does not place (the ghost keeps following the cursor; always confirm a placement
   by moving the cursor away or by the bottom-right entity panel). German layout: the
   game's `Y` key is sent as `z`. Tap `z` = ping GM; hold `z` 1.5 s = toggle editor /
   Respawn menu. Context menu on an entity: double click it (this also recentres the camera
   on it), re-read its screen position, hover, then right click. Context menu items need a
   slow click.
   Slider attributes (e.g. Server diagnostics) accept Left/Right keys after one click; typing
   does not change them.
6. Focus: a Windows TextInputHost window can steal the foreground after typing; with
   textinputhost.exe granted, one click on the game title bar returns focus.

## 3. GM navigation

- Placement browser: bottom toolbar `TAB Open Entity Browser`. If it shows only one card,
  press `R Reset filters`. Faction filter **EXPBG** shows the pack's items (4 pages).
- Camera jump: `m` map -> right click a spot -> Move camera. Map context also offers
  Create player and Add Garrison.
- Entity properties: hover the entity, right click -> Edit properties... ; save with
  `ESC Save and close`.
- Scenario settings: bottom left `Edit scenario properties` -> wrench tab (Game).

## 4. Player entity for triggers

Caching, Civilians, Garrison wake, Persistent Battlefield and Intel need a real player
character inside the zone; the GM camera is not a player observer (sounds also accept the
GM camera as a listener). Fastest: right click terrain -> **Create player...** -> pick a
character (US Rifleman). The character stays in the world while the GM editor is open;
hold `z` to drop into first person and back. Entity context menus offer **Teleport player**
to move the character into or out of a zone. GM Eden has no faction spawn point.
Persistent Battlefield: killing the client process logs `[EXPBG Reconnect] Reserved living
character`; rejoining shows "Reconnect successful" with the same body and loadout.

## 5. Cases and evidence

| Area | UI action | Evidence |
|---|---|---|
| Browser catalog | EXPBG filter, page through 4 pages | 2 caching, 7 intel, civ module + no-civ zone, destruction, war module, 3 radios (Red is legacy, hidden), crowd, TV, 28 sounds |
| No GM Budget | Scenario properties -> Game -> Enable Game Master Budgets No -> Save | both peers: `[EXPBG NO BUDGET] Game Master budgets enabled=0` |
| Sound module | place, Edit properties: On, Debug, distance, Save | server `[EAS DIAG] action=activation ... active=1`; client `action=play ... event=SOUND_EAS_VINNY_<NAME>_R<range> handle=<n>` |

## 6. Session log (2026-10-05)

- Retail server PID 84544, client joined; 0 `SCRIPT (E)` from EXPBG code at start.
- Server start shows 28 `Attempting to send an RPC through unregistred item
  SCR_CharacterControllerComponent` (Persistent Battlefield weapon safety on world-placed
  characters before registration) -> fixed in source (owner only, waits for RplId).
- Browser: all 28 Vinny sounds present; 7 sound-effect modules use the generic EXPBG logo
  preview; the Crowd item is named "EXPBG Crowd Module" (rename to Ambient Crowd Sound).
- Map/world context menu lists "Add Garrison" twice.
- No GM Budget OFF replicated to the client.
- Radio static module played `SOUND_EAS_VINNY_RADIO_STATIC_R300` with the debug range ring.
- The entity properties dialog title read "Editing: Scenario properties" right after the
  scenario dialog had been open (to recheck).

### Session log, part 2 (2026-10-06)

| Area | Result | Evidence |
|---|---|---|
| Admin | PASS | Chat (`-` key on DE layout opens chat) `#login` without password for a listed admin -> server `Player '<player>' signed in as server admin.` Cache overlays/zone icons only render for logged-in admins. |
| No GM Budget | PASS | OFF: all four budget meters 0% with a squad placed; ON: AI 8%, systems 1%. Readback persists. |
| Persistent Battlefield reconnect | PASS | kill client -> `[EXPBG Reconnect] Reserved living character`; rejoin -> "Reconnect successful", same body. |
| Civilians spawn/routines/seating | PASS | `[EAC] players=1 ... reserved=5/24`, routines 32/32, seatv start=7 |
| Civilian conversations | FAIL | after ~40 min `invite none=40 far=41 ok=0` |
| Civilian traffic (cars Yes) | FAIL | `traffic parties=0 ... trips=0/0 live_cars=0` with towns=34 |
| No Civilian Zone | PASS | `exclusion_completed=2`, exclusion gate rising |
| Civilians module removal | PASS with leak | `[EAC controller] removed ... reserved=3`; an empty civilian AI group (size 0) remains |
| Destruction | PASS (generation) | `[EAD COUNTS] records=17`, destroyed=2, wreckPlaced=17/25 |
| Unit caching Simulation sleep/wake | PASS | `Simulation cached`, wake `Original entities and saved local state restored` |
| Unit caching Full sleep/wake | PASS | `Full cached: living unit prefabs recorded`; wake 7 alive / 2 dead (no refill) |
| Per-casualty cleanup | FAIL (modset) | bodies `CLEANUP RELEASE attempts=3`: vanilla US E-tool `SCR_DeployablePlaceableItemComponent` and `ACE_Overheating_BarrelComponent` are not on the approved component list; heads from character mods and ACE helper attachment fail Full provenance |

Findings / TODO:
- Cleanup allow-list: approve `SCR_DeployablePlaceableItemComponent`, `SCR_MultiPartDeployableItemComponent` (deployed state is already protected) and `ACE_Overheating_BarrelComponent`; accept character-mod heads that are verified model leaves of the corpse.
- Add Garrison appears twice in context menus.
- Entity dialogs are titled "Editing: Scenario properties".
- Civ and sound debug legends overlap each other and the GM bottom-left panel.
- Destruction "Server diagnostics" and "Layout seed" show as decimal sliders (0.00 / 1.00).
- Zone status keeps "Native saves are paused while Full groups are absent" with 0 cached.
- Cache zone world icon hidden for non-admin (voted) GMs.
- Crowd module rename to "Ambient Crowd Sound"; 7 sound-effect modules use the generic EXPBG logo preview; mini icons review; EXPBG label check.
- No GM Budget attribute label needs the EXPBG prefix (user request).
- Unused asset `Samples/SoundEffects/EAS_WindowsXP_Startup.wav`.
- PB weapon-safety RPC through unregistered controller (server 28x on load, client on proxies) -> fixed in source.
- **Cleanup is not atomic (user-reported, confirmed):** `[EBG CLEANUP REMOVED] ... body=0` x47 deletes clothes/equipment roots one by one, then the body root is blocked (E-tool) and released, leaving a stripped corpse. Fixed in 0.1.4 (user decision): the body is deleted in one native call with everything inside it, plus its dropped weapon in the same tick; no per-item checks.
- The duplicate "Add Garrison" is ours plus Game Master Enhanced (5964E0B3BB7410CE) garrison action. TODO: rename ours "EXPBG Add Garrison" (user request), like the No GM Budget switch.
- TODO (user): garrison units on window posts should stand closer to the windows.
| Intel place + attributes | PASS | title/text/diagnostics saved; server `[EII] diagnostics enabled` |
| Intel read | FAIL (design) | server logs `[EII] read` (3x per 0.3 s press: no debounce) but the text is shown with `SCR_HintManagerComponent.ShowCustomHint`; this profile has hints disabled (`m_bHintsEnabled 0`), so nothing is displayed. Needs a dedicated read UI. Garrison replies also use hints. Multiline text: the newline became a space in the attribute box. |
| EXPBG Add Garrison | FAIL (modset) | with Game Master Enhanced loaded our context entry opens no picker and logs nothing; GME's own "Add Garrison" ("Choose garrison") works. Add bail-out diagnostics to EXPG_OpenPicker / Perform. GME garrison groups are then "Regroup held" (blocked) by unit caching. |
| Player action key | note | this profile maps PerformAction to left mouse; actions only fire with the weapon holstered (`x`). |
| Admin cache monitor | note | stays on screen in first person (text says it needs GM open). |
| CDF save | PASS | `[CDF][GMSave] Sauvegarde 'E2E_Pack_1' (17 entites)`; EAS session-save x5, `[EII] CDF captured`, `[EAD STATE] reason=save-snapshot`; sha256 84b31b0c...; GME garrison groups skipped as author-less |
| CDF cold load (server restart) | FAIL | vanilla persistence is active on GM_Eden and restores GM entities before CDF; load refused by `[EAD CDF HOLD] Missing or ambiguous building: PierShed_01` (EAD_BuildingSnapshot.Preflight: building already gone via native persistence, fresh ledger has no history). No GM-facing message. Fix: missing original + saved Destroyed = satisfied; keep failing on >1 match. |
| Sound debug legend | note | the client legend widget survives a rejoin to a fresh server (leak). |
