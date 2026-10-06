# Sources and attribution

**Structures For GM byHeine**, Heine (Heine.CRV), installed version 1.2.29, APL-SA.
Source: https://reforger.armaplatform.com/workshop/628EDA2ABC937159
License and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license-share-alike
Only the armored laptop, Tablet 2 and Server Rack 2 (`serverbox2`, used by Server Rack B) model/material/texture closures are imported.
Resource GUIDs and paths have been remapped under `EIIArt/`; texture bytes are unchanged. The space in `mat3 _Base_Color.edds` was removed from the filename.
Compiled-model reference replacements preserve byte lengths. No endorsement is implied.
The exact 23 input/output paths, identities and SHA256 hashes are recorded in `docs/licenses/intel-items/imported-assets.json`.

**Placeables for GM byHeine**, Heine (Heine.CRV), installed version 3.1.5, APL-SA.
Source: https://reforger.armaplatform.com/workshop/61110CC4F1FF9C8A
License and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license-share-alike
Only the server rack (`ServerRack`, used by Server Rack A) and external USB hard drive (`HD`, used by the USB Drive) model/material/texture closures are imported.
Resource GUIDs and paths have been remapped from `Models/` to `EIIArt/`. In the compiled models only equal-length reference strings in the XOB header chunk were replaced; all other model bytes are unchanged. Texture bytes are unchanged. The space in `Manufacturer Info_BaseColor.1003.edds` was removed from the filename.
Our own prefabs add the gameplay components, the drive's collision box and its weight. No endorsement is implied.
The exact 12 input/output paths, identities and SHA256 hashes are recorded in `docs/licenses/intel-items/imported-assets.json`, produced by `tools/art/import_heine_assets.py`.

**Game Master Enhanced**, GME Mod Team, installed version 1.3.8, APL.
Source: https://reforger.armaplatform.com/workshop/5964E0B3BB7410CE
Upstream: https://github.com/zen-mod/GME_AR
License and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license
`EII_StringAttributeVar.c`, `EII_EditboxEditorAttributeUIComponent.c` and the two EXPII editbox layouts adapt GME's string-attribute serialization and widget integration. Names, references and private state are namespaced; byte limits and cleanup were added. GME's modded serializer super-chain is preserved. These adapted files retain APL. The addon does not require or redistribute the complete GME mod.

**Bohemia Interactive**: vanilla field manual/notebook models, materials, core inventory, editor and widget resources are referenced from the base game and are not copied into this repository. The imported racks also reference the base-game `metal.gamemat` and `Bookshelf_01_Glass.emat`, which are not copied.

**Microsoft Windows XP startup recording**: supplied by the user as `Microsoft Windows XP Startup Sound_0_5.wav`. Imported byte-for-byte as `Audio/EXPII/EII_Startup.wav`. This recording is not relicensed under APL-SA. File metadata and source SHA256 are recorded in `docs/audio-source.json`; this source build does not establish publication clearance.

**EXPBG shared build/install tooling**, ExpBG Tech, MIT. Adapted from `mod-ambient-sounds`; the installer additionally permits model `.xob` files. Full retained notice: `licenses/EXPBG-tools-MIT.txt`.

**EXPBG GM Optimizer release tooling**, ExpBG Tech (M.Pac and K.Edgar), APL-SA. `release.ps1`, release configuration/staging/publication helpers and their portable tests are adapted from revision `10429eb6341a34fd1015641f306a5427ad66bb07` of `mod-gamemaster-optimizer`. Changes select Intel identities/versions/previews, require explicit orchestrator slots, enforce Unlisted first-publication flow, preserve custom upstream notices, and include attribution in source stages. Same APL-SA license/warranty URL as original contributions above. Existing MIT tools retain their separate notice.

**Artwork**: user-supplied EXPBG emblem and source screenshots used as references with the built-in image-generation tool. Final cards and root-supplied master art are documented in `docs/artwork.md`. They are illustrative catalogue cards; the actual in-game meshes remain the referenced/imported objects.

**CDF GameMaster Save** is an optional dependency, not redistributed. The companion targets observed 1.4.1 hooks and stores upstream state without interpreting or replacing it.

Required runtime attribution is also included as a comment-only script so a Workshop resource package retains it.
