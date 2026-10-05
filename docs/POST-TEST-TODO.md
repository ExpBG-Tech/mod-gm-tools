# EXPBG GM Tools: post-test tracker

Single source of truth for everything requested after the 2026-10-05/06 client test.
Status: `[ ]` open, `[~]` in progress, `[s]` fixed in source (native/client verification pending), `[x]` done and verified, `[-]` dropped.
Order matters: finish each milestone before starting the next unless marked parallel.

## Goals and targets

| # | Milestone | Target / exit criterion |
|---|---|---|
| M1 | Post-test fixes | Every finding below fixed in source, native fixtures green, portable tests green |
| M2 | Release | GM Tools 0.1.4 (+ CDF Compat 0.1.2 if touched) on GitHub and Workshop (unlisted), listing text updated |
| M3 | Focused re-test | Restart server + client; re-test **only modified areas**; all green or new findings filed |
| M4 | Cleanup of old world | 6 standalone Workshop items removed (user confirms), issues re-created sanitized (23) / closed (8), old repos archived, local folders in Recycle Bin |
| M5 | Issues to zero | 0 open issues in mod-gm-tools and mod-cdf-compat (obsolete ones closed with a comment) |
| M6 | New features (start only after M1-M5, user decision) | New addons below implemented, natively verified, previews in EXPBG style, released, re-tested in client |

## M1: Post-test fixes (from the client test)

Native gates for the 0.1.4 source: build `local-20261005-183149-156` compiled with 0 errors from our code; contracts PASS (incl. window slots); Garrison gameplay PASS (59 checks); UnitCleanup PASS with the body + weapon rule (`checks=95 failures=0 cases=10`, every casualty `body=1 roots=2` in one tick, evidence `build/gameplay-20261005-183223-510`); portable tests PASS.

### Unit caching
- [s] Cleanup is not atomic: clothes/gear deleted root by root, body released, stripped corpse left in the world. User decision 2026-10-06: just delete the body (one native call, everything inside goes with it) and its dropped weapon in the same tick; no per-item checks. Keep component / valuable intel still park the body.
- [s] Cleanup allow-list too strict for the production modset: vanilla E-tool (`SCR_DeployablePlaceableItemComponent`, `SCR_MultiPartDeployableItemComponent`), `ACE_Overheating_BarrelComponent`; Full provenance: character-mod heads, ACE overheating helper attachment.
- [s] Zone status keeps "Native saves are paused while Full groups are absent" with 0 cached.
- [s] Admin cache monitor stays visible in first person.
- [s] GMO #18 fixed; GMO #21 and #16 diagnostics only (release/discard escape not done); GMO #17 needs a wake-budget decision.
- [ ] Cache zone world icon hidden for voted (non-admin) GMs: decide intended behaviour.

### Garrison
- [s] EXPBG Add Garrison opens no picker on dedicated-server clients (root cause: `SCR_EditorManagerCore.GetEditorManager(playerId)` is server-only, so `EXPG_Authorized` always failed on clients; GME not involved). Fix + bail-out logging in source; needs native/client verification.
- [s] Rename context action to **EXPBG Add Garrison** (GME also has "Add Garrison").
- [s] Units on window posts should stand closer to the windows.
- [s] Feedback uses hints only (invisible with hints disabled).

### Intel items
- [s] Read text uses hints; players with hints off see nothing: dedicated read UI.
- [s] Read fires 3x per press: debounce.
- [s] Multiline intel text loses the newline.

### Ambient destruction
- [s] CDF cold load refused after restart (`[EAD CDF HOLD] Missing or ambiguous building`): missing original + saved destroyed = satisfied; GM-visible refusal.
- [s] "Layout seed" and "Server diagnostics" shown as decimal sliders: integer / selector.
- [s] Debug rings may render as a huge white dome.
- [s] Issue drafts EAD #5 (AfghanTruck material remap), EAD #6 (collapse pacing).
- [ ] Re-test disable/enable and module deletion cleanup in the client (not reached in the first session).

