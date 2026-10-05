// Game Master view of the crowd radius: a vanilla area mesh built only while the
// editor reports the zone as rendered. Players and a dedicated server allocate nothing.
[ComponentEditorProps(category: "EXPBG/Ambient", description: "Draws the EXPBG Civil Protest Zone radius for Game Masters")]
class EAU_ZoneAreaComponentClass : SCR_BaseAreaMeshComponentClass
{
}

class EAU_ZoneAreaComponent : SCR_BaseAreaMeshComponent
{
 static const float DRAW_DISTANCE_MARGIN = 600;
 protected bool m_bBuilt;

 // Radius is read live from the replicated setting, so a GM edit resizes the mesh on refresh.
 override float GetRadius()
 {
  EAU_ProtestZone zone = EAU_ProtestZone.Cast(GetOwner());
  if (!zone) return 0;
  return Math.Clamp(zone.RadiusMeters, EAU_ProtestZone.RADIUS_MIN, EAU_ProtestZone.RADIUS_MAX);
 }

 // Deliberately does not generate the mesh: only a rendering editor builds it.
 override void EOnInit(IEntity owner)
 {
  if (!owner || !EAU_ProtestZone.Cast(owner)) return;
  ApplyDrawDistance();
 }

 // The setter, not the prefab field: the editable stores the squared distance.
 void ApplyDrawDistance()
 {
  IEntity owner = GetOwner();
  if (!owner) return;
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(owner.FindComponent(SCR_EditableEntityComponent));
  if (editable) editable.SetMaxDrawDistance(GetRadius() + DRAW_DISTANCE_MARGIN);
 }

 void Build()
 {
  if (System.IsConsoleApp()) return;
  ApplyDrawDistance();
  GenerateAreaMesh();
  m_bBuilt = true;
 }

 // Rebuild after a radius change, but only where a mesh already exists.
 void Refresh()
 {
  ApplyDrawDistance();
  if (m_bBuilt) Build();
 }

 bool IsBuilt() { return m_bBuilt; }
}

[ComponentEditorProps(category: "EXPBG/Ambient", description: "Shows the EXPBG Civil Protest Zone radius only while the editor renders the zone")]
class EAU_ZoneVisibilityComponentClass : SCR_EditableEntityVisibilityChildComponentClass
{
}

class EAU_ZoneVisibilityComponent : SCR_EditableEntityVisibilityChildComponent
{
 // The mesh is built before super runs, so it exists when super sets it visible.
 override void EOnStateChanged(EEditableEntityState states, EEditableEntityState changedState, bool toSet)
 {
  GenericEntity owner = m_Owner;
  int active = states & m_State;
  if (owner && active > 0)
  {
   EAU_ZoneAreaComponent area = EAU_ZoneAreaComponent.Cast(owner.FindComponent(EAU_ZoneAreaComponent));
   if (area && !area.IsBuilt()) area.Build();
  }
  super.EOnStateChanged(states, changedState, toSet);
 }
}
