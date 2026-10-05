# EXPBG GM Tools artwork

The pack uses the EXPBG black and antique-gold engraved artwork style. The square
card identifies the Garrison feature; the wide Workshop banner identifies GM Tools.
Both images were created with Codex's built-in image generation, using the existing
EXPBG GM Optimizer card as a visual style reference. No GME artwork was used.

Source PNGs are in `addon/garrison/UI/Textures/EXPBG_Garrison/`:

- `EXPG_Card.png`: square Garrison feature artwork.
- `EXPG_Workshop.png`: wide GM Tools banner with a smaller Garrison subtitle.

The final banner editing prompt was:

> Edit this wide Workshop banner for a renamed Arma Reforger mod pack. Preserve the
> black and antique-gold engraved military artwork, cutaway house, soldiers, logo,
> wide aspect ratio and overall visual style. Replace the bottom title GARRISON
> with the exact title GM TOOLS, same gold military condensed lettering. Add a
> discreet smaller subtitle GARRISON beneath GM TOOLS to identify the first included
> feature. Keep everything legible with comfortable margins. No other changes.

The square card was imported with Workbench's native Reimport action. The resulting
`EXPG_Card.edds` is 313 x 313 pixels (294246 bytes), with resource GUID
`5ACF724CA7564555`. Import receipt and rendered preview are retained under
`build/root-art-ui-20261004-180502/`. The indexed native build
`build/root-garrison-full-20261004-1824/` includes that texture.

Actual packaged UI presentation remains unverified. The native squad selector
does not expose a header-image field in its display config. The small context
menu icon remains a stock icon; the detailed poster is not suitable at that size.