### Ambient sounds / No GM Budget
- [s] Rename "EXPBG Crowd Module" to **EXPBG Ambient Crowd Sound**.
- [s] No GM Budget switch label gets the **EXPBG** prefix.
- [s] 7 sound-effect modules use the generic logo preview; mini icons review.
- [s] Verify EXPBG label 157026 on every editable prefab (EXPBG faction filter).
- [s] Consistent attribute naming/order for radio, TV, crowd, sound modules (On/Off first, Debug last).
- [s] Debug legend overlaps the GM panel and the civilians legend, and survives a rejoin (widget leak).
- [s] Remove unused `EAS_WindowsXP_Startup.wav` if unreferenced.
- [x] Native audio sweep green on published 0.1.3 (standard mode, all banks; evidence `build/audio-sweep-20261005-171659-294`; harness fix `@($busy).Count`). Re-run after the sounds changes.

- [s] Generate correctly labelled previews in the EXPBG black/antique-gold style (done: `tools/art/New-TitleCard.ps1` + `tools/art/Import-PackArt.ps1`, cooked natively, new GUID {9FA40A6AFCC72D32} for SOUND EFFECTS): "SOUND EFFECTS" (7 sound-effect modules now borrow the Ambient Civilians art) and "AMBIENT CROWD SOUND" (crowd art still reads "CROWD MODULE"). Source artwork: coordination root `artwork-v2`.
- [s] Sounds #11: retry approach capped at 50 m.

### Ambient civilians
- [s] Conversations never happen (`invite ... ok=0` after 40 min).
- [s] Traffic never runs with cars enabled (`traffic parties=0 ... live_cars=0`).
- [s] Empty civilian group left after deleting the module.
- [s] Civilians debug legend overlap.
- [s] Issue drafts Civ #8 (migrated off obsolete Serialize). Civ #4 (native junction driving) and Civ #5 (vanilla smoking preset particles) not ours - close with evidence.

### Persistent Battlefield
- [x] Weapon safety sent RPCs through unregistered controllers (server x28 on load, client proxies): owner-only, waits for RplId (uncommitted, in source).
- [ ] Re-test vehicle wreck persistence and corpse protection near players (not reached).
- [ ] Decision needed: corpse protection 50 m (pack) vs 80 m (standalone 1.0.6) - PB #4.

### Cross-cutting
- [ ] Entity property dialogs are titled "Editing: Scenario properties" (check whether vanilla or a mod causes it).
- [ ] Restore `ReforgerEngineSettings.conf` from `.local/e2e/settings-backup` after the final client test.
- [ ] Update CHANGELOG, README, Workshop description, docs/TESTING.md, E2E runbook.

## M2: Release
- [ ] mod-cdf-compat: update `tools/pack.json` blob for `Scripts/Game/EAD_CDF/EAD_CDF.c` (new `c43d59b91cbec3cd29e133b41a171a2d4b677eac`) + CDF_CHANGELOG/CHANGELOG entry (GM-visible refusal dialog).
- [ ] Commit per module, push, `release.ps1 -Publish -Interactive` (listing text changes), GitHub release.
- [ ] CDF Compat: bump pin to the new GM Tools tag, release if changed.

## M3: Focused re-test (only modified areas)
- [ ] Rejoin flow from the runbook; cleanup (US squad with E-tool), Garrison with GME loaded, Intel read UI, CDF cold load after restart, civilians conversations/traffic/leak, renamed/relabelled browser items, debug legends, audio sweep.

## M4: Old world cleanup (user confirms Workshop removal clicks)
- [ ] Remove 6 standalone Workshop items: GM Optimizer F3B7C6FB18AB1F79, Intel Items E110000000000001, Ambient Civilians A9C45E82D6710B3F, Ambient Destruction E2A47D19C8B6503F, Ambient Sounds A93E9F6271894A3C, Persistent Battlefield 6A32DB878B264D05 (old CDF companions stay).
- [ ] Re-create 23 sanitized public issues (drafts in `.local/issue-drafts/`), close 8 originals as obsolete, comment + close all originals with pointers.
- [ ] Archive the 6 old repos with a pointer to mod-gm-tools.
- [ ] Move the 6 local folders to the Recycle Bin.

