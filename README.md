# EXPBG GM Tools

Original Game Master tools by M.Pac and K.Edgar. The first feature is **Garrison**:
one-squad building garrisons with individual guard posts and indoor patrols.

Development candidate. Not yet compiled, gameplay verified, or published.

Right-click a building, choose **Add Garrison**, and select an infantry squad.
The intended behavior is one squad with individual fixed guard posts and indoor
patrol routes for surplus members. A GM Force Move releases the garrison.

The first version offers Off or Simulation caching with per-garrison wake and
sleep distances. Simulation retains the original actors, equipment and casualty
state. Full caching and cold save/load restoration of garrison ownership are
unsupported. Garrison is the current tool; EXPBG GM Tools is the pack name for
this and any future tools.

Requires Arma Reforger and [EXPBG GM Optimizer](https://reforger.armaplatform.com/workshop/F3B7C6FB18AB1F79)
(developed against 0.1.31).
Game Master Enhanced is not a dependency and no GME implementation is copied.

Before saving, use Optimizer **Prepare for Save** and wait for Ready. This restores
and releases garrisons as ordinary squads; post assignments are mission-only.
When CDF is installed, its Optimizer companion is required for the save-admission
hook. Without that companion, garrison caching stays off. The vanilla Game Master
native serializer and supported Optimizer-CDF export hooks refuse active ownership.
Other scenarios and CDF import over an active session still require acceptance;
Prepare for Save first. These are source contracts awaiting native verification.

See [architecture](docs/ARCHITECTURE.md), [testing](docs/TESTING.md), and
[artwork](docs/ASSETS.md). Source: [ExpBG-Tech/mod-gm-tools](https://github.com/ExpBG-Tech/mod-gm-tools).

Build with PowerShell 7: `./build.ps1 -NonInteractive` after configuring the local
addon destination. Portable checks: `./tests/Test-Tools.ps1`.

License: Arma Public License Share Alike (APL-SA).
