// Separate native attribute types keep sound controls scoped to placed sound sources.
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundAttribute : EAS_RadioAttribute
{
 override bool SupportsModule(EAS_RadioModule module) { return module && module.AudioKind() == 3; }
 // Audible distance is a fixed list of graph ranges, exchanged like recordings.
 override protected bool UsesValueList() { return m_Key == 0 || m_Key == 5; }
}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundRecordingAttribute : EAS_SoundAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundVolumeAttribute : EAS_SoundAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundEnabledAttribute : EAS_SoundAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundLoopAttribute : EAS_SoundAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundPauseAttribute : EAS_SoundAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundRangeAttribute : EAS_SoundAttribute {}
[BaseContainerProps(), SCR_BaseEditorAttributeCustomTitle()]
class EAS_SoundDebugAttribute : EAS_SoundAttribute {}
