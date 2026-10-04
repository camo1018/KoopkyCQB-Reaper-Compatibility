// While a clear is running, Reaper Improved AI's combat changes are held
// off for that group. They run again when the clear ends. A wedge Reaper
// put on the group is put back, and the morale fire rate is applied again.

class KKREAPER_ClearState
{
	bool m_bHeldFormation;
	bool m_bGroupWasWedge;
	string m_sGroupFormation;
	ref array<int> m_aWedgeHandlers = new array<int>();
	ref set<AIAgent> m_aReleased = new set<AIAgent>();
}

class KKREAPER_ClearGate
{
	protected static const string WEDGE = "Wedge";
	protected static const string COLUMN = "StaggeredColumn";

	protected static ref map<SCR_AIGroup, ref KKREAPER_ClearState> s_States =
		new map<SCR_AIGroup, ref KKREAPER_ClearState>();

	static bool IsGroup(SCR_AIGroup group)
	{
		if (!group)
			return false;

		return s_States.Get(group) != null;
	}

	static bool Affects(AIAgent agent)
	{
		if (!agent)
			return false;

		SCR_AIGroup group = SCR_AIGroup.Cast(agent);
		if (!group)
			group = SCR_AIGroup.Cast(agent.GetParentGroup());

		return IsGroup(group);
	}

	static bool AffectsEntity(IEntity body)
	{
		if (!body)
			return false;

		AIControlComponent control = AIControlComponent.Cast(
			body.FindComponent(AIControlComponent)
		);
		if (!control)
			return false;

		return Affects(control.GetAIAgent());
	}

	static bool Begin(SCR_AIGroup group)
	{
		if (!group || !Replication.IsServer() || IsGroup(group))
			return IsGroup(group);

		KKREAPER_ClearState state = new KKREAPER_ClearState();
		s_States.Set(group, state);
		HoldFormation(group, state);
		ReleaseNewSoldiers(group, state);

		SCR_AIGroupUtilityComponent utility = GroupUtility(group);
		if (utility)
			utility.KKREAPER_OnClearBegan();

		PrintFormat("KKREAPER: Paused Reaper Improved AI for %1", group);
		return true;
	}

	static void End(SCR_AIGroup group)
	{
		if (!group || !Replication.IsServer())
			return;

		KKREAPER_ClearState state = s_States.Get(group);
		if (!state)
			return;

		RestoreFormation(group, state);
		s_States.Remove(group);

		SCR_AIGroupUtilityComponent utility = GroupUtility(group);
		if (utility)
			utility.KKREAPER_OnClearEnded();

		PrintFormat("KKREAPER: Restored Reaper Improved AI for %1", group);
	}

	static void CaptureLate(SCR_AIGroup group)
	{
		if (!group || !Replication.IsServer())
			return;

		KKREAPER_ClearState state = s_States.Get(group);
		if (!state)
			return;

		ReleaseNewSoldiers(group, state);
	}

	protected static SCR_AIGroupUtilityComponent GroupUtility(SCR_AIGroup group)
	{
		return SCR_AIGroupUtilityComponent.Cast(
			group.FindComponent(SCR_AIGroupUtilityComponent)
		);
	}

	protected static SCR_AIUtilityComponent SoldierUtility(AIAgent agent)
	{
		SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(
			agent.FindComponent(SCR_AIUtilityComponent)
		);
		if (utility)
			return utility;

		IEntity body = agent.GetControlledEntity();
		if (!body)
			return null;

		return SCR_AIUtilityComponent.Cast(
			body.FindComponent(SCR_AIUtilityComponent)
		);
	}

	protected static void ReleaseNewSoldiers(SCR_AIGroup group, KKREAPER_ClearState state)
	{
		array<AIAgent> agents = {};
		group.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent || state.m_aReleased.Contains(agent))
				continue;

			SCR_AIUtilityComponent utility = SoldierUtility(agent);
			if (!utility)
				continue;

			state.m_aReleased.Insert(agent);

