[EntityEditorProps(category: "EXPBG/Ambient Sounds/Sound Effects", description: "Physical emergency-alert television")]
class EAS_TVModuleClass : EAS_RadioModuleClass {}

class EAS_TVModule : EAS_RadioModule
{
 override int AudioKind() { return 2; }
 override string AudioEvent(int recording) { return EAS_TVBank.Event(recording); }
 override float AudioDuration(int recording) { return EAS_TVBank.Duration(recording); }
 override int ResolveRecording(int selection, int previous) { return EAS_TVBank.Resolve(selection, previous); }
 override bool ValidSetting(int key, float value)
 {
  if (!EAS_RadioState.ValidValue(key, value)) return false;
  if (key == 0) return value == 0;
  if (key == 4) return value <= 600;
  if (key == 5) return value == 30;
  return (key >= 1 && key <= 3) || key == 6;
 }
}

class EAS_TVModuleSerializer : EAS_RadioModuleSerializer
{
 override static typename GetTargetType() { return EAS_TVModule; }
}
