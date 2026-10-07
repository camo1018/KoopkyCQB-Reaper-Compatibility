// While a clear, garrison, or take cover order is running, Reaper Improved AI's
// combat changes are held off for that group. They run again when the last
// such order on that group ends. A wedge Reaper put on the group is put back,
// and the morale fire rate is applied again.

class KKREAPER_ClearState
{
	int m_iDepth;
	bool m_bHeldFormation;
	bool m_bGroupWasWedge;
	bool m_bMoveLocked;
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

	static bool MoveLocked(AIAgent agent)
	{
		if (!agent)
			return false;

		SCR_AIGroup group = SCR_AIGroup.Cast(agent);
		if (!group)
			group = SCR_AIGroup.Cast(agent.GetParentGroup());

		if (!group)
			return false;

		KKREAPER_ClearState state = s_States.Get(group);
		return state && state.m_bMoveLocked;
	}

	// Take cover sets this while it still owns the route. The fight at the
	// point clears it, so normal attack can move again. Clear and Garrison
	// never set it.
	static void SetMoveLocked(SCR_AIGroup group, bool locked)
	{
		if (!group)
			return;

		KKREAPER_ClearState state = s_States.Get(group);
		if (!state)
			return;

		state.m_bMoveLocked = locked;
	}

	static bool Begin(SCR_AIGroup group)
	{
		if (!group || !Replication.IsServer())
			return false;

		KKREAPER_ClearState existing = s_States.Get(group);
		if (existing)
		{
			existing.m_iDepth++;
			return true;
		}

		KKREAPER_ClearState state = new KKREAPER_ClearState();
		state.m_iDepth = 1;
		s_States.Set(group, state);
		HoldFormation(group, state);
		ReleaseNewSoldiers(group, state);

		SCR_AIGroupUtilityComponent utility = GroupUtility(group);
		if (utility)
			utility.KKREAPER_OnClearBegan();

		if (SCR_BaseGameMode.KK_LogEnabled())
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

		state.m_iDepth--;
		if (state.m_iDepth > 0)
			return;

		RestoreFormation(group, state);
		s_States.Remove(group);

		SCR_AIGroupUtilityComponent utility = GroupUtility(group);
		if (utility)
			utility.KKREAPER_OnClearEnded();

		if (SCR_BaseGameMode.KK_LogEnabled())
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

		if (formationComponent)
		{
			if (state.m_bGroupWasWedge)
				formationComponent.SetFormation(WEDGE);
			else if (state.m_sGroupFormation != string.Empty)
				formationComponent.SetFormation(state.m_sGroupFormation);
		}

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
	// A player command that should keep the route. Reaper stands down for that
	// group, and comes back when the order ends.
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

	protected bool KKREAPER_BoundSprint()
	{
		if (KK_GarrisonHold.IsBoundSprint(m_MyEntity))
			return true;

		if (!m_Utility)
			return false;

		return KK_GarrisonHold.IsBoundSprint(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.IsBoundSprint(m_Utility.GetOwner());
	}

	protected override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		// Bound does not pause Reaper. The sprint still has to stay a sprint.
		// Reaper aims that step at the enemy and it becomes a sidestep.
		if (KK_GarrisonHold.IsBoundSprint(body) || KK_GarrisonHold.IsBoundSprint(owner))
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			KKREAPER_KeepBoundSprint(owner);
			return ENodeResult.RUNNING;
		}

		// One man running back does not pause Reaper for the men still
		// fighting at the point. His own combat move stays off.
		if (KK_GarrisonHold.IsRecalled(body) || KK_GarrisonHold.IsRecalled(owner))
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			KKREAPER_KeepRecall(owner);
			return ENodeResult.RUNNING;
		}

