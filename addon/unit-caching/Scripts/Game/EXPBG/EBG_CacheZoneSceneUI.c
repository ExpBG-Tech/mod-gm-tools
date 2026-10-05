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

// Vanilla GM entity list gap (1.8): m_Entities holds weak references and ShowEntries has
// no null check, so paging between a deletion (cache sleep, casualty cleanup) and the
// queued Refresh threw NULL 'm_EntityState'. Same page logic, deleted entries skipped and
// a refresh queued; the refresh latch is cleared first so one exception cannot stick it.
modded class SCR_EntitiesToolbarEditorUIComponent
{
 override protected void ShowEntries(Widget contentWidget, int indexStart, int indexEnd)
 {
  Clear();
  bool skipped;
  indexEnd = Math.Min(indexEnd, m_Entities.Count());
  for (int i = indexStart; i < indexEnd; i++)
  {
   SCR_EditableEntityComponent entity = m_Entities[i];
   if (!entity) { skipped = true; continue; }
   CreateItem(entity);
  }
  if (skipped) QueueRefresh();
 }

 override protected void Refresh()
 {
  m_queuedRefresh = false;
  super.Refresh();
 }
}

modded class SCR_EditableEntityUIRuleTracker
{
 override bool HasState(SCR_EditableEntityComponent entity)
 {
  return entity && super.HasState(entity);
 }
}
