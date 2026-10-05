// EXPBG GM Tools end-to-end audio sweep. TEST ONLY: never copy into addon/** or publish.
//
// Runs only in a windowed diagnostic client started with -expe2eAudioSweep (tests/e2e/Run-AudioSweep.ps1).
// A dedicated server is refused: it has no audio listener and the pack's runtimes never call PlayEvent
// there (System.IsConsoleApp()). Once the world has a camera and a settle delay has passed, every selected
// EXPBG Ambient Sounds audio event is played once at the listener (current camera) with EAS_Gain = 0.5:
//   [EXPE2E AUDIO BEGIN] seq=<k> bank=<bank> id=<n> event=<name> range=<r|none> resolved=<n>
//   [EAS DIAG] action=play ... runtime=e2e-sweep seq=<k> ... event=<name> handle=<h> ... duration=<s>
//   [EXPE2E AUDIO] bank=<bank> id=<n> event=<name> range=<r|none> result=PASS|FAIL reason=<why> seq=<k> ... handle=<h>
// then [EXPE2E AUDIO RESULT] total=N pass=P fail=F and GetGame().RequestClose().
// The [EAS DIAG] line is written through the pack's own EAS_Diagnostics (needs -easDiagnostics 1).
//
// Event names and audio projects come from the pack's public banks, built exactly as the runtimes do:
//   war   EAS_Runtime.Play         EAS_Bank.PROJECT       clip.EventName + "_R" + range (EAS_ContentSettings key 3)
//   radio EAS_RadioModule          EAS_RadioBank.PROJECT  EAS_RadioBank.Event(EAS_RadioBank.Resolve(selection))
//   crowd EAS_CrowdModule          EAS_CrowdBank.PROJECT  EAS_CrowdBank.Event(EAS_CrowdBank.Resolve(selection), Range)
//   tv    EAS_TVModule             EAS_TVBank.PROJECT     EAS_TVBank.Event(recording)
//   sound EAS_SoundModule          EAS_SoundBank.PROJECT  EAS_SoundBank.Event(recording, Range)
// Random selections (radio 100-103, crowd 100) are resolved by the bank's own Resolve() at play time.
// Modes (-expe2eAudioMode):
//   quick     every recording at its authored default range, plus every range of one recording per
//             range bank, so every sample file and every graph range branch is played once.
//   standard  quick for the war bank; every recording at every range for crowd and sound (default).
//   full      every recording at every valid range in every bank.
// Optional: -expe2eAudioDelay <s> (default 20), -expe2eAudioHold <ms> (default 1500),
// -expe2eAudioBanks war,radio,crowd,tv,sound (subset), -expe2eAudioKeepOpen (no RequestClose).
//
// PASS means the native event started (valid handle) and, after the hold time, was still playing or
// had finished no earlier than its recorded duration allows. It does not measure loudness.

class EXPE2E_AudioCase
{
	string Bank;
	int Id;
	string EventName;
	int Range; // -1: the event has no range suffix
	ResourceName Project;
	float Duration;
	bool DefaultRange;
	bool Group; // random selection, resolved by the bank at play time
	int Resolved;

	void EXPE2E_AudioCase(string bank, int id, string eventName, int range, ResourceName project, float duration, bool defaultRange, bool group)
	{
		Bank = bank;
		Id = id;
		EventName = eventName;
		Range = range;
		Project = project;
		Duration = duration;
		DefaultRange = defaultRange;
		Group = group;
		Resolved = id;
		if (group)
			Resolved = -1;
	}
}

enum EXPE2E_EAudioPhase
{
	WAIT_WORLD,
	SETTLE,
	NEXT,
	PROBE,
	CHECK,
	GAP,
	DONE
}

