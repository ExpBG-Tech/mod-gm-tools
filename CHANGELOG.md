# EXPBG GM Tools changelog

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
