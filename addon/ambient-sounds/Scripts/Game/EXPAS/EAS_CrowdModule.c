[EntityEditorProps(category: "EXPBG/Ambient Sounds/Crowd", description: "Local crowd recordings")]
class EAS_CrowdModuleClass : EAS_RadioModuleClass {}

// Shares admission, proximity, finite playback and teardown with physical radios.
class EAS_CrowdModule : EAS_RadioModule
{
 override int AudioKind() { return 1; }
 override string AudioEvent(int recording) { return EAS_CrowdBank.Event(recording, Range); }
 override float AudioDuration(int recording) { return EAS_CrowdBank.Duration(recording); }
 override int ResolveRecording(int selection, int previous) { return EAS_CrowdBank.Resolve(selection, previous); }
 override bool ValidSetting(int key, float value)
 {
  if (!EAS_RadioState.ValidValue(key, value)) return false;
  // Recordings 0-4, Random crowd 100 and the angry/rioting alternation 101.
  if (key == 0) return EAS_CrowdBank.ValidSelection(value);
  if (key == 4) return value == 1;
  if (key == 5) return value >= 10 && value <= 150 && Math.Floor(value / 10) * 10 == value;
  return (key >= 1 && key <= 3) || key == 6;
 }
}

class EAS_CrowdModuleSerializer : EAS_RadioModuleSerializer
{
 override static typename GetTargetType() { return EAS_CrowdModule; }
}