			if (utility.m_CombatMoveState)
			{
				utility.m_CombatMoveState.CancelRequest();
				utility.m_CombatMoveState.ReleaseCover();
			}

			array<ref AIActionBase> waits = {};
			utility.FindActionsOfType(REAPER_AI_DoorPassageWaitBehavior, waits);
			foreach (AIActionBase action : waits)
			{
				SCR_AIBehaviorBase wait = SCR_AIBehaviorBase.Cast(action);
				if (wait)
					wait.Complete();
			}
		}
	}

	protected static void HoldFormation(SCR_AIGroup group, KKREAPER_ClearState state)
	{
		AIFormationComponent formationComponent = group.GetFormationComponent();
		AIGroupMovementComponent movement = AIGroupMovementComponent.Cast(
			group.FindComponent(AIGroupMovementComponent)
		);

		if (formationComponent)
		{
			AIFormationDefinition current = formationComponent.GetFormation();
			if (current)
				state.m_sGroupFormation = current.GetName();
		}

		if (movement)
		{
			int nextHandlerId = 0;
			while (movement.GetMoveHandlerAgentCount(nextHandlerId) != -1)
			{
				AIFormationDefinition handlerFormation = movement.GetFormationDefinition(nextHandlerId);
				if (handlerFormation && handlerFormation.GetName() == WEDGE)
					state.m_aWedgeHandlers.Insert(nextHandlerId);

				nextHandlerId++;
			}
		}

		bool groupWasWedge = state.m_sGroupFormation == WEDGE;
		if (!groupWasWedge && state.m_aWedgeHandlers.IsEmpty())
			return;

		state.m_bHeldFormation = true;
		state.m_bGroupWasWedge = groupWasWedge;

		if (groupWasWedge && formationComponent)
			formationComponent.SetFormation(COLUMN);

		if (!movement)
			return;

		foreach (int handlerId : state.m_aWedgeHandlers)
			movement.SetFormationDefinition(handlerId, COLUMN);
	}

	protected static void RestoreFormation(SCR_AIGroup group, KKREAPER_ClearState state)
	{
		if (!state.m_bHeldFormation)
			return;

		AIFormationComponent formationComponent = group.GetFormationComponent();
		AIGroupMovementComponent movement = AIGroupMovementComponent.Cast(
			group.FindComponent(AIGroupMovementComponent)
		);
		if (!formationComponent)
			return;

		if (state.m_bGroupWasWedge)
			formationComponent.SetFormation(WEDGE);
		else if (state.m_sGroupFormation != string.Empty)
			formationComponent.SetFormation(state.m_sGroupFormation);

		if (!movement)
			return;

		foreach (int handlerId : state.m_aWedgeHandlers)
			movement.SetFormationDefinition(handlerId, WEDGE);

		if (!state.m_bGroupWasWedge)
			return;

		int nextHandlerId = 0;
		while (movement.GetMoveHandlerAgentCount(nextHandlerId) != -1)
		{
			AIFormationDefinition handlerFormation = movement.GetFormationDefinition(nextHandlerId);
			if (handlerFormation && handlerFormation.GetName() == COLUMN)
				movement.SetFormationDefinition(nextHandlerId, WEDGE);

			nextHandlerId++;
		}
	}
}

modded class REAPER_AI_PlayerCommandPriority
{
	// A clear keeps the player's route. Reaper's own checks stand down for
	// that group, and come back when the clear ends.
	override static bool REAPER_AI_ShouldPreservePlayerMovement(AIAgent agent)
	{
		if (KKREAPER_ClearGate.Affects(agent))
			return true;

		if (!REAPER_AI_HasPlayerWaypoint(agent))
			return false;

		SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(
			agent.FindComponent(SCR_AIUtilityComponent)
		);
		if (!utility || !utility.m_ThreatSystem)
			return true;

		EAIThreatState threatState = utility.m_ThreatSystem.GetState();
		return threatState != EAIThreatState.ALERTED && threatState != EAIThreatState.THREATENED;
	}
}

