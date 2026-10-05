// Exclusion-zone radius visualization. Two layers, both independent of DebugDraw:
//  - Game Master: a vanilla area mesh built only while the editor reports RENDERED.
//  - World Editor: a Shape ring drawn inside #ifdef WORKBENCH, compiled out of the runtime.
// Neither layer reads or alters EAC_ExclusionZone.IsPopulationAllowed / IsTransitAllowed.
// A dedicated server constructs both components, clears EntityFlags.VISIBLE once and stops:
// EntityEvent.FRAME is never set and GenerateAreaMesh is unreachable.

[ComponentEditorProps(category: "EXPBG/Ambient", description: "Draws the no-go radius of an EXPBG exclusion zone for Game Masters and in the World Editor")]
class EAC_ExclusionAreaComponentClass : SCR_BaseAreaMeshComponentClass
{
}

class EAC_ExclusionAreaComponent : SCR_BaseAreaMeshComponent
{
 // Orange matches EAC_DebugKind.NO_GO in EAC_DebugView; red marks additional transit blocking.
 static const int RING_COLOUR_POPULATION = 0xFFFF9900;
 static const int RING_COLOUR_TRANSIT = 0xFFFF4400;
 static const float DRAW_DISTANCE_MARGIN = 600;
 static const float RING_BAND_OFFSET = 20;

 protected bool m_bBuilt;

 //------------------------------------------------------------------------------------------------
 // Radius is read live from the replicated attribute, so a GM edit resizes the mesh on refresh.
 override float GetRadius()
 {
  IEntity owner = GetOwner();
  if (!owner)
  {
   return 0;
  }
  EAC_ExclusionZone zone = EAC_ExclusionZone.Cast(owner);
  if (!zone)
  {
   return 0;
  }
  return Math.Clamp(zone.RadiusMeters, 5, 5000);
 }

 //------------------------------------------------------------------------------------------------
 // Deliberately does NOT generate the mesh. Vanilla SCR_TriggerAreaMeshComponent generates here
 // because a trigger is authored per world; this zone replicates to every client, so a plain
 // player and a dedicated server must allocate nothing until an editor actually renders it.
 override void EOnInit(IEntity owner)
 {
  if (!owner)
  {
   return;
  }
  if (!owner.IsInherited(EAC_ExclusionZone))
  {
   PrintFormat("[EAC] EAC_ExclusionAreaComponent must be attached to EAC_ExclusionZone; radius visualization disabled.", level: LogLevel.ERROR);
   return;
  }
  ApplyDrawDistance();
 }

 //------------------------------------------------------------------------------------------------
 // Must be the setter, never the prefab attribute: SCR_EditableEntityComponent.SetMaxDrawDistance
 // stores the square of the value while the authored field is read raw, so an authored 2000 would
 // be interpreted as 2000 m^2 (about 45 m).
 void ApplyDrawDistance()
 {
  IEntity owner = GetOwner();
  if (!owner)
  {
   return;
  }
  SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(owner.FindComponent(SCR_EditableEntityComponent));
  if (!editable)
  {
   return;
  }
  float radius = GetRadius();
  editable.SetMaxDrawDistance(radius + DRAW_DISTANCE_MARGIN);
 }

 //------------------------------------------------------------------------------------------------
 // The only path that reaches GenerateAreaMesh. IsConsoleApp is the second hard stop for a
 // dedicated server; the RENDERED gate on EAC_ExclusionVisibilityComponent is the first.
 void Build()
 {
  if (System.IsConsoleApp())
  {
   return;
  }
  ApplyDrawDistance();
  GenerateAreaMesh();
  m_bBuilt = true;
 }

 //------------------------------------------------------------------------------------------------
 // Rebuild after a radius change, but only where a mesh already exists.
 void Refresh()
 {
  // A replicated or restored larger radius must extend visibility even before
  // the first GM render; otherwise the old draw distance can hide the new zone.
  ApplyDrawDistance();
  if (m_bBuilt)
  {
   Build();
  }
 }

 //------------------------------------------------------------------------------------------------
 bool IsBuilt()
 {
  return m_bBuilt;
 }

#ifdef WORKBENCH
 //------------------------------------------------------------------------------------------------
 // CALL_WHEN_ENTITY_VISIBLE: the entity must be visible in frustum, selected or named, so an
 // off-screen zone costs nothing. Same pattern as vanilla SCR_SpawnPositionComponent.
 override int _WB_GetAfterWorldUpdateSpecs(IEntity owner, IEntitySource src)
 {
  return EEntityFrameUpdateSpecs.CALL_WHEN_ENTITY_VISIBLE;
 }

 //------------------------------------------------------------------------------------------------
 // Three stacked circles so the band stays readable across rolling terrain. ShapeFlags.ONCE
 // destroys each shape after the frame, so no pointer is retained. Reading RadiusMeters every
 // call means the ring resizes the same frame a designer edits the attribute.
 override event void _WB_AfterWorldUpdate(IEntity owner, float timeSlice)
 {
  if (!owner)
  {
   return;
  }
  if (GetGame() && GetGame().InPlayMode())
  {
   return;
  }
  EAC_ExclusionZone zone = EAC_ExclusionZone.Cast(owner);
  if (!zone)
  {
   return;
  }
  float radius = Math.Clamp(zone.RadiusMeters, 5, 5000);
  int colour = RING_COLOUR_POPULATION;
  if (zone.BlockTransit == 1)
  {
   colour = RING_COLOUR_TRANSIT;
  }
  int slices = Math.Round(Math.Clamp(radius * 0.5, 24, 128));
  int flags = ShapeFlags.ONCE | ShapeFlags.NOZBUFFER | ShapeFlags.NOOUTLINE;
  vector origin = owner.GetOrigin();
  vector lower = origin;
  vector upper = origin;
  lower[1] = origin[1] - RING_BAND_OFFSET;
  upper[1] = origin[1] + RING_BAND_OFFSET;
  Shape.CreateCircle(colour, flags, origin, radius, slices, 1);
  Shape.CreateCircle(colour, flags, lower, radius, slices, 1);
  Shape.CreateCircle(colour, flags, upper, radius, slices, 1);
 }
#endif
}

[ComponentEditorProps(category: "EXPBG/Ambient", description: "Shows the EXPBG exclusion radius mesh only while the editor reports the zone as rendered")]
class EAC_ExclusionVisibilityComponentClass : SCR_EditableEntityVisibilityChildComponentClass
{
}

class EAC_ExclusionVisibilityComponent : SCR_EditableEntityVisibilityChildComponent
{
 //------------------------------------------------------------------------------------------------
 // SCR_EditableEntityBaseChildComponent.UpdateFromCurrentState starts its parent walk at m_Owner
 // itself, so this component works on the same entity that carries SCR_EditableEntityComponent.
 // The mesh is built BEFORE super runs, so it exists when super sets EntityFlags.VISIBLE.
 override void EOnStateChanged(EEditableEntityState states, EEditableEntityState changedState, bool toSet)
 {
  GenericEntity owner = m_Owner;
  if (owner)
  {
   int active = states & m_State;
   if (active > 0)
   {
    EAC_ExclusionAreaComponent area = EAC_ExclusionAreaComponent.Cast(owner.FindComponent(EAC_ExclusionAreaComponent));
    if (area)
    {
     if (!area.IsBuilt())
     {
      area.Build();
     }
    }
   }
  }
  super.EOnStateChanged(states, changedState, toSet);
 }
}