class EXPE2E_AudioSweep
{
	// Authored defaults: E_EXPBG_AmbientSounds.et and the war attribute use 1500 m, E_EXPBG_Crowd.et 40 m.
	static const int WAR_DEFAULT_RANGE = 1500;
	static const int CROWD_DEFAULT_RANGE = 40;
	// One recording per range bank receives every range variant in quick mode.
	static const int WAR_SWEEP_CLIP = 0; // SOUND_EAS_CZ_SHORT
	static const int CROWD_SWEEP_RECORDING = 0; // SOUND_EAS_CROWD_ANGRY
	static const int SOUND_SWEEP_RECORDING = 27; // SOUND_EAS_VINNY_DISTANT_SHEEP
	static const int MAX_RECORDING = 100;
	static const int GROUP_FIRST = 100;
	static const int GROUP_LAST = 109;
	static const int GROUP_DRAWS = 64;
	static const float GAIN = 0.5;
	static const int TICK_MS = 50;
	static const int PROBE_MS = 250;
	static const int GAP_MS = 300;
	static const int READY_TIMEOUT_MS = 180000;

	protected static ref EXPE2E_AudioSweep s_Instance;

	protected ref array<ref EXPE2E_AudioCase> m_Cases = {};
	protected EXPE2E_EAudioPhase m_Phase = EXPE2E_EAudioPhase.WAIT_WORLD;
	protected BaseWorld m_World;
	protected int m_Booted;
	protected int m_PhaseStarted;
	protected int m_PlayedAt;
	protected int m_Index = -1;
	protected AudioHandle m_Handle = AudioHandle.Invalid;
	protected AudioHandle m_LastHandle = AudioHandle.Invalid;
	protected string m_Early = "na";
	protected float m_Audible = -1;
	protected float m_Distance = -1;
	protected int m_Pass;
	protected int m_Fail;
	protected int m_DelayMs = 20000;
	protected int m_HoldMs = 1500;
	protected string m_Mode = "standard";
	protected bool m_KeepOpen;
	protected string m_Banks;

	//------------------------------------------------------------------------------------------------
	static void Start()
	{
		if (s_Instance || !GetGame() || System.IsConsoleApp() || !System.IsCLIParam("expe2eAudioSweep"))
			return;
#ifdef WORKBENCH
		if (!GetGame().InPlayMode())
			return;
#endif
		s_Instance = new EXPE2E_AudioSweep();
		s_Instance.Begin();
	}