modded class SCR_AIGroup
{
	override void OnAgentAdded(AIAgent child)
	{
		if (KKREAPER_ClearGate.IsGroup(this))
		{
			vanilla.OnAgentAdded(child);
			return;
		}

		super.OnAgentAdded(child);
	}
}

modded class SCR_AIGroupUtilityComponent
{
	void KKREAPER_OnClearBegan()
	{
		KKREAPER_UseBaseFireRate();
	}

	void KKREAPER_OnClearEnded()
	{
		UpdateThreatMeasure();
	}

	protected override void UpdateThreatMeasure()
	{
		super.UpdateThreatMeasure();

		if (KKREAPER_ClearGate.Affects(GetAIAgent()))
			KKREAPER_UseBaseFireRate();
	}

	override void SetFireRateCoef(float coef = 1, bool overridePersistent = false)
	{
		super.SetFireRateCoef(coef, overridePersistent);

		if (KKREAPER_ClearGate.Affects(GetAIAgent()))
			KKREAPER_UseBaseFireRate();
	}

	protected void KKREAPER_UseBaseFireRate()
	{
		foreach (SCR_AIInfoComponent info : m_aInfoComponents)
		{
			if (!info)
				continue;

			SCR_AICombatComponent combat = info.GetCombatComponent();
			if (combat)
				combat.SetGroupFireRateCoef(m_fFireRateCoef);
		}
	}
}

modded class SCR_AIGroupInfoComponent
{
	override bool IsGrenadeThrowAllowed(AIAgent soldierAgent)
	{
		if (m_UtilityComponent && KKREAPER_ClearGate.Affects(m_UtilityComponent.GetAIAgent()))
			return vanilla.IsGrenadeThrowAllowed(soldierAgent);

		return super.IsGrenadeThrowAllowed(soldierAgent);
	}

	override void OnAgentSelectedGrenade(AIAgent soldierAgent)
	{
		if (m_UtilityComponent && KKREAPER_ClearGate.Affects(m_UtilityComponent.GetAIAgent()))
		{
			vanilla.OnAgentSelectedGrenade(soldierAgent);
			return;
		}

		super.OnAgentSelectedGrenade(soldierAgent);
	}
}

modded class SCR_AICombatMoveLogic_Attack
{
	protected bool KKREAPER_Clearing()
	{
		return m_Utility && KKREAPER_ClearGate.Affects(m_Utility.GetAIAgent());
	}

	protected override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}

	protected override bool ResolveFailMoveIfNoCover()
	{
		if (KKREAPER_Clearing())
			return vanilla.ResolveFailMoveIfNoCover();

		return super.ResolveFailMoveIfNoCover();
	}

	protected override float ResolveStoppedWaitTime(bool inCover, EAIThreatState threat, EWeaponType weaponType)
	{
		if (KKREAPER_Clearing())
			return vanilla.ResolveStoppedWaitTime(inCover, threat, weaponType);

		return super.ResolveStoppedWaitTime(inCover, threat, weaponType);
	}

	protected override void PushRequestMove()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.PushRequestMove();
			return;
		}

		super.PushRequestMove();
	}

	protected override void PushRequestLeaveUselessCover()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.PushRequestLeaveUselessCover();
			return;
		}

		super.PushRequestLeaveUselessCover();
	}

	protected override bool SuppressedInCoverCondition()
	{
		if (!KKREAPER_Clearing())
			return super.SuppressedInCoverCondition();

		if (KK_GarrisonHold.IsPinned(m_MyEntity))
			return false;

		if (m_Utility && KK_GarrisonHold.IsPinned(m_Utility.GetOwner()))
			return false;

		return vanilla.SuppressedInCoverCondition();
	}

	protected override bool CurrentCoverUselessCondition()
	{
		if (KKREAPER_Clearing())
			return vanilla.CurrentCoverUselessCondition();

		return super.CurrentCoverUselessCondition();
	}

	protected override bool MoveToNextPosCondition()
	{
		if (KKREAPER_Clearing())
			return vanilla.MoveToNextPosCondition();

		return super.MoveToNextPosCondition();
	}
}