		// The first man at the point turns Reaper back on for the group.
		// A pair still planted on the way has to stay there.
		if (KK_GarrisonHold.IsPinned(body) || KK_GarrisonHold.IsPinned(owner))
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			KKREAPER_HoldPinned(owner);
			return ENodeResult.RUNNING;
		}

		if (KKREAPER_ClearGate.MoveLocked(owner))
		{
			if (m_State && m_State.IsExecutingRequest())
				m_State.CancelRequest();

			KKREAPER_KeepBoundSprint(owner);
			return ENodeResult.RUNNING;
		}

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
		// The bound is the sprint. A combat move from here is the strafe
		// toward the enemy, and that strafe will not take a sprint.
		if (KKREAPER_BoundSprint())
			return;

		if (m_Utility && KKREAPER_ClearGate.MoveLocked(m_Utility.GetAIAgent()))
			return;

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
		if (KKREAPER_ClearGate.MoveLocked(owner))
			return ENodeResult.FAIL;

		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIFindCover
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.MoveLocked(owner))
			return ENodeResult.FAIL;

		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIGetCoverParameters
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.MoveLocked(owner))
			return ENodeResult.FAIL;

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
		if (KKREAPER_ClearGate.MoveLocked(owner))
			return ENodeResult.FAIL;

		if (KKREAPER_ClearGate.Affects(owner))
			return vanilla.EOnTaskSimulate(owner, dt);

		return super.EOnTaskSimulate(owner, dt);
	}
}

modded class SCR_AIGetCombatMoveRequestParameters_Move
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		if (KKREAPER_ClearGate.MoveLocked(owner))
			return ENodeResult.FAIL;

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

	override void OnActionRemoved()
	{
		super.OnActionRemoved();
		KKREAPER_TryResume();
	}

	override void Supersede()
	{
		super.Supersede();
		KKREAPER_TryResume();
	}

	protected void KKREAPER_TrySuspend()
	{
		if (m_bKKREAPER_Suspended || !IsLive())
			return;

		if (KKREAPER_ClearGate.Begin(m_Group))
			m_bKKREAPER_Suspended = true;
	}

	// A restart that keeps this same activity is not a cancel. Reaper stays
	// paused until this clear is finished, cancelled, or replaced.
	protected void KKREAPER_TryResume()
	{
		if (!m_bKKREAPER_Suspended || IsLive())
			return;

		m_bKKREAPER_Suspended = false;
		KKREAPER_ClearGate.End(m_Group);
	}
}

modded class KK_GarrisonBuildingActivity
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

	override void OnActionRemoved()
	{
		super.OnActionRemoved();
		KKREAPER_TryResume();
	}

	override void Supersede()
	{
		super.Supersede();
		KKREAPER_TryResume();
	}

	protected void KKREAPER_TrySuspend()
	{
		if (m_bKKREAPER_Suspended || !IsLive())
			return;

		if (KKREAPER_ClearGate.Begin(m_Group))
			m_bKKREAPER_Suspended = true;
	}

	// A restart that keeps this same activity is not a cancel. Reaper stays
	// paused until this garrison is finished, cancelled, or replaced.
	protected void KKREAPER_TryResume()
	{
		if (!m_bKKREAPER_Suspended || IsLive())
			return;

		m_bKKREAPER_Suspended = false;
		KKREAPER_ClearGate.End(m_Group);
	}
}

