# Placeables for GM byHeine assets

Selected vehicle wreck and fallen-soldier models, materials and textures are
adapted from Placeables for GM byHeine 3.1.5, by Heine.CRV.

- Source: https://reforger.armaplatform.com/workshop/61110CC4F1FF9C8A
- License on the public Workshop listing, checked 2026-10-02: Arma Public License Share Alike (APL-SA).
- Full license and warranty disclaimer: https://www.bohemia.net/en/licenses/arma-public-license-share-alike

Attribution, noncommercial use, Arma-only use and share-alike terms apply.
These adaptations remain under APL-SA. No endorsement by Heine.CRV or Bohemia
Interactive is implied. The assets are provided as-is under the license's
warranty disclaimer; no additional restrictions are imposed.

Changes: extracted only the resource closure for thirteen vehicle appearances
and three seated soldiers; relocated resources and assigned independent GUIDs;
replaced equal-length resource strings in compiled XOB headers while preserving
geometry, collision and every other chunk. Removed trailing whitespace from one
material. Corrected compiled-texture metadata to the native EDDS resource class,
preserving texture bytes and resource identities. Rebuilt standalone static prefabs,
retaining relevant material variants and removing invalid material overrides,
upstream inheritance, scripts, editor actions, destruction, persistence,
replication, sound, animation and all interaction components.

No upstream license/readme/credits/notice file was present in the downloaded
package. The Workshop license is recorded as the available permission evidence;
it does not independently establish ownership of every upstream contribution.
Base-game dependencies are referenced rather than redistributed. Original and
imported SHA-256 hashes, resource mappings and modifications are recorded in
docs/HEINE_ASSET_PROVENANCE.json in the source repository.