modded class SCR_AICalculateCoverQueryProps_CombatMove
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIFindCover
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIGetCoverParameters
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AISetStance
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIGetCombatMovementParameters
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIGetCombatMoveRequestParameters_Move
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AISwitchMagazine
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIDecoWeaponUnobstructed
{
	override bool TestFunction(AIAgent owner)
	{
		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.TestFunction(owner);

		return super.TestFunction(owner);
	}
}

modded class REAPER_AI_DoorPassageWaitBehavior
{
	override float CustomEvaluate()
	{
		if (m_Utility && KKREAPER_ClearGate.Affects(m_Utility.GetAIAgent()))
		{
			Complete();
			return 0;
		}

		return super.CustomEvaluate();
	}
}

[BaseContainerProps()]
modded class SCR_AIGoalReaction_OpenNavlinkDoor
{
	override void PerformReaction(notnull SCR_AIUtilityComponent utility, SCR_AIMessageBase message)
	{
		if (KKREAPER_ClearGate.Affects(utility.GetAIAgent()))
		{
			vanilla.PerformReaction(utility, message);
			return;
		}

		super.PerformReaction(utility, message);
	}
}

modded class SCR_DoorUserAction
{
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		AIAgent agent;
		if (pUserEntity)
		{
			AIControlComponent control = AIControlComponent.Cast(
				pUserEntity.FindComponent(AIControlComponent)
			);
			if (control)
				agent = control.GetAIAgent();
		}

		if (KKREAPER_ClearGate.Affects(agent))
		{
			vanilla.PerformAction(pOwnerEntity, pUserEntity);
			return;
		}

		super.PerformAction(pOwnerEntity, pUserEntity);
	}
}

modded class SCR_AIThrowGrenadeToBehavior
{
	protected bool KKREAPER_Clearing()
	{
		return m_Utility && KKREAPER_ClearGate.Affects(m_Utility.GetAIAgent());
	}

	override void OnActionSelected()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.OnActionSelected();
			return;
		}

		super.OnActionSelected();
	}

	override void OnActionExecuted()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.OnActionExecuted();
			return;
		}

		super.OnActionExecuted();
	}

	override void OnActionDeselected()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.OnActionDeselected();
			return;
		}

		super.OnActionDeselected();
	}

	override void OnActionCompleted()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.OnActionCompleted();
			return;
		}

		super.OnActionCompleted();
	}

	override void OnActionFailed()
	{
		if (KKREAPER_Clearing())
		{
			vanilla.OnActionFailed();
			return;
		}

		super.OnActionFailed();
	}
}

modded class SCR_AIMedicHealBehavior
{
	override float CustomEvaluate()
	{
		if (m_Utility && KKREAPER_ClearGate.Affects(m_Utility.GetAIAgent()))
			return vanilla.CustomEvaluate();

		return super.CustomEvaluate();
	}
}

modded class SCR_AIHealActivitySmokeCoverFeature
{
	override bool ExecuteForActivity(SCR_AIActivityBase activity, int maxPositionCount = 3)
	{
		if (activity && activity.m_Utility && KKREAPER_ClearGate.Affects(activity.m_Utility.GetAIAgent()))
			return vanilla.ExecuteForActivity(activity, maxPositionCount);

		return super.ExecuteForActivity(activity, maxPositionCount);
	}
}

modded class SCR_AIActivitySmokeCoverFeature
{
	override bool Execute(
		notnull SCR_AIGroupUtilityComponent groupUtility,
		vector targetPosition,
		SCR_AIActivitySmokeCoverFeatureProperties smokeCoverProperties,
		notnull array<AIAgent> avoidAgents,
		notnull array<AIAgent> excludeAgents,
		int maxPositionCount = 1,
		SCR_AIActivityBase contextActivity = null)
	{
		if (KKREAPER_ClearGate.Affects(groupUtility.GetAIAgent()))
			return false;

		return super.Execute(
			groupUtility,
			targetPosition,
			smokeCoverProperties,
			avoidAgents,
			excludeAgents,
			maxPositionCount,
			contextActivity
		);
	}
}

