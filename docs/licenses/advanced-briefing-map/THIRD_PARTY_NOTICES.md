# Sources and attribution

**Structures For GM byHeine**, Heine (Heine.CRV), installed version 1.2.29, Arma Public License Share Alike (APL-SA).
Source: https://reforger.armaplatform.com/workshop/628EDA2ABC937159
License and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license-share-alike

Only the model closure of "Prop - Projector Screen" (`Models/NewProps/Tela_Projetor`) is imported: the compiled model `TelaProjetor.xob`, the screen frame and fabric material `telao.emat` and its two textures. Resource GUIDs and paths are remapped from `Models/` to `EBMArt/`. In the compiled model only the two equal-length material reference strings in the XOB header chunk were replaced; geometry, collision, LODs and every other byte are unchanged. Texture bytes are unchanged.

The upstream screen image (`tela1.emat` with `perde_gorsel_diff.edds`, a captured video-player frame) is not redistributed: `tela1.emat` is an original EXPBG placeholder material without textures, kept under the remapped identity so the compiled model reference resolves. Heine's prefab, which inherits the base-game approach radar, is not imported. The EXPBG prefab `PrefabsEditable/EXPBM/EBM_BriefingProjector.et` uses the model, assigns the EXPBG render-target material to the screen surface (`tela1`) and keeps the imported material on the frame and fabric (`telao`).

These adaptations remain under APL-SA: attribution, noncommercial use, Arma-only use and share-alike terms apply. No endorsement by Heine.CRV or Bohemia Interactive is implied. The assets are provided as-is under the license's warranty disclaimer; no additional restrictions are imposed on them.

No upstream license, readme, credits or notice file was present in the downloaded package. The APL-SA designation comes from the Workshop listing (local Workshop metadata, checked 2026-10-07); it does not independently establish ownership of every upstream contribution. The base-game surface material `plastic.gamemat` referenced by the model is not copied. The briefing board also references base-game map layouts, imagesets, textures and map configs, which are not copied.

The exact input/output paths, identities and SHA256 hashes are recorded in [imported-assets.json](imported-assets.json), produced by `tools/art/import_heine_assets.py`.

Required runtime attribution is also included as the comment-only script `Scripts/Game/EXPBM/EBM_DistributionNotices.c` and in `Credits/EBM_ASSET_CREDITS.txt`, so the Workshop resource package retains it.
