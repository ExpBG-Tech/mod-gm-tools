// Native positional GM icons only. Lists, browser entries and edit permissions
// use other UI paths and remain unchanged. No entity flags or replicated state.
modded class SCR_EditableEntitySceneSlotUIComponent
{
 protected bool EBG_DenyZoneBadge()
 {
  return m_Entity && (EBG_CacheZone.Cast(m_Entity.GetOwner()) || EBG_OptimizerController.Cast(m_Entity.GetOwner())) && !EBG_CacheVisuals.Authorized();
 }

 override void InitSlot(SCR_EditableEntityComponent entity)
 {
  super.InitSlot(entity);
  if (m_Widget && EBG_DenyZoneBadge()) m_Widget.SetVisible(false);
 }

 override Widget CreateWidgetForEntity(SCR_EditableEntityComponent entity, SCR_EntitiesEditorUIRule rule, ResourceName layout)
 {
  Widget widget = super.CreateWidgetForEntity(entity, rule, layout);
  if (m_Widget && EBG_DenyZoneBadge()) m_Widget.SetVisible(false);
  return widget;
 }

 override vector UpdateSlot(int screenW, int screenH, vector posCenter, vector posCam)
 {
  if (EBG_DenyZoneBadge())
  {
   if (m_Widget) m_Widget.SetVisible(false);
   return vector.Zero;
  }
  // Native positioning and visibility restore naturally after real login.
  return super.UpdateSlot(screenW, screenH, posCenter, posCam);
 }
}
