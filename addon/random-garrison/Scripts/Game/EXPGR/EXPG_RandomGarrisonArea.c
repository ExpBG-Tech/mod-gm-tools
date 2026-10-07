// Game Master view of the Random Garrison radius: a vanilla area mesh following the
// terrain, built only while the editor reports the zone as rendered (the Civil
// Protest Zone pattern). Players and a dedicated server allocate nothing.
[ComponentEditorProps(category: "EXPBG/Garrison", description: "Draws the Random Garrison radius for Game Masters")]
class EXPG_RandomGarrisonAreaComponentClass : SCR_BaseAreaMeshComponentClass
{
}

class EXPG_RandomGarrisonAreaComponent : SCR_BaseAreaMeshComponent
{
 static const float DRAW_DISTANCE_MARGIN = 800;
 protected bool m_bBuilt;

 // Read live from the replicated radius, so a Game Master's edit resizes the mesh.
 override float GetRadius()
 {
  EXPG_RandomGarrisonModule zone = EXPG_RandomGarrisonModule.Cast(GetOwner());
  if (!zone)
  {
   return 0;
  }
  return Math.Clamp(zone.GetRadius(), EXPG_RandomGarrisonModule.RADIUS_MIN, EXPG_RandomGarrisonModule.RADIUS_MAX);
 }

 // Deliberately does not generate the mesh: only a rendering editor builds it.
 override void EOnInit(IEntity owner)
 {
  if (!owner || !EXPG_RandomGarrisonModule.Cast(owner)) { return; }
  ApplyDrawDistance();
 }

 // The setter, not the prefab field: the editable stores the squared distance.
 void ApplyDrawDistance()
 {
  IEntity owner = GetOwner();
  if (!owner) { return; }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(owner.FindComponent(SCR_EditableEntityComponent));
  if (editable) { editable.SetMaxDrawDistance(GetRadius() + DRAW_DISTANCE_MARGIN); }
 }

 void Build()
 {
  if (System.IsConsoleApp()) { return; }
  ApplyDrawDistance();
  GenerateAreaMesh();
  m_bBuilt = true;
 }

 // Rebuild after a radius change, but only where a mesh already exists.
 void Refresh()
 {
  ApplyDrawDistance();
  if (m_bBuilt) { Build(); }
 }

 bool IsBuilt()
 {
  return m_bBuilt;
 }
}

[ComponentEditorProps(category: "EXPBG/Garrison", description: "Shows the Random Garrison radius only while the editor renders the zone")]
class EXPG_RandomGarrisonVisibilityComponentClass : SCR_EditableEntityVisibilityChildComponentClass
{
}

class EXPG_RandomGarrisonVisibilityComponent : SCR_EditableEntityVisibilityChildComponent
{
 // The mesh is built before super runs, so it exists when super sets it visible.
 override void EOnStateChanged(EEditableEntityState states, EEditableEntityState changedState, bool toSet)
 {
  GenericEntity owner = m_Owner;
  int active = states & m_State;
  if (owner && active > 0)
  {
   EXPG_RandomGarrisonAreaComponent area = EXPG_RandomGarrisonAreaComponent.Cast(owner.FindComponent(EXPG_RandomGarrisonAreaComponent));
   if (area && !area.IsBuilt()) { area.Build(); }
  }
  super.EOnStateChanged(states, changedState, toSet);
 }
}
