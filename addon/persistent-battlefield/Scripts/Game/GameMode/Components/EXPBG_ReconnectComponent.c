//------------------------------------------------------------------------------------------------
//! Extends the vanilla 1.8 reconnect reservation so every disconnect cause can retain a living
//! character. Vanilla remains responsible for reconnect possession and reservation expiry.
//------------------------------------------------------------------------------------------------
modded class SCR_ReconnectComponent : SCR_BaseGameModeComponent
{
	protected ref map<int, UUID> m_mEXPBGSettledIdentities = new map<int, UUID>();

	//------------------------------------------------------------------------------------------------
	override protected void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (Replication.IsServer())
			m_iReconnectTime = 900;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerAuditSuccess(int playerId)
	{
		super.OnPlayerAuditSuccess(playerId);
		EXPBG_CacheSettledIdentity(playerId, "successful");
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerAuditFail(int playerId)
	{
		super.OnPlayerAuditFail(playerId);
		EXPBG_CacheSettledIdentity(playerId, "failed/offline");
	}

	//------------------------------------------------------------------------------------------------
	protected void EXPBG_CacheSettledIdentity(int playerId, string auditResult)
	{
		if (!Replication.IsServer() || playerId <= 0)
			return;

		const UUID identity = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
		if (identity.IsNull())
		{
			Print(string.Format("[EXPBG Reconnect] Identity unavailable after %1 audit for playerId=%2", auditResult, playerId), LogLevel.WARNING);
			return;
		}

		m_mEXPBGSettledIdentities.Set(playerId, identity);
		PrintFormat("[EXPBG Reconnect] Cached identity after %1 audit for playerId=%2", auditResult, playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Every disconnect cause is eligible. Each log line names the check that decided.
	override bool HandlePlayerDisconnect(int playerId, KickCauseCode cause)
	{
		const SCR_ReconnectData data = StoreData(playerId);
		if (!IsDataRelevant(data))
		{
			PrintFormat("[EXPBG Reconnect] Reservation rejected: %1 for playerId=%2 cause=%3", EXPBG_DescribeIrrelevantData(data), playerId, EXPBG_DescribeCause(cause));
			return false;
		}

		UUID identity;
		if (!m_mEXPBGSettledIdentities.Find(playerId, identity))
			identity = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);

		if (identity.IsNull())
		{
			Print(string.Format("[EXPBG Reconnect] Reservation rejected: identity unavailable for playerId=%1 cause=%2", playerId, EXPBG_DescribeCause(cause)), LogLevel.WARNING);
			return false;
		}

		m_mReconnectData.Set(identity, data);
		UpdateExpieryCheck();
		PrintFormat("[EXPBG Reconnect] Reserved living character for playerId=%1 cause=%2", playerId, EXPBG_DescribeCause(cause));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Which part of IsDataRelevant refused the reservation.
	protected string EXPBG_DescribeIrrelevantData(notnull SCR_ReconnectData data)
	{
		if (!data.m_ReservedEntity)
			return "no controlled entity";

		const ChimeraCharacter character = ChimeraCharacter.Cast(data.m_ReservedEntity);
		if (!character)
			return "controlled entity is not a character";

		CharacterControllerComponent controller = character.GetCharacterController();
		if (!controller)
			return "character has no controller";

		if (controller.IsDead())
			return "character is dead";

		return "character refused by IsDataRelevant";
	}

	//------------------------------------------------------------------------------------------------
	//! KickCauseCode is an opaque handle: name its group and reason, e.g. "REPLICATION/SHUTDOWN (1/9)".
	protected string EXPBG_DescribeCause(KickCauseCode cause)
	{
		if (!cause)
			return "none";

		KickCauseGroup2 groupInt;
		int reasonInt;
		string group, reason;
		GetGame().GetFullKickReason(cause, groupInt, reasonInt, group, reason);
		return string.Format("%1/%2 (%3/%4)", group, reason, groupInt, reasonInt);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);
		m_mEXPBGSettledIdentities.Remove(playerId);
	}
}
