// EXPBG AI Global Skills module: the Game Master handle for the global settings.
// The settings are active while at least one module exists. Every module mirrors the
// per-faction table in replicated properties so the attribute dialog can switch factions
// on any client (late joiners included); AI behaviour itself only runs on the server.
[EntityEditorProps(category: "EXPBG/AI", description: "EXPBG AI Global Skills: per-faction AI skill and aim, rules of engagement and ammunition")]
class EGS_ModuleClass : GenericEntityClass
{
}

class EGS_Module : GenericEntity
{
	protected static ref array<EGS_Module> s_aModules = {};

	[RplProp()]
	protected ref array<string> m_aFactionKeys = {};

	[RplProp()]
	protected ref array<int> m_aValues = {};

	//------------------------------------------------------------------------------------------------
	void EGS_Module(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		if (!GetGame().InPlayMode())
			return;

		if (!s_aModules.Contains(this))
			s_aModules.Insert(this);

		if (!Replication.IsServer())
			return;

		EGS_Publish();
		EGS_Manager.OnModulesChanged();
	}

	//------------------------------------------------------------------------------------------------
	void ~EGS_Module()
	{
		if (s_aModules)
			s_aModules.RemoveItem(this);

		if (GetGame() && GetGame().GetWorld() && Replication.IsServer())
			EGS_Manager.OnModulesChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! \return true while at least one module exists (server and clients).
	static bool HasAny()
	{
		return GetAny() != null;
	}

	//------------------------------------------------------------------------------------------------
	static EGS_Module GetAny()
	{
		for (int i = s_aModules.Count() - 1; i >= 0; i--)
		{
			EGS_Module module = s_aModules[i];
			if (module)
				return module;

			s_aModules.Remove(i);
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: copy the authoritative settings into every module and replicate them.
	static void PublishAll()
	{
		for (int i = s_aModules.Count() - 1; i >= 0; i--)
		{
			EGS_Module module = s_aModules[i];
			if (!module)
			{
				s_aModules.Remove(i);
				continue;
			}

			module.EGS_Publish();
		}
	}

	//------------------------------------------------------------------------------------------------
	void EGS_Publish()
	{
		if (!Replication.IsServer())
			return;

		EGS_Settings.ExportReplicated(m_aFactionKeys, m_aValues);
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	int EGS_GetFactionCount()
	{
		return m_aFactionKeys.Count();
	}

	//------------------------------------------------------------------------------------------------
	string EGS_GetFactionKey(int index)
	{
		if (!m_aFactionKeys.IsIndexValid(index))
			return string.Empty;

		return m_aFactionKeys[index];
	}

	//------------------------------------------------------------------------------------------------
	//! Replicated per-faction value (used by the client-side faction switch in the dialog).
	int EGS_GetValue(int factionIndex, int slot)
	{
		if (slot < 0 || slot >= EGS_Settings.SLOT_COUNT)
			return 0;

		int index = factionIndex * EGS_Settings.SLOT_COUNT + slot;
		if (factionIndex < 0 || !m_aValues.IsIndexValid(index))
			return 0;

		return m_aValues[index];
	}
}