modded class KK_AttackActivity
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
		KKREAPER_SyncSuspend();
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

	override void OnActionRemoved()
	{
		super.OnActionRemoved();
		KKREAPER_TryResume();
	}

	override void Supersede()
	{
		super.Supersede();
		KKREAPER_TryResume();
	}

	protected void KKREAPER_SyncSuspend()
	{
		// The fight at the point belongs to Reaper again. A push or a bound
		// still holds Reaper off. One man running back does not.
		if (!IsTakeCover() || KKREAPER_FightYielded())
		{
			KKREAPER_EndSuspend();
			return;
		}

		if (!m_bKKREAPER_Suspended)
			KKREAPER_TrySuspend();
		else
			KKREAPER_ClearGate.CaptureLate(m_Group);

		if (m_bKKREAPER_Suspended)
			KKREAPER_ClearGate.SetMoveLocked(m_Group, true);
	}

	// At the point, with "Take cover uses attack" on, each man is released
	// when he gets there. Reaper runs for whoever is still on the point.
	// A man walked back past the return distance is held on his own.
	protected bool KKREAPER_FightYielded()
	{
		return ReleasedToFight();
	}

	protected void KKREAPER_TrySuspend()
	{
		if (m_bKKREAPER_Suspended || !IsLive() || !IsTakeCover())
			return;

		if (KKREAPER_ClearGate.Begin(m_Group))
			m_bKKREAPER_Suspended = true;
	}

	// A restart that keeps this same activity is not a cancel. Reaper stays
	// paused until this take cover is finished, cancelled, or replaced, or
	// until the fight at the point is handed over.
	protected void KKREAPER_TryResume()
	{
		if (IsLive())
			return;

		KKREAPER_EndSuspend();
	}

	protected void KKREAPER_EndSuspend()
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
		// through. The building order keeps Koopky's factor instead.
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

	override void EvaluateWeaponAndTarget(
		out bool outWeaponEvent,
		out bool outSelectedTargetChanged,
		out BaseTarget outPrevTarget,
		out BaseTarget outCurrentTarget,
		out bool outRetreatTargetChanged,
		out bool outCompartmentChanged)
	{
		super.EvaluateWeaponAndTarget(
			outWeaponEvent,
			outSelectedTargetChanged,
			outPrevTarget,
			outCurrentTarget,
			outRetreatTargetChanged,
			outCompartmentChanged
		);

		// Reaper selects the enemy again after Koopky clears him. That
		// selection aims the bound, and the aimed bound will not sprint.
		// The enemy he already had is kept, and put back when the sprint ends.
		IEntity sprinting = GetOwner();
		if (sprinting && KK_GarrisonHold.IsBoundSprint(sprinting))
		{
			KK_GarrisonHold.RememberSprintEnemy(sprinting, m_SelectedTarget);
			KK_ClearTarget();
			m_SelectedTargetVisible = false;
			outCurrentTarget = null;
			outSelectedTargetChanged = false;
			vector lane;
			if (KK_GarrisonHold.GetSprintLook(sprinting, lane))
				m_SelectedTargetDestinationPos = lane;
			return;
		}

		KK_HoldSprintEnemy(outCurrentTarget);

		// A cover hold stands against something solid. This order's sight
		// trace hits that cover. Keep a living target the normal attack
		// can still see when Reaper's own pass does not.
		IEntity owner = GetOwner();
		if (!owner || !KK_GarrisonHold.IsPinned(owner) || !KK_GarrisonHold.IsFreshOrder(owner))
			return;

		if (!m_SelectedTarget || !KK_GarrisonHold.IsLivingTarget(m_SelectedTarget))
			return;

		KK_GarrisonHold.EndFreshOrder(owner);
	}
}

// The bound runner has to keep the sprint Koopky issued. Reaper aims that
// step at the enemy, and the sprint becomes a sidestep. The look stays on
// the lane from the look node. This keeps the rifle down and the speed on
// sprint.
void KKREAPER_KeepBoundSprint(IEntity soldier)
{
	if (!KK_GarrisonHold.IsBoundSprint(soldier))
		return;

	IEntity body = soldier;
	AIAgent agent = AIAgent.Cast(soldier);
	if (agent)
		body = agent.GetControlledEntity();

	if (!body)
		body = soldier;

	CharacterControllerComponent controller = CharacterControllerComponent.Cast(
		body.FindComponent(CharacterControllerComponent)
	);
	if (controller && (controller.IsWeaponRaised() || controller.IsWeaponADS()))
	{
		controller.SetWeaponADS(false);
		controller.SetWeaponRaised(false);
	}

	AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(
		body.FindComponent(AICharacterMovementComponent)
	);
	if (movement)
		movement.SetMovementTypeWanted(EMovementType.SPRINT);

	SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(
		body.FindComponent(SCR_AIUtilityComponent)
	);
	if (!utility && agent)
	{
		utility = SCR_AIUtilityComponent.Cast(
			agent.FindComponent(SCR_AIUtilityComponent)
		);
	}

	if (!utility || !utility.m_CombatMoveState)
		return;

	utility.m_CombatMoveState.m_bAimAtTarget = false;
}

// A soldier who has stopped to shoot is pinned. Reaper still aims the move
// that was just cancelled, and that aim turns the stop into a sidestep.
void KKREAPER_HoldPinned(IEntity soldier)
{
	if (!KK_GarrisonHold.IsPinned(soldier))
		return;

	IEntity body = soldier;
	AIAgent agent = AIAgent.Cast(soldier);
	if (agent)
		body = agent.GetControlledEntity();

	if (!body)
		body = soldier;

	AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(
		body.FindComponent(AICharacterMovementComponent)
	);
	if (movement)
		movement.SetMovementTypeWanted(EMovementType.IDLE);

	SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(
		body.FindComponent(SCR_AIUtilityComponent)
	);
	if (!utility && agent)
	{
		utility = SCR_AIUtilityComponent.Cast(
			agent.FindComponent(SCR_AIUtilityComponent)
		);
	}

	if (!utility || !utility.m_CombatMoveState)
		return;

	utility.m_CombatMoveState.m_bAimAtTarget = false;
}