modded class SCR_AIGroupTargetClusterProcessor
{
	override SCR_AIActivityBase TryCreateActivityForState(
		SCR_AITargetClusterState s,
		EAITargetClusterState estate,
		notnull TFireteamLockRefArray inFtsMain,
		notnull TFireteamLockRefArray inFtsAux)
	{
		if (m_Utility && KKREAPER_ClearGate.Affects(m_Utility.GetAIAgent()))
			return vanilla.TryCreateActivityForState(s, estate, inFtsMain, inFtsAux);

		return super.TryCreateActivityForState(s, estate, inFtsMain, inFtsAux);
	}
}

modded class KK_ClearBuildingActivity
{
	protected bool m_bKKREAPER_Suspended;

	override void OnActionSelected()
	{
		super.OnActionSelected();
		KKREAPER_TrySuspend();
	}

	override float CustomEvaluate()
	{
		float score = super.CustomEvaluate();

		if (m_bKKREAPER_Suspended)
			KKREAPER_ClearGate.CaptureLate(m_Group);

		return score;
	}

	override void OnActionDeselected()
	{
		super.OnActionDeselected();
		KKREAPER_TryResume();
	}

	override void OnActionFailed()
	{
		super.OnActionFailed();
		KKREAPER_TryResume();
	}

	protected void KKREAPER_TrySuspend()
	{
		if (m_bKKREAPER_Suspended || m_bFinished || m_bCancelled || !m_ClearWaypoint)
			return;

		if (KKREAPER_ClearGate.Begin(m_Group))
			m_bKKREAPER_Suspended = true;
	}

	protected void KKREAPER_TryResume()
	{
		if (!m_bKKREAPER_Suspended)
			return;

		m_bKKREAPER_Suspended = false;
		KKREAPER_ClearGate.End(m_Group);
	}
}

modded class SCR_AICombatComponent
{
	override void UpdatePerceptionFactor(
		PerceptionComponent perceptionComp,
		SCR_AIThreatSystem threatSystem
	)
	{
		IEntity owner = GetOwner();
		if (!KKREAPER_ClearGate.AffectsEntity(owner))
		{
			super.UpdatePerceptionFactor(perceptionComp, threatSystem);
			return;
		}

		// Reaper replaces recognition while threatened and does not call
		// through. A clear keeps Koopky's factor instead.
		if (owner && KK_GarrisonHold.IsIgnoringTargets(owner))
		{
			if (perceptionComp)
				perceptionComp.SetPerceptionFactor(0);

			return;
		}

		if (
			!perceptionComp ||
			!threatSystem ||
			!KK_PerceptionBoost.IsActiveSoldier(owner) ||
			!KK_PerceptionBoost.UseSharpCombat()
		)
		{
			vanilla.UpdatePerceptionFactor(perceptionComp, threatSystem);
			return;
		}

		EAIThreatState threatState = threatSystem.GetState();
		float perceptionFactor = PERCEPTION_FACTOR_SAFE;

		switch (threatState)
		{
			case EAIThreatState.VIGILANT:
				perceptionFactor = PERCEPTION_FACTOR_VIGILANT;
				break;
			case EAIThreatState.ALERTED:
				perceptionFactor = PERCEPTION_FACTOR_ALERTED;
				break;
			case EAIThreatState.THREATENED:
				perceptionFactor = PERCEPTION_FACTOR_ALERTED;
				break;
		}

		perceptionFactor *= m_fEquipmentPerceptionFactor;
		perceptionFactor *= m_fPerceptionFactor;
		perceptionComp.SetPerceptionFactor(perceptionFactor);
	}
}
