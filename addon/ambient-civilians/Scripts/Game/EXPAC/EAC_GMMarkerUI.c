// Match vanilla SYSTEM modules, including their focus/selection layers.
// The separate marker texture has a 730x710 visible glyph on a 1254px canvas;
// a 48-unit slot shows roughly 28x27 units rather than the old 14x14 glyph.
// Browser cards use a different UI path and are unaffected.
class EAC_GMMarkerUI
{
 static bool Resize(Widget slot, typename ownerType)
 {
  if (!slot || !ownerType || (!ownerType.IsInherited(EAC_AmbientModule) && !ownerType.IsInherited(EAC_ExclusionZone))) return false;
  FrameSlot.SetSize(slot, 48, 48);
  return true;
 }
}

modded class SCR_EditableEntitySceneSlotUIComponent
{
 override void InitSlot(SCR_EditableEntityComponent entity)
 {
  super.InitSlot(entity);
  if (entity && entity.GetOwner()) EAC_GMMarkerUI.Resize(GetWidget(), entity.GetOwner().Type());
 }
}
