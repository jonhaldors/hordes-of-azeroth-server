#include "BonkPlayer.h"
#include "BonkMastery.h"
#include "BonkMgr.h"
#include "BonkQuests.h"
#include "Database/DatabaseEnv.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>

namespace
{
    bool IsDefaultStarter(BonkWeapon const& weapon, uint32 classMask)
    {
        return (weapon.starterClassMask & classMask) != 0;
    }

    std::string DescribeStarterUnlock(BonkMasteryNode const& node)
    {
        BonkMasteryTree const* tree = sBonkMastery.GetTree(node.tree);
        return "Learn " + node.name + (tree ? " in the " + tree->name + " talents" : std::string()) + " to unlock it.";
    }

    std::string DescribeSpellUnlock(uint32 weaponId)
    {
        if (BonkQuest const* quest = sBonkQuests.GetUnlockingQuest(BONK_REWARD_WEAPON, weaponId))
            return "Finish the quest " + quest->name + " first.";
        return "Unlock it first.";
    }
}

void BonkPlayer::LoadStarter()
{
    m_starterChoice = 0;
    std::unique_ptr<QueryResult> result(CharacterDatabase.PQuery("SELECT `weapon` FROM `bonk_account_starter` WHERE `account` = %u AND `class` = %u",
        m_player->GetSession()->GetAccountId(), uint32(m_player->GetClass())));
    if (result)
        m_starterChoice = result->Fetch()[0].GetUInt32();
}

std::vector<BonkPlayer::StarterOption> BonkPlayer::GetStarterOptions() const
{
    uint32 const classMask = m_player->GetClassMask();
    uint32 const account = m_player->GetSession()->GetAccountId();
    std::vector<StarterOption> options;
    for (auto const& itr : sBonkMgr.GetWeapons())
    {
        BonkWeapon const& weapon = itr.second;
        if (!((weapon.starterClassMask | weapon.starterOptionMask) & classMask))
            continue;

        StarterOption option;
        option.weapon = &weapon;
        option.unlocked = true;
        if (!IsDefaultStarter(weapon, classMask))
            if (BonkMasteryNode const* node = sBonkMastery.FindStarterUnlock(m_player->GetClass(), weapon.id))
                if (!GetMasteryRank(node->id))
                {
                    option.unlocked = false;
                    option.hint = DescribeStarterUnlock(*node);
                }
        if (option.unlocked && sBonkQuests.IsLocked(account, BONK_REWARD_WEAPON, weapon.id))
        {
            option.unlocked = false;
            option.hint = DescribeSpellUnlock(weapon.id);
        }
        options.push_back(option);
    }

    std::stable_sort(options.begin(), options.end(), [classMask](StarterOption const& a, StarterOption const& b)
    {
        bool const defaultA = IsDefaultStarter(*a.weapon, classMask);
        bool const defaultB = IsDefaultStarter(*b.weapon, classMask);
        if (defaultA != defaultB)
            return defaultA;
        return a.weapon->id < b.weapon->id;
    });
    return options;
}

BonkWeapon const* BonkPlayer::GetStarterWeapon() const
{
    std::vector<StarterOption> const options = GetStarterOptions();
    for (StarterOption const& option : options)
        if (option.unlocked && option.weapon->id == m_starterChoice)
            return option.weapon;
    return options.empty() ? nullptr : options.front().weapon;
}

bool BonkPlayer::ChooseStarter(uint32 weaponId, std::string& error)
{
    for (StarterOption const& option : GetStarterOptions())
    {
        if (option.weapon->id != weaponId)
            continue;
        if (!option.unlocked)
        {
            error = option.hint;
            return false;
        }
        m_starterChoice = weaponId;
        CharacterDatabase.PExecute("REPLACE INTO `bonk_account_starter` (`account`, `class`, `weapon`) VALUES (%u, %u, %u)",
            m_player->GetSession()->GetAccountId(), uint32(m_player->GetClass()), weaponId);
        SendStarters();
        return true;
    }
    error = "That spell can't be your starter spell.";
    return false;
}

void BonkPlayer::SendStarters() const
{
    BonkWeapon const* chosen = GetStarterWeapon();
    for (StarterOption const& option : GetStarterOptions())
    {
        BonkWeapon const& weapon = *option.weapon;
        SendAddon("starter~" + std::to_string(weapon.id) + "~" + weapon.icon + "~" + weapon.name + "~" + (option.unlocked ? "1" : "0") + "~" +
            (&weapon == chosen ? "1" : "0") + "~" + option.hint + "~" + TagBonkWeaponDescription(weapon, weapon.description));
    }
    SendAddon("starterend");
}
