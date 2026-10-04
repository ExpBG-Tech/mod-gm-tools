# EXPBG GM Tools design

The pack's first feature is Garrison. The contracts below describe that feature.
Future tools can be added under the same pack name without renaming Garrison.

## Product contract

One native SCR_AIGroup per building. All selected infantry remain members of it.
The GM context action starts bounded building analysis while the native squad
picker is open. Server validation owns the final selection, placement and roster.
Keep no more than one pending operation and one garrison per building.

Prefer authored sentinel positions, supplement with physically checked interior
positions for entrances, windows and stairs. Every accepted position must be on
the building floor, have character clearance and a connected entrance route.
Nearby navmesh alone does not prove entrance connectivity. Reject uncertain
locations, disconnected floors and roof surfaces without a usable entrance path.
Cancellation and unsupported buildings create no orphaned squads or reservations.

Fixed guards stay at their assigned position, retaining aim, shooting, rotation
and stance changes. Overflow patrols use verified interior routes; native paths
that leave the building are not acceptable. If the full squad cannot fit safely,
refuse the placement with feedback rather than delete surplus soldiers.
Force Move ends garrison enforcement once; ordinary simulation resumption is not
a GM order. Destruction invalidates posts and releases survivors to normal AI.
Player possession, transfers and deletion also release affected ownership safely.

## Runtime and caching

One shared bounded scheduler; no per-soldier timers or repeated world scanning.
Each building record retains roster identity, local post transforms, route state,
cache lifecycle and release intent. Defaults: wake 300 m, sleep 400 m, adjustable
per garrison; these are starting values, not measured optimal distances.
Active combat or player possession prevents sleeping. Exactly one cache owner
may manage a garrison, including overlapping Optimizer zones.

Version 0.0.1 supports Off or Simulation caching using each garrison's wake and
sleep distances. Simulation retains the native group and original actors,
pauses their simulation and resumes those same actors; no soldiers are deleted
or recreated. Equipment and casualty state remain on the retained actors.
Full caching and cold save/load restoration of the garrison ledger are unsupported.
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

Optimizer's Full-cache path does not provide a complete garrison post-state
bridge. This first version uses Simulation only; native acceptance must still
verify that guards resume their posts and that no second cache owner intervenes.
