#include "pch-il2cpp.h"
#include "KillAura.h"

#include <helpers.h>
#include <algorithm>

#include <cheat/events.h>
#include <cheat/game/EntityManager.h>
#include <cheat/game/util.h>
#include <cheat/game/filters.h>

namespace cheat::feature 
{
    KillAura::KillAura() : Feature(),
        NF(m_Enabled,      "Kill aura",                 "KillAura", false),
        NF(m_OnlyTargeted, "Only targeted",             "KillAura", true),
        NF(m_Range,        "Range",                     "KillAura", 15.0f),
        NF(m_AttackDelay,  "Attack delay time (in ms)", "KillAura", 100),
        NF(m_RepeatDelay,  "Repeat delay time (in ms)", "KillAura", 1000)
    { 
		events::GameUpdateEvent += MY_METHOD_HANDLER(KillAura::OnGameUpdate);
	}

    const FeatureGUIInfo& KillAura::GetGUIInfo() const
    {
        static const FeatureGUIInfo info{ "Kill aura", "World", true };
        return info;
    }

    void KillAura::DrawMain()
    {
		ConfigWidget("Enabled", m_Enabled, "Enables kill aura.\n" \
            "Kill aura cause crash damage for monster around you.");
		ConfigWidget(m_Range, 0.1f, 5.0f, 100.0f);
		ConfigWidget(m_OnlyTargeted, "If enabled, only monsters whose target at you will be affected by kill aura.");
		ConfigWidget(m_AttackDelay, 1, 0, 1000, "Delay before attack next monster.");
		ConfigWidget(m_RepeatDelay, 1, 100, 2000, "Delay before next attack same monster.");
    }

    bool KillAura::NeedStatusDraw() const
	{
        return m_Enabled;
    }

    void KillAura::DrawStatus() 
    { 
        ImGui::Text("Kill aura [%.01fm]", m_Range.value());
    }

    KillAura& KillAura::GetInstance()
    {
        static KillAura instance;
        return instance;
    }

	// Kill aura logic is just emulate monster fall crash, so simple but works.
	// Note. No work on mob with shield, maybe update like auto ore destroy.
	void KillAura::OnGameUpdate()
	{
		static std::default_random_engine generator;
		static std::uniform_int_distribution<int> distribution(-50, 50);

		static int64_t nextAttackTime = 0;
		static std::map<uint32_t, int64_t> monsterRepeatTimeMap;
		static std::queue<game::Entity*> attackQueue;
		static std::unordered_set<uint32_t> attackSet;

		if (!m_Enabled)
			return;

		auto eventManager = GET_SINGLETON(EventManager);
		if (eventManager == nullptr || *app::CreateCrashEvent__MethodInfo == nullptr)
			return;

		auto currentTime = util::GetCurrentTimeMillisec();
		if (currentTime < nextAttackTime)
			return;

		auto& manager = game::EntityManager::instance();

		for (const auto& monster : manager.entities(game::filters::combined::Monsters))
		{
			auto monsterID = monster->runtimeID();

			if (attackSet.count(monsterID) > 0)
				continue;

			if (monsterRepeatTimeMap.count(monsterID) > 0 && monsterRepeatTimeMap[monsterID] > currentTime)
				continue;

			auto combat = monster->combat();
			if (combat == nullptr)
				continue;

			auto combatProp = combat->fields._combatProperty_k__BackingField;
			if (combatProp == nullptr)
				continue;

			auto maxHP = app::SafeFloat_GetValue(nullptr, combatProp->fields.maxHP, nullptr);
			auto isLockHp = combatProp->fields.islockHP == nullptr || app::FixedBoolStack_get_value(combatProp->fields.islockHP, nullptr);
			auto isInvincible = combatProp->fields.isInvincible == nullptr || app::FixedBoolStack_get_value(combatProp->fields.isInvincible, nullptr);
			auto HP = app::SafeFloat_GetValue(nullptr, combatProp->fields.HP, nullptr);
			if (maxHP < 10 || HP < 2 || isLockHp || isInvincible)
				continue;

			if (m_OnlyTargeted && combat->fields._attackTarget.runtimeID != manager.avatar()->runtimeID())
				continue;

			if (manager.avatar()->distance(monster) > m_Range)
				continue;

			attackQueue.push(monster);
			attackSet.insert(monsterID);
		}

		if (attackQueue.empty())
			return;

		auto monster = attackQueue.front();
		attackQueue.pop();

		if (!monster->isLoaded())
		{
			// If monster entity isn't active means that it was unloaded (it happen when player teleport or moving fast)
			// And we don't have way to get id
			// So better to clear all queue, to prevent memory leak
			// This happen rarely, so don't give any performance issues
			std::queue<game::Entity*> empty;
			std::swap(attackQueue, empty);

			attackSet.clear();
			return;
		}

		attackSet.erase(monster->runtimeID());

		auto combat = monster->combat();
		auto maxHP = app::SafeFloat_GetValue(nullptr, combat->fields._combatProperty_k__BackingField->fields.maxHP, nullptr);

		auto crashEvt = app::CreateCrashEvent(nullptr, *app::CreateCrashEvent__MethodInfo);
		app::EvtCrash_Init(crashEvt, monster->runtimeID(), nullptr);
		crashEvt->fields.maxHp = maxHP;
		crashEvt->fields.velChange = 1000;
		crashEvt->fields.hitPos = monster->absolutePosition();

		app::EventManager_FireEvent(eventManager, reinterpret_cast<app::BaseEvent*>(crashEvt), false, nullptr);

		monsterRepeatTimeMap[monster->runtimeID()] = currentTime + (int)m_RepeatDelay + distribution(generator);

		nextAttackTime = currentTime + (int)m_AttackDelay + distribution(generator);
	}
}

