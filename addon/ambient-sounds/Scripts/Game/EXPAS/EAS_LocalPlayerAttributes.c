// Separate native attribute types keep controls scoped to the selected family.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdAttribute : EAS_RadioAttribute
{
 override bool SupportsModule(EAS_RadioModule module) { return module && module.AudioKind() == 1; }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdRecordingAttribute : EAS_CrowdAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdVolumeAttribute : EAS_CrowdAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdEnabledAttribute : EAS_CrowdAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdLoopAttribute : EAS_CrowdAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdRangeAttribute : EAS_CrowdAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_CrowdDebugAttribute : EAS_CrowdAttribute {}

[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_TVAttribute : EAS_RadioAttribute
{
 override bool SupportsModule(EAS_RadioModule module) { return module && module.AudioKind() == 2; }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_TVVolumeAttribute : EAS_TVAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_TVEnabledAttribute : EAS_TVAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_TVLoopAttribute : EAS_TVAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_TVPauseAttribute : EAS_TVAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_TVDebugAttribute : EAS_TVAttribute {}
