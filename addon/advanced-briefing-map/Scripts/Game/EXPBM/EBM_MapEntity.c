// Lets a briefing board borrow the game's own map renderer (roads, buildings, names,
// contours, grid) while no map of the player is open. Only the drawing setup of a map
// config is applied (layers, map properties, descriptor visibility and defaults): no
// modules, UI components, OpenMap/CloseMap events or camera changes. The native map has
// one zoom, pan, layer set and icon mapping, so the next map the player opens gets its
// own view back before vanilla continues (vanilla does not reset them on reopening).
modded class SCR_MapEntity
{
 protected bool m_bEBM_NativeDirty;

 // Drawing setup from a map config (normally the scenario's gadget map config); parts the
 // config lacks fall back to the vanilla defaults, as in SetupMapConfig.
 MapConfiguration EBM_CreateBoardConfig(ResourceName configPath, Widget rootWidget)
 {
  SCR_MapConfig drawing = new SCR_MapConfig();
  Resource resource;
  if (!configPath.IsEmpty())
   resource = BaseContainerTools.LoadContainer(configPath);
  if (resource && resource.IsValid())
  {
   BaseContainer source = resource.GetResource().ToBaseContainer();
   if (source)
   {
    BaseContainer part = source.GetObject("m_LayersConfig");
    if (part)
     drawing.m_LayersConfig = SCR_MapLayersBase.Cast(BaseContainerTools.CreateInstanceFromContainer(part));
    part = source.GetObject("m_MapPropsConfig");
    if (part)
     drawing.m_MapPropsConfig = SCR_MapPropsBase.Cast(BaseContainerTools.CreateInstanceFromContainer(part));
    part = source.GetObject("m_DescriptorVisibilityConfig");
    if (part)
     drawing.m_DescriptorVisibilityConfig = SCR_MapDescriptorVisibilityBase.Cast(BaseContainerTools.CreateInstanceFromContainer(part));
    part = source.GetObject("m_DescriptorDefaultsConfig");
    if (part)
     drawing.m_DescriptorDefaultsConfig = SCR_MapDescriptorDefaults.Cast(BaseContainerTools.CreateInstanceFromContainer(part));
   }
  }
  MapConfiguration configObject = new MapConfiguration();
  configObject.RootWidgetRef = rootWidget;
  configObject.MapEntityMode = EMapEntityMode.PLAIN;
  SetupLayersAndProps(configObject, drawing);
  if (!configObject.LayerConfig || !configObject.MapPropsConfig || !configObject.DescriptorVisibilityConfig || !configObject.DescriptorDefsConfig)
   return null;
  return configObject;
 }

 // Applies the board's drawing setup to the native layers (vanilla InitLayers).
 void EBM_InitBoardLayers(notnull MapConfiguration config)
 {
  EBM_ApplyDescriptorTypes(config);
  InitLayers(config);
  m_bEBM_NativeDirty = true;
 }

 // A board moved the native zoom, pan or layer.
 void EBM_MarkNativeDirty()
 {
  m_bEBM_NativeDirty = true;
 }

 protected void EBM_ApplyDescriptorTypes(MapConfiguration config)
 {
  if (!config || !config.DescriptorDefsConfig || !config.DescriptorDefsConfig.m_aDescriptorDefaults)
   return;
  SetupDescriptorTypes(config.DescriptorDefsConfig);
 }

 // Runs once the opening map's widget has its size (vanilla FRAME_DELAY), before vanilla
 // continues: puts back this map's icon mapping, zoom and pan if a board changed them.
 // Layers are re-initialised by OpenMap and assigned by vanilla OnMapOpen.
 override protected void OnMapOpen(MapConfiguration config)
 {
  if (m_bEBM_NativeDirty && m_MapWidget && m_Workspace)
  {
   m_bEBM_NativeDirty = false;
   EBM_ApplyDescriptorTypes(config);
   m_MapWidget.SetSizeInUnits(Vector(m_iMapSizeX, m_iMapSizeY, 0));
   float pixelPerUnit = m_MapWidget.PixelPerUnit();
   if (pixelPerUnit > 0)
    ZoomChange(m_fZoomPPU / pixelPerUnit);
   PosChange(m_Workspace.DPIScale(m_iPanX), m_Workspace.DPIScale(m_iPanY));
  }
  super.OnMapOpen(config);
 }
}
