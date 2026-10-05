// Shared authority snapshot, not a new entity or recurring service. Both bounded
// runtimes reuse it; remote cameras use vanilla editor movement replication.
class EAS_Activation
{
 static const float AMBIENT_RADIUS = 3000;
 static const float RADIO_RADIUS = 1000;
 protected static ref EAS_Activation s_Instance;
 protected BaseWorld m_World;
 protected float m_CollectedAt = -100000;
 protected ref array<vector> m_Positions = {};

 static EAS_Activation Get(float now)
 {
  if (!GetGame() || !GetGame().GetWorld()) { s_Instance = null; return null; }
  BaseWorld world = GetGame().GetWorld();
  if (!s_Instance || s_Instance.m_World != world)
  {
   s_Instance = new EAS_Activation();
   s_Instance.m_World = world;
  }
  s_Instance.Update(now);
  return s_Instance;
 }

 static void ForgetWorld(BaseWorld world)
 {
  if (s_Instance && s_Instance.m_World == world) s_Instance = null;
 }

 static bool CameraMode(EEditorMode mode) { return mode == EEditorMode.EDIT || mode == EEditorMode.SPECTATE; }

 void Update(float now)
 {
  if (now >= m_CollectedAt && now < m_CollectedAt + 2) return;
  m_CollectedAt = now;
  m_Positions.Clear();
  Collect();
 }

 protected void Collect()
 {
  PlayerManager players = GetGame().GetPlayerManager();
  SCR_EditorManagerCore editors = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
  array<int> ids = {};
  if (players) players.GetPlayers(ids);
  foreach (int id : ids)
  {
   IEntity controlled = players.GetPlayerControlledEntity(id);
   if (controlled) m_Positions.Insert(controlled.GetOrigin());
   SCR_EditorManagerEntity editor;
   if (editors) editor = editors.GetEditorManager(id);
   if (editor && editor.IsOpened() && CameraMode(editor.GetCurrentMode())) m_Positions.Insert(editor.GetOrigin());
  }
  // Called by module authority only. A standalone/listen-host free camera may
  // have no controlled character or registered player; dedicated has no camera.
  if (!System.IsConsoleApp())
  {
   CameraManager cameras = GetGame().GetCameraManager();
   CameraBase camera;
   if (cameras) camera = cameras.CurrentCamera();
   if (camera) m_Positions.Insert(camera.GetOrigin());
  }
 }

 bool Near(vector centre, float radius)
 {
  foreach (vector position : m_Positions)
   if (vector.DistanceSq(centre, position) <= radius * radius) return true;
  return false;
 }

 static bool LocalNear(vector centre, float radius)
 {
  if (System.IsConsoleApp()) return false;
  float distance = AudioSystem.GetDistance(centre);
  return distance >= 0 && distance <= radius;
 }
}