	//------------------------------------------------------------------------------------------------
	void Begin()
	{
		m_Booted = System.GetTickCount();
		m_PhaseStarted = m_Booted;
		ReadOptions();
		BuildInventory();
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadOptions()
	{
		string value;
		if (System.GetCLIParam("expe2eAudioMode", value) && (value == "quick" || value == "standard" || value == "full"))
			m_Mode = value;

		value = "";
		if (System.GetCLIParam("expe2eAudioDelay", value) && !value.IsEmpty())
		{
			int delay = value.ToInt();
			if (delay >= 0 && delay <= 600)
				m_DelayMs = delay * 1000;
		}

		value = "";
		if (System.GetCLIParam("expe2eAudioHold", value) && !value.IsEmpty())
		{
			int hold = value.ToInt();
			if (hold >= 500 && hold <= 10000)
				m_HoldMs = hold;
		}

		value = "";
		if (System.GetCLIParam("expe2eAudioBanks", value))
			m_Banks = value;

		m_KeepOpen = System.IsCLIParam("expe2eAudioKeepOpen");
	}

	//------------------------------------------------------------------------------------------------
	protected void BuildInventory()
	{
		int war = AddWar();
		int radio = AddRadio();
		int crowd = AddCrowd();
		int tv = AddTV();
		int sound = AddSound();
		string banks = m_Banks;
		if (banks.IsEmpty())
			banks = "all";

		PrintFormat("[EXPE2E AUDIO INVENTORY] mode=%1 total=%2 war=%3 radio=%4 crowd=%5 tv=%6 sound=%7 banks=%8", m_Mode, m_Cases.Count(), war, radio, crowd, tv, sound, banks);
		PrintFormat("[EXPE2E AUDIO CONFIG] mode=%1 delayMs=%2 holdMs=%3 probeMs=%4 gapMs=%5 gain=%6 keepOpen=%7 easDiag=%8", m_Mode, m_DelayMs, m_HoldMs, PROBE_MS, GAP_MS, GAIN, Flag(m_KeepOpen), Flag(EAS_Diagnostics.Enabled()));

		// The pack's own random selections: every draw must resolve to a playable recording.
		for (int radioGroup = GROUP_FIRST; radioGroup <= GROUP_LAST; radioGroup++)
		{
			if (BankSelected("radio") && EAS_RadioBank.Event(radioGroup).IsEmpty() && EAS_RadioBank.ValidSelection(radioGroup))
				LogGroupDraws("radio", radioGroup);
		}

		for (int crowdGroup = GROUP_FIRST; crowdGroup <= GROUP_LAST; crowdGroup++)
		{
			if (BankSelected("crowd") && EAS_CrowdBank.Event(crowdGroup, CROWD_DEFAULT_RANGE).IsEmpty() && EAS_CrowdBank.ValidSelection(crowdGroup))
				LogGroupDraws("crowd", crowdGroup);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool AllRanges(string bank)
	{
		if (m_Mode == "full")
			return true;

		if (m_Mode == "standard")
			return bank == "crowd" || bank == "sound";

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected int AddWar()
	{
		if (!BankSelected("war"))
			return 0;

		int before = m_Cases.Count();
		bool allRanges = AllRanges("war");
		array<int> ranges = {};
		for (int candidate = 0; candidate <= 3000; candidate += 50)
		{
			// Same validator as the war module's "Audible distance" attribute (key 3).
			if (EAS_ContentSettings.ValidValue(3, candidate))
				ranges.Insert(candidate);
		}

		int defaultRange = WAR_DEFAULT_RANGE;
		if (!ranges.Contains(defaultRange) && !ranges.IsEmpty())
			defaultRange = ranges[0];

		array<ref EAS_Clip> clips = EAS_Runtime.Bank();
		foreach (int index, EAS_Clip clip : clips)
		{
			foreach (int range : ranges)
			{
				bool isDefault = range == defaultRange;
				if (allRanges || isDefault || index == WAR_SWEEP_CLIP)
					AddCase("war", index, clip.EventName + "_R" + range.ToString(), range, EAS_Bank.PROJECT, clip.Duration, isDefault, false);
			}
		}

		return m_Cases.Count() - before;
	}

	//------------------------------------------------------------------------------------------------
	protected int AddRadio()
	{
		if (!BankSelected("radio"))
			return 0;

		int before = m_Cases.Count();
		for (int recording = 0; recording < MAX_RECORDING; recording++)
		{
			string eventName = EAS_RadioBank.Event(recording);
			if (!eventName.IsEmpty())
				AddCase("radio", recording, eventName, -1, EAS_RadioBank.PROJECT, EAS_RadioBank.Duration(recording), true, false);
		}

		for (int selection = GROUP_FIRST; selection <= GROUP_LAST; selection++)
		{
			if (EAS_RadioBank.Event(selection).IsEmpty() && EAS_RadioBank.ValidSelection(selection))
				AddCase("radio", selection, "", -1, EAS_RadioBank.PROJECT, 0, true, true);
		}

		return m_Cases.Count() - before;
	}

	//------------------------------------------------------------------------------------------------
	protected int AddCrowd()
	{
		if (!BankSelected("crowd"))
			return 0;

		int before = m_Cases.Count();
		bool allRanges = AllRanges("crowd");
		array<int> ranges = {};
		for (int candidate = 0; candidate <= 3000; candidate += 10)
		{
			if (!EAS_CrowdBank.Event(CROWD_SWEEP_RECORDING, candidate).IsEmpty())
				ranges.Insert(candidate);
		}

		int defaultRange = CROWD_DEFAULT_RANGE;
		if (!ranges.Contains(defaultRange) && !ranges.IsEmpty())
			defaultRange = ranges[0];

		for (int recording = 0; recording < MAX_RECORDING; recording++)
		{
			if (EAS_CrowdBank.Event(recording, defaultRange).IsEmpty())
				continue;

			foreach (int range : ranges)
			{
				bool isDefault = range == defaultRange;
				if (allRanges || isDefault || recording == CROWD_SWEEP_RECORDING)
					AddCase("crowd", recording, EAS_CrowdBank.Event(recording, range), range, EAS_CrowdBank.PROJECT, EAS_CrowdBank.Duration(recording), isDefault, false);
			}
		}

		for (int selection = GROUP_FIRST; selection <= GROUP_LAST; selection++)
		{
			if (EAS_CrowdBank.Event(selection, defaultRange).IsEmpty() && EAS_CrowdBank.ValidSelection(selection))
				AddCase("crowd", selection, "", defaultRange, EAS_CrowdBank.PROJECT, 0, true, true);
		}

		return m_Cases.Count() - before;
	}

	//------------------------------------------------------------------------------------------------
	protected int AddTV()
	{
		if (!BankSelected("tv"))
			return 0;

		int before = m_Cases.Count();
		for (int recording = 0; recording < MAX_RECORDING; recording++)
		{
			string eventName = EAS_TVBank.Event(recording);
			if (!eventName.IsEmpty())
				AddCase("tv", recording, eventName, -1, EAS_TVBank.PROJECT, EAS_TVBank.Duration(recording), true, false);
		}

		return m_Cases.Count() - before;
	}

	//------------------------------------------------------------------------------------------------
	protected int AddSound()
	{
		if (!BankSelected("sound"))
			return 0;

		int before = m_Cases.Count();
		bool allRanges = AllRanges("sound");
		array<int> ranges = {};
		for (int candidate = 0; candidate <= 3000; candidate += 10)
		{
			if (EAS_SoundBank.ValidRange(candidate))
				ranges.Insert(candidate);
		}

		for (int recording = 0; recording < MAX_RECORDING; recording++)
		{
			if (EAS_SoundBank.Event(recording).IsEmpty())
				continue;

			int defaultRange = SoundDefaultRange(recording);
			if (!EAS_SoundBank.ValidRange(defaultRange))
				defaultRange = 50;

			foreach (int range : ranges)
			{
				bool isDefault = range == defaultRange;
				if (allRanges || isDefault || recording == SOUND_SWEEP_RECORDING)
					AddCase("sound", recording, EAS_SoundBank.Event(recording, range), range, EAS_SoundBank.PROJECT, EAS_SoundBank.Duration(recording), isDefault, false);
			}
		}

		return m_Cases.Count() - before;
	}

	//------------------------------------------------------------------------------------------------
	//! Audible distance authored on PrefabsEditable/EXPBG/Sounds/E_EXPBG_Sound_*.et, by recording.
	protected static int SoundDefaultRange(int recording)
	{
		switch (recording)
		{
			case 0: return 50; // RadioStatic
			case 1: return 30; // Apache1
			case 2: return 30; // Apache2
			case 3: return 50; // Russian1
			case 4: return 50; // Russian2
			case 5: return 50; // Russian3
			case 6: return 50; // Russian4
			case 7: return 50; // Chinese1
			case 8: return 30; // ArabChatter
			case 9: return 30; // Arab1
			case 10: return 500; // HanoiHannah
			case 11: return 600; // CloseFirefight
			case 12: return 1500; // DistantFirefight
			case 13: return 1500; // DistantShelling
			case 14: return 1000; // JetFlyby1
			case 15: return 1000; // JetFlyby2
			case 16: return 300; // DroneOverhead
			case 17: return 250; // Market
			case 18: return 250; // MarketSeller
			case 19: return 250; // StreetSinging
			case 20: return 1000; // MuslimPrayer
			case 21: return 1000; // TrafficMedium
			case 22: return 500; // ChurchBell
			case 23: return 150; // CarAlarm
			case 24: return 300; // PoliceCar
			case 25: return 40; // NokiaRingtone
			case 26: return 150; // DistantBarking
			case 27: return 200; // DistantSheep
		}
		return 50;
	}

	//------------------------------------------------------------------------------------------------
	protected void AddCase(string bank, int id, string eventName, int range, ResourceName project, float duration, bool defaultRange, bool group)
	{
		m_Cases.Insert(new EXPE2E_AudioCase(bank, id, eventName, range, project, duration, defaultRange, group));
	}

	//------------------------------------------------------------------------------------------------
	protected bool BankSelected(string bank)
	{
		if (m_Banks.IsEmpty())
			return true;

		string padded = "," + m_Banks + ",";
		return padded.Contains("," + bank + ",");
	}

	//------------------------------------------------------------------------------------------------
	//! The bank's own random pick for a group selection; -1 when it does not resolve.
	protected static int ResolveSelection(string bank, int selection)
	{
		if (bank == "radio")
			return EAS_RadioBank.Resolve(selection, -1);

		if (bank == "crowd")
			return EAS_CrowdBank.Resolve(selection, -1);

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	protected static string GroupEvent(string bank, int recording)
	{
		if (recording < 0)
			return "";

		if (bank == "radio")
			return EAS_RadioBank.Event(recording);

		if (bank == "crowd")
			return EAS_CrowdBank.Event(recording, CROWD_DEFAULT_RANGE);

		return "";
	}

	//------------------------------------------------------------------------------------------------
	protected static float GroupDuration(string bank, int recording)
	{
		if (bank == "radio")
			return EAS_RadioBank.Duration(recording);

		if (bank == "crowd")
			return EAS_CrowdBank.Duration(recording);

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void LogGroupDraws(string bank, int selection)
	{
		array<int> distinct = {};
		int invalid;
		for (int draw = 0; draw < GROUP_DRAWS; draw++)
		{
			int pick = ResolveSelection(bank, selection);
			string pickedEvent = GroupEvent(bank, pick);
			if (pick < 0 || pickedEvent.IsEmpty())
				invalid++;
			else if (!distinct.Contains(pick))
				distinct.Insert(pick);
		}

		// Order is irrelevant: the runner compares the drawn set with the bank's candidates.
		string list;
		foreach (int value : distinct)
		{
			if (!list.IsEmpty())
				list += "+";

			list += value.ToString();
		}

		if (list.IsEmpty())
			list = "none";

		PrintFormat("[EXPE2E AUDIO GROUP] bank=%1 selection=%2 draws=%3 invalid=%4 resolved=%5", bank, selection, GROUP_DRAWS, invalid, list);
	}

	//------------------------------------------------------------------------------------------------
	protected void ResolveGroup(EXPE2E_AudioCase audioCase)
	{
		int resolved = ResolveSelection(audioCase.Bank, audioCase.Id);
		audioCase.Resolved = resolved;
		audioCase.EventName = GroupEvent(audioCase.Bank, resolved);
		audioCase.Duration = 0;
		if (resolved >= 0)
			audioCase.Duration = GroupDuration(audioCase.Bank, resolved);
	}

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		if (!GetGame())
			return;

		BaseWorld world = GetGame().GetWorld();
		if (m_World && world != m_World)
		{
			Abort("world-changed");
			return;
		}

		if (!world)
			return;

		if (!m_World)
			m_World = world;

		int now = System.GetTickCount();
		if (m_Phase == EXPE2E_EAudioPhase.WAIT_WORLD)
			TickWaitWorld(now);
		else if (m_Phase == EXPE2E_EAudioPhase.SETTLE)
			TickSettle(now);
		else if (m_Phase == EXPE2E_EAudioPhase.NEXT)
			TickNext(now);
		else if (m_Phase == EXPE2E_EAudioPhase.PROBE)
			TickProbe(now);
		else if (m_Phase == EXPE2E_EAudioPhase.CHECK)
			TickCheck(now);
		else if (m_Phase == EXPE2E_EAudioPhase.GAP)
			TickGap(now);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickWaitWorld(int now)
	{
		bool camera = HasCamera();
		int waited = now - m_Booted;
		vector position = ListenerPosition();
		float distance = AudioSystem.GetDistance(position);
		// Ready once a camera exists and the native listener sits at it (sources play at distance ~0).
		bool listenerHere = distance >= 0 && distance <= 5;
		if ((!camera || !listenerHere) && waited < READY_TIMEOUT_MS)
			return;

		PrintFormat("[EXPE2E AUDIO READY] camera=%1 listener=%2 waitedMs=%3 position=%4 listenerDistance=%5 settleMs=%6", Flag(camera), Flag(listenerHere), waited, position, distance, m_DelayMs);
		Enter(EXPE2E_EAudioPhase.SETTLE, now);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickSettle(int now)
	{
		if (now - m_PhaseStarted < m_DelayMs)
			return;

		// Native resource load of each audio project the pack ships (once the audio system is live).
		LogProject("war", EAS_Bank.PROJECT);
		LogProject("radio", EAS_RadioBank.PROJECT);
		LogProject("crowd", EAS_CrowdBank.PROJECT);
		LogProject("tv", EAS_TVBank.PROJECT);
		LogProject("sound", EAS_SoundBank.PROJECT);
		PrintFormat("[EXPE2E AUDIO START] total=%1 mode=%2", m_Cases.Count(), m_Mode);
		Enter(EXPE2E_EAudioPhase.NEXT, now);
	}

	//------------------------------------------------------------------------------------------------
	protected void LogProject(string bank, ResourceName project)
	{
		if (!BankSelected(bank))
			return;

		bool preload = AudioSystem.PlayEventInitialize(project);
		PrintFormat("[EXPE2E AUDIO PROJECT] bank=%1 project=%2 preload=%3", bank, project, Flag(preload));
	}

	//------------------------------------------------------------------------------------------------
	protected void TickNext(int now)
	{
		m_Index++;
		if (m_Index >= m_Cases.Count())
		{
			Finish();
			return;
		}

		EXPE2E_AudioCase audioCase = m_Cases[m_Index];
		m_Early = "na";
		m_Audible = -1;
		m_Distance = -1;
		m_LastHandle = AudioHandle.Invalid;
		if (audioCase.Group)
			ResolveGroup(audioCase);

		PrintFormat("[EXPE2E AUDIO BEGIN] seq=%1 bank=%2 id=%3 event=%4 range=%5 resolved=%6", m_Index + 1, audioCase.Bank, audioCase.Id, audioCase.EventName, RangeText(audioCase), audioCase.Resolved);
		if (audioCase.EventName.IsEmpty() || audioCase.Project.IsEmpty())
		{
			Report(audioCase, false, "empty-event-name", "na");
			Enter(EXPE2E_EAudioPhase.GAP, now);
			return;
		}

		vector position = ListenerPosition();
		vector transform[4];
		Math3D.MatrixIdentity4(transform);
		transform[3] = position;
		m_Distance = AudioSystem.GetDistance(position);
		m_Audible = AudioSystem.IsAudible(audioCase.Project, audioCase.EventName, position);
		array<string> names = {"EAS_Gain"};
		array<float> values = {GAIN};
		m_Handle = AudioSystem.PlayEvent(audioCase.Project, audioCase.EventName, transform, names, values);
		m_LastHandle = m_Handle;
		m_PlayedAt = now;
		// Same evidence shape as the pack's runtimes: [EAS DIAG] action=play ... event= handle= duration=
		if (EAS_Diagnostics.Enabled())
			EAS_Diagnostics.Event("play", null, string.Format("runtime=e2e-sweep seq=%1 bank=%2 selection=%3 resolved=%4 event=%5 handle=%6 range=%7 duration=%8 gain=%9", m_Index + 1, audioCase.Bank, audioCase.Id, audioCase.Resolved, audioCase.EventName, m_Handle, RangeText(audioCase), audioCase.Duration, GAIN));

		if (m_Handle == AudioHandle.Invalid)
		{
			Report(audioCase, false, "invalid-handle", "na");
			Enter(EXPE2E_EAudioPhase.GAP, now);
			return;
		}

		Enter(EXPE2E_EAudioPhase.PROBE, now);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickProbe(int now)
	{
		if (now - m_PlayedAt < PROBE_MS)
			return;

		m_Early = PlaybackState(m_Handle);
		Enter(EXPE2E_EAudioPhase.CHECK, now);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickCheck(int now)
	{
		if (now - m_PlayedAt < m_HoldMs)
			return;

		EXPE2E_AudioCase audioCase = m_Cases[m_Index];
		// EAS runtimes: native IsSoundPlayed is true once playback has finished.
		bool finished = AudioSystem.IsSoundPlayed(m_Handle);
		AudioSystem.TerminateSound(m_Handle);
		m_Handle = AudioHandle.Invalid;

		string late = "playing";
		if (finished)
			late = "ended";

		float window = m_HoldMs * 0.001 + 0.5;
		if (!finished)
			Report(audioCase, true, "playing", late);
		else if (audioCase.Duration > 0 && audioCase.Duration <= window)
			Report(audioCase, true, "finished-normally", late);
		else
			Report(audioCase, false, "ended-early", late);

		Enter(EXPE2E_EAudioPhase.GAP, now);
	}

	//------------------------------------------------------------------------------------------------
	protected void TickGap(int now)
	{
		if (now - m_PhaseStarted >= GAP_MS)
			Enter(EXPE2E_EAudioPhase.NEXT, now);
	}

	//------------------------------------------------------------------------------------------------
	protected void Report(EXPE2E_AudioCase audioCase, bool pass, string reason, string late)
	{
		string result = "FAIL";
		if (pass)
		{
			result = "PASS";
			m_Pass++;
		}
		else
		{
			m_Fail++;
		}

		string line = string.Format("[EXPE2E AUDIO] bank=%1 id=%2 event=%3 range=%4 result=%5 reason=%6", audioCase.Bank, audioCase.Id, audioCase.EventName, RangeText(audioCase), result, reason);
		line += string.Format(" seq=%1 default=%2 duration=%3 early=%4 late=%5 audible=%6 distance=%7 holdMs=%8", m_Index + 1, Flag(audioCase.DefaultRange), audioCase.Duration, m_Early, late, m_Audible, m_Distance, m_HoldMs);
		line += string.Format(" handle=%1 invalid=%2 resolved=%3 group=%4", m_LastHandle, AudioHandle.Invalid, audioCase.Resolved, Flag(audioCase.Group));
		line += string.Format(" project=%1", audioCase.Project);
		Print(line, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected void Finish()
	{
		m_Phase = EXPE2E_EAudioPhase.DONE;
		GetGame().GetCallqueue().Remove(Tick);
		PrintFormat("[EXPE2E AUDIO RESULT] total=%1 pass=%2 fail=%3 mode=%4", m_Cases.Count(), m_Pass, m_Fail, m_Mode);
		if (m_KeepOpen)
			return;

		// Leave time for the result to reach the log files before the engine closes.
		GetGame().GetCallqueue().CallLater(Close, 3000, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void Close()
	{
		Print("[EXPE2E AUDIO CLOSE] requested=1", LogLevel.NORMAL);
		GetGame().RequestClose();
	}

	//------------------------------------------------------------------------------------------------
	protected void Abort(string reason)
	{
		if (m_Phase == EXPE2E_EAudioPhase.DONE)
			return;

		m_Phase = EXPE2E_EAudioPhase.DONE;
		GetGame().GetCallqueue().Remove(Tick);
		// The owning world is gone; its native voice went with it.
		m_Handle = AudioHandle.Invalid;
		PrintFormat("[EXPE2E AUDIO ABORT] reason=%1 completed=%2 total=%3 pass=%4 fail=%5", reason, m_Pass + m_Fail, m_Cases.Count(), m_Pass, m_Fail);
	}

	//------------------------------------------------------------------------------------------------
	protected void Enter(EXPE2E_EAudioPhase phase, int now)
	{
		m_Phase = phase;
		m_PhaseStarted = now;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool HasCamera()
	{
		CameraManager cameras = GetGame().GetCameraManager();
		if (!cameras)
			return false;

		return cameras.CurrentCamera() != null;
	}

	//------------------------------------------------------------------------------------------------
	//! The audio listener follows the current camera; play at its position.
	protected static vector ListenerPosition()
	{
		vector mat[4];
		CameraManager cameras = GetGame().GetCameraManager();
		if (cameras)
		{
			CameraBase camera = cameras.CurrentCamera();
			if (camera)
			{
				camera.GetWorldCameraTransform(mat);
				return mat[3];
			}
		}

		BaseWorld world = GetGame().GetWorld();
		if (world)
			world.GetCurrentCamera(mat);

		return mat[3];
	}

	//------------------------------------------------------------------------------------------------
	protected static string PlaybackState(AudioHandle handle)
	{
		if (AudioSystem.IsSoundPlayed(handle))
			return "ended";

		return "playing";
	}

	//------------------------------------------------------------------------------------------------
	protected static string RangeText(EXPE2E_AudioCase audioCase)
	{
		if (audioCase.Range < 0)
			return "none";

		return audioCase.Range.ToString();
	}

	//------------------------------------------------------------------------------------------------
	protected static int Flag(bool value)
	{
		if (value)
			return 1;

		return 0;
	}
}

//! Client-side driver start. The game mode exists once per local world on the client.
modded class SCR_BaseGameMode
{
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		EXPE2E_AudioSweep.Start();
	}
}
