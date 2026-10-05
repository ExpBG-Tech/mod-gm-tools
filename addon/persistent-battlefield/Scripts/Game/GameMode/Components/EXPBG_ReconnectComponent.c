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
	override bool HandlePlayerDisconnect(int playerId, KickCauseCode cause)
	{
		const SCR_ReconnectData data = StoreData(playerId);
		if (!IsDataRelevant(data))
		{
			PrintFormat("[EXPBG Reconnect] Reservation rejected: no living character for playerId=%1 cause=%2", playerId, cause);
			return false;
		}

		UUID identity;
		if (!m_mEXPBGSettledIdentities.Find(playerId, identity))
			identity = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);

		if (identity.IsNull())
		{
			Print(string.Format("[EXPBG Reconnect] Reservation rejected: identity unavailable for playerId=%1 cause=%2", playerId, cause), LogLevel.WARNING);
			return false;
		}

		m_mReconnectData.Set(identity, data);
		UpdateExpieryCheck();
		PrintFormat("[EXPBG Reconnect] Reserved living character for playerId=%1 cause=%2", playerId, cause);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);
		m_mEXPBGSettledIdentities.Remove(playerId);
	}
}
