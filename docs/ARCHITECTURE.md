# EXPBG GM Tools design

## Pack assembly

EXPBG GM Tools is one engine project (`addon/EXPBG_GM_Tools.gproj`,
`FC1402F65B2F4A45`) built from seven module folders listed in `tools/pack.json`.
`tools/Assemble-Pack.ps1` copies every module file to its original runtime path,
so resource GUIDs and `{GUID}path` references are unchanged. Three vanilla
overrides are shared and merged in module order: the `Edit.conf` attribute list,
the `Systems.conf` placeable registry and the persistence `GameMaster.conf`
(both `Configurations` and `PersistentStates`). Only the Ambient Sounds banner
PNG and two `license.txt` files are relocated. Anything else that collides,
duplicate metadata GUIDs, module `.gproj` files and Workbench-only scripts stop
the assembly. The pack project registers all five string tables.

Unit Caching (former GM Optimizer) keeps its `EBG_` classes. Its Full save gate
accepts the pack identity, and Unit Caching and Garrison check the reserved pack
CDF companion `07BC942D90324CD9` instead of the standalone Optimizer companion.

The contracts below describe Garrison. The other modules' contracts live in
their original repositories at the commits recorded in `tools/pack.json`.

## Product contract

One native SCR_AIGroup per building. All retained infantry remain members of it.
The GM context action starts bounded building analysis while the native squad
picker is open. Server validation owns the final selection, placement and roster.
Keep no more than one pending operation and one garrison per building.

Prefer authored sentinel positions, supplement with physically checked interior
positions for entrances, windows and stairs. Clear positions 1-3 m behind windows
or roughly 2 m from doors can watch openings blocked by nearby furniture.
Every accepted position must be on
the building floor, have character clearance and a connected entrance route.
Nearby navmesh alone does not prove entrance connectivity. Reject uncertain
locations, disconnected floors and roof surfaces without a usable entrance path.
Closed native doors with Soldier navigation links are traversable for planning;
their frames, walls and furniture still block traces. Spawn occupancy never
ignores a door leaf. Standing-volume overlap, structural support and interior
volume checks prevent using furniture tops or exterior porch nodes as posts.
The grid checks eight neighbors, including diagonals around furniture, with the
same body sweep and intermediate floor checks on each connection. Patrols require
interior endpoints and a short interior segment; reachable exterior entrance
nodes establish access but cannot become patrol destinations.
Cancellation and unsupported buildings create no orphaned squads or reservations.

Fixed guards stay at their assigned position, retaining aim, shooting, rotation
and stance changes. Overflow patrols use verified interior routes; native paths
that leave the building are not acceptable. Explicit fresh editor spawns may
trim initial surplus to the safe capacity, preserving the leader. A transaction
tracks exact native-spawned actor identities before editor callbacks; foreign
additions, removals or same-count substitutions invalidate it. Revalidation
precedes each deletion; the ordinary adoption path never authorizes trimming.
Zero safe positions still produce an honest refusal.
Force Move ends garrison enforcement once; ordinary simulation resumption is not
a GM order. Destruction invalidates posts and releases survivors to normal AI.
Player possession, transfers and deletion also release affected ownership safely.
Each fixed post owns a native character maximum-speed limit in addition to its
AI setting. The character combines limits from all sources; release removes only
the post's entry. Player input releases the post in the first controller callback.
No repeated position correction or disabling of the whole AI is used.

## Runtime and caching

One shared bounded scheduler; no per-soldier timers or repeated world scanning.
Each building record retains roster identity, local post transforms, route state,
cache lifecycle and release intent. Defaults: wake 300 m, sleep 400 m, adjustable
per garrison; these are starting values, not measured optimal distances.
Active combat or player possession prevents sleeping. Exactly one cache owner
may manage a garrison, including overlapping Optimizer zones.

Version 0.0.1 supports Off, Simulation and Full (default), using each garrison's
wake and sleep distances. Simulation retains the native group and original actors,
pauses their simulation and resumes those same actors; no soldiers are deleted
or recreated. Equipment and casualty state remain on the retained actors.
Full reuses Optimizer's standalone `EBG_PrefabFullCache` transaction, retaining
the original native group and shared logical survivor records. Only current
survivors are captured; recreated actors use prefab-default kits and health.
Post identity and patrol route state survive actor replacement. New actors receive
an owned LOD hold, then their controls before native AI release. Actual spawn
timing, world-transform parity and client behavior require native acceptance.
Partial deletion/restoration retains its recovery transaction; a previously
created slot or retained original is never spawned again after external removal.
An ungrouped newly created survivor whose first native group admission failed
remains held for recovery, distinct from an actor transferred externally. Original-group
deletion or foreign membership during restoration remains a recovery hold,
including save refusal, rather than silently creating another group.
Cold save/load restoration of the garrison ledger remains unsupported.
If safe ownership cannot be established, retain live AI and report the refusal.
No CDF-specific dependency or persistence guarantee is added to the core.

## Boundaries

Production depends on base game 58D0FB3206B6F859 and Optimizer F3B7C6FB18AB1F79.
Project identity FC1402F65B2F4A45 is newly allocated, not yet Workshop registered.
Runtime namespace EXPG; all new resources get independent GUIDs and unique paths.
Use native editor selection and server-authenticated requests. Treat delayed
selection/building references as stale until revalidated by the server.

Use the existing EXPBG black/antique-gold screenprint style, small top-left emblem,
small module title, separate square browser card and wide Workshop banner.
Import textures through native Workbench and verify the packaged pixels.

## Reference analysis

Installed Game Master Enhanced 1.3.8 (5964E0B3BB7410CE) was inspected read-only.
Its AddGarrison code creates a separate group per member, assigns a native Defend
order, and deletes members after slots run out. Its random navmesh/floor sampling
does not verify entrance-to-post connectivity. These observations motivate this
original design; no GME scripts, resources or generated positions are imported.

The Garrison ledger supplies post/patrol ownership around Optimizer's existing
Full survivor transaction. It is not a portable CDF snapshot bridge. Prepare for
Save restores surviving soldiers and releases ordinary squads before admission.

## Unit Caching casualty cleanup
Cleanup is per casualty. A dead, death-confirmed, never-possessed member's corpse
and only that member's owned rows are deletable after Minimum corpse age, player
clearance (Wake radius around the module, every member and every held remain) and
the continuous clear delay. The same check runs for awake, Simulation-suspended
and Full-cached records, never during Full transitions or recovery. Every node of
a deleted tree must belong to the same dead member, and the root must still be on
the ground, on that body or in that member's own container. One root is deleted
per 0.5 s scan across the server. A root that fails verification three times is
released to native garbage handling, unless its tree holds a keep component, a
mission-protected item or valuable intel anywhere (checked over the whole tree,
not only the first blocker met), which stays protected. A record with nothing
owned left settles and costs O(members) per tick; a casualty whose body is gone
and who has nothing left to delete no longer counts as mature, so a younger
casualty waits on the O(members) corpse-age hold without player-distance scans.