## M5: Issues to zero
- [ ] Work every open issue; close obsolete ones with a comment.

## M6: New features (new addons in the GM Tools pack)

Every new addon: own folder `addon/<name>/`, EXPBG label 157026, EXPBG prefix in UI text, preview images in the existing black/antique-gold style (generated from the existing artwork with new text), GM attributes in an **EXPBG** tab where possible, server-authoritative, JIP-safe, CDF-aware where state matters, native fixture + client test.

### F1 `unit-scripts` (AI unit behaviour)
- [ ] **Hold position**: unit stops roaming, stays in place, may still turn and change stance.
- [ ] **Freeze**: fully static (no movement, no stance change), only the head tracks.
- [ ] **Release**: return the unit to normal AI.
- [ ] **Animation state**: pick from an approved animation list (ambience); unit stays in it.
- [ ] Hold/freeze/animation break on damage and on GM release.
- [ ] **Force Night Discipline** (group): Light Discipline (flashlights off, NVG only) or Terror Tactics (flashlights on, pointed at players).
- [-] ~~Tie Hands / Ziptie~~: dropped (user decision 2026-10-06; no ACE interaction). Surrendered units get our own intel/interrogation interaction in F5 instead.

### F2 `unit-dialog`
- [ ] Unit name (use existing vanilla/custom name if the game already supports one).
- [ ] Up to 10 dialog lines in the unit's EXPBG properties tab.
- [ ] Player action "Speak to <Name>" opens a dialog window: Next / Continue until the end, then restart from the top or End conversation.
- [ ] Optional: talking gesture/animation while the conversation is open. Voice lines out of scope.

### F3 `advanced-briefing-map` (feasibility first; **skip entirely if a true live map on the whiteboard is not possible** - user decision)
- [ ] Find a large whiteboard model (Heine's placeables or vanilla) readable from a distance.
- [ ] One player at a time interacts (lock); their map opens automatically.
- [ ] Their markers and drawing are streamed to the whiteboard for everyone; closing the map ends streaming and releases the lock.

### F4 `intel-items` update: server rack + USB drive
- [ ] Server rack (Heine's placeables, 2 variants): not pickable, holds title/text + download timer (seconds).
- [ ] Interaction only with a USB drive anywhere in the inventory: "Download intel"; abort if the player leaves ~3 m; progress shown to the downloading player.
- [ ] Downloaded intel is stored on the USB drive; shareable by handing the drive over; hovering the drive shows the intel.

### F5 `ai-surrender` (global module)
- [ ] GM-set surrender chance, squad casualty threshold, random factor.
- [ ] Surrender: drop weapon + surrender animation (confirm vanilla support).
- [ ] Interaction "Interrogate" with a chance to: reveal one nearby squad on the map (removable marker), or give his name, bio and squad leader name.

### F6 `ai-global-skills` (global module)
- [ ] Per-faction tabs (auto-detected, works with RHS etc. without a compat mod): skill level and aim accuracy with overrides for **4 role groups: Rifleman, MG/LMG, Marksman, Leader** (user decision), defaults = vanilla.
- [ ] Rules of engagement (also right-click per group): Return Fire Only / Fire on Sight / Warning Shots First (2-3 rounds near the player, 5 s pause, then lethal).
- [ ] Ammunition: unlimited magazines or auto-refill x times when out of ammo (magazines only, no grenades).

### F7 `ambient-unrest` (Civil Protest Zone)
- [ ] Circular zone: 10-15 unarmed civilians in one group gather at the centre, static (no roaming/stance change), protest animation loop (arms raised, shouting), angry crowd audio from Ambient Sounds.