// A man walked back to the point keeps that run. Reaper is still on for
// the others, and aiming this step at the enemy turns it into a sidestep.
void KKREAPER_KeepRecall(IEntity soldier)
{
	if (!KK_GarrisonHold.IsRecalled(soldier))
		return;

	IEntity body = soldier;
	AIAgent agent = AIAgent.Cast(soldier);
	if (agent)
		body = agent.GetControlledEntity();

	if (!body)
		body = soldier;

	AICharacterMovementComponent movement = AICharacterMovementComponent.Cast(
		body.FindComponent(AICharacterMovementComponent)
	);
	if (movement)
		movement.SetMovementTypeWanted(EMovementType.RUN);

	SCR_AIUtilityComponent utility = SCR_AIUtilityComponent.Cast(
		body.FindComponent(SCR_AIUtilityComponent)
	);
	if (!utility && agent)
	{
		utility = SCR_AIUtilityComponent.Cast(
			agent.FindComponent(SCR_AIUtilityComponent)
		);
	}

	if (!utility || !utility.m_CombatMoveState)
		return;

	utility.m_CombatMoveState.m_bAimAtTarget = false;
}

modded class SCR_AIMoveIndividuallyBehavior
{
	override float CustomEvaluate()
	{
		float score;
		IEntity body;
		if (m_Utility)
			body = m_Utility.m_OwnerEntity;

		bool sprinting = KK_GarrisonHold.IsBoundSprint(body);
		bool planted = KK_GarrisonHold.IsPinned(body);
		bool recalled = KK_GarrisonHold.IsRecalled(body);
		if (m_Utility)
		{
			sprinting = sprinting || KK_GarrisonHold.IsBoundSprint(m_Utility.GetOwner());
			planted = planted || KK_GarrisonHold.IsPinned(m_Utility.GetOwner());
			recalled = recalled || KK_GarrisonHold.IsRecalled(m_Utility.GetOwner());
		}

		// Reaper raises this move into a strafe. The bound sprint is Koopky's.
		// A planted hold is too, and so is the run back to the point.
		if (sprinting || planted || recalled)
			score = vanilla.CustomEvaluate();
		else
			score = super.CustomEvaluate();

		// vanilla skips Koopky, so the combat-move flag has to be cleared
		// here. Left on, this move strafes at the enemy instead of sprinting.
		if (sprinting)
			m_bUseCombatMove = false;

		KKREAPER_KeepBoundSprint(body);
		KKREAPER_HoldPinned(body);
		KKREAPER_KeepRecall(body);
		return score;
	}
}

modded class SCR_AICharacterSetMovementSpeed
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		ENodeResult result = super.EOnTaskSimulate(owner, dt);

		// Reaper writes RUN onto a combat step after the order. An aimed
		// bound was coming out as a sidestep. Put the sprint back after
		// the tree has accepted the move.
		if (KK_GarrisonHold.IsBoundSprint(body) || KK_GarrisonHold.IsBoundSprint(owner))
		{
			KKREAPER_KeepBoundSprint(body);
			return result;
		}

		if (KK_GarrisonHold.IsRecalled(body) || KK_GarrisonHold.IsRecalled(owner))
		{
			KKREAPER_KeepRecall(body);
			return result;
		}

		if (KK_GarrisonHold.IsPinned(body) || KK_GarrisonHold.IsPinned(owner))
			KKREAPER_HoldPinned(body);

		// This node is in the move that a take cover jog actually runs.
		// The rifle-up node can belong to a tree Reaper no longer ticks.
		if (
			KK_GarrisonHold.IsMoveFire(body) ||
			KK_GarrisonHold.IsMoveFire(owner) ||
			KK_GarrisonHold.IsRoomFire(body) ||
			KK_GarrisonHold.IsRoomFire(owner)
		)
		{
			SCR_ChimeraAIAgent moving = SCR_ChimeraAIAgent.Cast(owner);
			if (moving)
				KK_GarrisonHold.ApplyMoveFire(moving.m_UtilityComponent);
		}
		else
		{
			IEntity soldier = body;
			if (!soldier)
				soldier = owner;

			KK_GarrisonHold.KeepClearWeaponRaised(soldier);
		}

		return result;
	}
}

