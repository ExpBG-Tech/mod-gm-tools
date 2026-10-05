[EntityEditorProps(category: "EXPBG/Ambient Sounds/Sounds", description: "Placed ambient sound source")]
class EAS_SoundModuleClass : EAS_RadioModuleClass {}

// Invisible finite source. Shares admission, proximity activation, the 4-voice
// playback budget, replication, session saves and teardown with physical radios.
class EAS_SoundModule : EAS_RadioModule
{
 override int AudioKind() { return 3; }
 override ResourceName AudioProject() { return EAS_SoundBank.PROJECT; }
 override string AudioEvent(int recording) { return EAS_SoundBank.Event(recording, Range); }
 override float AudioDuration(int recording) { return EAS_SoundBank.Duration(recording); }
 // Long-range sources must activate before a listener reaches their audible edge.
 override float ActivationRadius() { return Math.Max(EAS_Activation.RADIO_RADIUS, Range + 50); }
 override int ResolveRecording(int selection, int previous) { return EAS_SoundBank.Resolve(selection, previous); }
 override bool ValidSetting(int key, float value)
 {
  if (!EAS_RadioState.ValidValue(key, value)) return false;
  if (key == 0) return EAS_SoundBank.ValidSelection(value);
  if (key == 5) return EAS_SoundBank.ValidRange(value);
  return (key >= 1 && key <= 4) || key == 6;
 }
}

class EAS_SoundModuleSerializer : EAS_RadioModuleSerializer
{
 override static typename GetTargetType() { return EAS_SoundModule; }
}