modded class SCR_AILookAction
{
	override void LookAt(vector pos, float priority, float duration = 0.8)
	{
		// A look at the enemy, applied and then replaced by the lane, is
		// the turn off the sprint and back. The lane is written by the
		// look node. This call does not take it.
		if (KKREAPER_Sprinting())
			return;

		// The walk back still needs a look along the route. Reaper would
		// turn that look toward the enemy.
		if (KKREAPER_Recalled())
		{
			vanilla.LookAt(pos, priority, duration);
			return;
		}

		super.LookAt(pos, priority, duration);
	}

	override void LookAt(IEntity ent, float priority, float duration = 0.8)
	{
		// Facing the enemy turns the sprint into a strafe.
		if (KKREAPER_Sprinting() || KKREAPER_Recalled())
			return;

		super.LookAt(ent, priority, duration);
	}

	// The behavior tree finishes a look on its own timer and Reaper then
	// aims the head at the enemy. While he is sprinting, that finish is
	// what swings him off the lane and back.
	override void Complete()
	{
		if (KKREAPER_Sprinting() && m_fPriority >= SCR_AILookAction.PRIO_COMMANDER)
			return;

		super.Complete();
	}

	override void MoveLookParametersToNode(
		out bool outCanLook,
		out vector outLookPos,
		out float outLookDuration,
		out bool outCancelLook,
		out bool outRestartLook)
	{
		// Reaper's own look runs in this call and points him at the enemy.
		// The next pass points him down the lane. That pair of writes is
		// the head thrash. While he is sprinting, this node does not
		// enter Reaper, and the turn is started only once.
		if (KKREAPER_Sprinting())
		{
			vector lane;
			IEntity body;
			if (m_Utility)
				body = m_Utility.m_OwnerEntity;

			bool haveLane = KK_GarrisonHold.GetSprintLook(body, lane);
			if (!haveLane && m_Utility)
				haveLane = KK_GarrisonHold.GetSprintLook(m_Utility.GetOwner(), lane);

			m_bCancelLook = false;
			m_bRestartLook = false;

			if (!haveLane)
			{
				outCanLook = false;
				outCancelLook = false;
				outRestartLook = false;
				outLookPos = vector.Zero;
				outLookDuration = 0;
				return;
			}

			bool restart = !m_bKKLaneHeld;
			m_bKKLaneHeld = true;

			outCanLook = true;
			outCancelLook = false;
			outRestartLook = restart;
			outLookPos = lane;
			outLookDuration = 8;
			m_vPosition = lane;
			m_fPriority = SCR_AILookAction.PRIO_COMMANDER;
			m_fDuration = 8;
			return;
		}

		m_bKKLaneHeld = false;
		super.MoveLookParametersToNode(
			outCanLook,
			outLookPos,
			outLookDuration,
			outCancelLook,
			outRestartLook
		);
	}

	protected bool m_bKKLaneHeld;

	protected bool KKREAPER_Sprinting()
	{
		if (!m_Utility)
			return false;

		return KK_GarrisonHold.IsBoundSprint(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.IsBoundSprint(m_Utility.GetOwner());
	}

	protected bool KKREAPER_Recalled()
	{
		if (!m_Utility)
			return false;

		return KK_GarrisonHold.IsRecalled(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.IsRecalled(m_Utility.GetOwner());
	}
}

// The attack aims the bound, and the aimed step walks. Reaper scores
// that attack without calling through, so the zero has to land here.
modded class SCR_AIAttackBehavior
{
	override float CustomEvaluate()
	{
		if (!m_Utility)
			return super.CustomEvaluate();

		if (
			KK_GarrisonHold.IsBoundSprint(m_Utility.m_OwnerEntity) ||
			KK_GarrisonHold.IsBoundSprint(m_Utility.GetOwner())
		)
		{
			m_bUseCombatMove = false;
			return 0;
		}

		return super.CustomEvaluate();
	}
}

// Outermost, so Reaper cannot accept a combat move during the bound.
// That request is the walk toward the enemy. The sprint is the order.
modded class SCR_AICombatMoveState
{
	override void ApplyNewRequest(notnull SCR_AICombatMoveRequestBase request)
	{
		if (KK_GarrisonHold.IsSprintMoveLocked(this))
		{
			request.m_eState = SCR_EAICombatMoveRequestState.CANCELED;
			if (m_Request && m_Request.m_eState == SCR_EAICombatMoveRequestState.EXECUTING)
				m_Request.m_eState = SCR_EAICombatMoveRequestState.CANCELED;

			m_Request = null;
			m_bAimAtTarget = false;
			return;
		}

		super.ApplyNewRequest(request);
	}

	override void EnableAiming(bool enable)
	{
		if (enable && KK_GarrisonHold.IsSprintMoveLocked(this))
		{
			m_bAimAtTarget = false;
			return;
		}

		super.EnableAiming(enable);
	}
}

modded class SCR_AISetWeaponRaised
{
	override ENodeResult EOnTaskSimulate(AIAgent owner, float dt)
	{
		IEntity body;
		if (owner)
			body = owner.GetControlledEntity();

		if (KK_GarrisonHold.IsBoundSprint(body) || KK_GarrisonHold.IsBoundSprint(owner))
		{
			if (body)
			{
				CharacterControllerComponent controller =
					CharacterControllerComponent.Cast(
						body.FindComponent(CharacterControllerComponent)
					);

				// Sending the lower again restarts it and cuts the step off.
				if (controller && (controller.IsWeaponRaised() || controller.IsWeaponADS()))
				{
					controller.SetWeaponADS(false);
					controller.SetWeaponRaised(false);
				}
			}

			return ENodeResult.SUCCESS;
		}

		IEntity soldier = body;
		if (!soldier)
			soldier = owner;

		if (KK_GarrisonHold.IsIgnoringTargets(body) || KK_GarrisonHold.IsIgnoringTargets(owner))
		{
			if (body)
			{
				CharacterControllerComponent controller =
					CharacterControllerComponent.Cast(
						body.FindComponent(CharacterControllerComponent)
					);

				if (controller)
					controller.SetWeaponRaised(false);
			}

			return ENodeResult.SUCCESS;
		}

		// Call the shot here. super reaches Reaper's node when that addon
		// loads after Koopky, and that node does not run Koopky's trigger.
		if (KK_GarrisonHold.OwnsShot(body) || KK_GarrisonHold.OwnsShot(owner))
		{
			SCR_ChimeraAIAgent roomSoldier = SCR_ChimeraAIAgent.Cast(owner);
			if (roomSoldier)
				KK_GarrisonHold.ApplyRoomShot(roomSoldier.m_UtilityComponent);

			KK_GarrisonHold.KeepClearWeaponRaised(soldier);
			return ENodeResult.SUCCESS;
		}

		if (
			KK_GarrisonHold.IsMoveFire(body) ||
			KK_GarrisonHold.IsMoveFire(owner) ||
			KK_GarrisonHold.IsRoomFire(body) ||
			KK_GarrisonHold.IsRoomFire(owner)
		)
		{
			SCR_ChimeraAIAgent firing = SCR_ChimeraAIAgent.Cast(owner);
			if (firing)
				KK_GarrisonHold.ApplyMoveFire(firing.m_UtilityComponent);

			return ENodeResult.SUCCESS;
		}

		if (KK_GarrisonHold.IsQuietReload(body) || KK_GarrisonHold.IsQuietReload(owner))
			return ENodeResult.SUCCESS;

		if (
			KK_GarrisonHold.SprintBeforeReload(body) ||
			KK_GarrisonHold.SprintBeforeReload(owner) ||
			KK_GarrisonHold.IsReloadBashing(body) ||
			KK_GarrisonHold.IsReloadBashing(owner)
		)
		{
			KK_GarrisonHold.LowerForReloadSprint(owner);
			return ENodeResult.SUCCESS;
		}

		if (KK_GarrisonHold.KeepClearWeaponRaised(soldier))
			return ENodeResult.SUCCESS;

		return super.EOnTaskSimulate(owner, dt);
	}
}
