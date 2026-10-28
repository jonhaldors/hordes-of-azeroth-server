#include "BonkRun.h"
#include "BonkMgr.h"
#include "Creature.h"
#include "Item.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Utilities/Random.h"

#include <algorithm>

namespace
{
    float const BONK_TRINKET_CHANCE = 5.0f;

    uint32 const BONK_KIT_TRINKET_LIGHTNING = 283;
    uint32 const BONK_KIT_TRINKET_FIRE      = 728;
    uint32 const BONK_KIT_TRINKET_FROST     = 160;

    BonkTrinket const BONK_TRINKETS[MAX_BONK_TRINKET_EFFECT] =
    {
        { "Stormcaller's Fetish", "inv_jewelry_talisman_12", 16614, 0.8f, 20.0f, 4, 0,
          "Your spells have a 5% chance to strike up to 4 enemies near you with lightning for 80% of the spell's damage." },
        { "Smoldering Ember", "inv_misc_orb_03", 6305, 1.0f, 10.0f, 0, 0,
          "Your spells have a 5% chance to burst into flames around you, hitting enemies within 10 yards for 100% of the spell's damage." },
        { "Shard of Hastened Time", "inv_jewelry_talisman_11", 0, 20.0f, 0.0f, 0, 6000,
          "Your spells have a 5% chance to grant 20% Haste for 6 sec." },
        { "Bulwark Sigil", "inv_misc_rune_06", 0, 8.0f, 0.0f, 0, 0,
          "Your spells have a 5% chance to shield you for 8% of your maximum health." },
        { "Bloodvine Charm", "inv_jewelry_talisman_01", 0, 3.0f, 0.0f, 0, 0,
          "Your spells have a 5% chance to heal you for 3% of your maximum health." },
        { "Frostbound Idol", "inv_misc_ahnqirajtrinket_03", 22519, 0.7f, 8.0f, 0, 0,
          "Your spells have a 5% chance to freeze the air around the target, hitting enemies within 8 yards for 70% of the spell's damage." },
        { "Grinning Skull of Wrath", "inv_misc_bone_elfskull_01", 0, 20.0f, 0.0f, 0, 6000,
          "Your spells have a 5% chance to grant 20% Power for 6 sec." },
        { "Windrunner's Compass", "inv_jewelry_talisman_13", 0, 30.0f, 0.0f, 0, 4000,
          "Your spells have a 5% chance to make you run 30% faster for 4 sec." },
    };

    bool StrikesOnHit(BonkTrinketEffect effect)
    {
        return effect == BONK_TRINKET_STORM || effect == BONK_TRINKET_EMBER || effect == BONK_TRINKET_FROST;
    }
}

BonkTrinket const& BonkMgr::GetTrinket(BonkTrinketEffect effect)
{
    return BONK_TRINKETS[effect < MAX_BONK_TRINKET_EFFECT ? effect : 0];
}

uint32 BonkRun::GetWornTrinkets() const
{
    uint32 worn = 0;
    for (uint8 slot : { EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 })
    {
        BonkTrinketEffect effect;
        if (Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (BonkMgr::ReadTrinketEffect(item->GetEntry(), effect))
                worn |= 1u << effect;
    }
    return worn;
}

void BonkRun::ExpireTrinketSpeed()
{
    if (!m_trinketSpeedUntilMs || m_totalMs < m_trinketSpeedUntilMs)
        return;
    m_trinketSpeedUntilMs = 0;
    ApplyPassives();
}

void BonkRun::RollTrinketCharges(OwnedWeapon& owned)
{
    if (owned.weapon.IsPassiveTrigger() || int32(m_totalMs - owned.trinketRollAtMs) < 0)
        return;
    owned.trinketRollAtMs = m_totalMs + GetWeaponCooldown(owned);
    owned.trinketCharges = 0;
    uint32 const worn = GetWornTrinkets();
    for (uint8 index = 0; index < MAX_BONK_TRINKET_EFFECT; ++index)
    {
        BonkTrinketEffect const effect = BonkTrinketEffect(index);
        if (!(worn & (1u << effect)) || !roll_chance_f(BONK_TRINKET_CHANCE))
            continue;
        if (StrikesOnHit(effect))
        {
            owned.trinketCharges |= 1u << effect;
            continue;
        }

        BonkTrinket const& trinket = BonkMgr::GetTrinket(effect);
        SupportScope scope(*this, { trinket.icon, trinket.name });
        uint32 const maxHealth = m_player->GetMaxHealth();
        switch (effect)
        {
            case BONK_TRINKET_HASTE:
                m_trinketHasteUntilMs = m_totalMs + trinket.durationMs;
                break;
            case BONK_TRINKET_WRATH:
                m_trinketPowerUntilMs = m_totalMs + trinket.durationMs;
                break;
            case BONK_TRINKET_WIND:
            {
                bool const starting = !m_trinketSpeedUntilMs;
                m_trinketSpeedUntilMs = m_totalMs + trinket.durationMs;
                if (starting)
                    ApplyPassives();
                break;
            }
            case BONK_TRINKET_BULWARK:
                AddSpellShield(uint32(maxHealth * trinket.value / 100.0f));
                break;
            case BONK_TRINKET_BLOODVINE:
                HealHero(ReduceHealing(uint32(maxHealth * trinket.value / 100.0f)));
                break;
            default:
                break;
        }
    }
}

void BonkRun::OnTrinketHit(Unit* victim, uint32 damage, SpellEntry const* spell)
{
    OwnedWeapon* owned = spell ? const_cast<OwnedWeapon*>(FindOwnedBySpell(spell->Id)) : nullptr;
    if (!damage || !owned || !owned->trinketCharges || !victim)
        return;

    float const multiplier = std::max(0.1f, GetDamageMultiplier(nullptr, victim)) * GetCastChargeMultiplier(spell, victim);
    float const castScale = owned->weapon.HasFlag(BONK_WEAPON_FLAG_AOE) ? 1.0f : 1.0f + float(GetSpellCount(*owned));
    float const base = damage * castScale / multiplier;

    for (uint8 index = 0; index < MAX_BONK_TRINKET_EFFECT; ++index)
    {
        BonkTrinketEffect const effect = BonkTrinketEffect(index);
        uint32 const bit = 1u << effect;
        if (!(owned->trinketCharges & bit))
            continue;
        owned->trinketCharges &= ~bit;

        BonkTrinket const& trinket = BonkMgr::GetTrinket(effect);
        float const strike = base * trinket.value;
        if (effect == BONK_TRINKET_STORM)
        {
            std::vector<Creature*> mobs = GetMobsInRange(trinket.radius, m_player);
            if (mobs.size() > trinket.targets)
            {
                for (uint32 i = 0; i < trinket.targets; ++i)
                    std::swap(mobs[i], mobs[urand(i, uint32(mobs.size() - 1))]);
                mobs.resize(trinket.targets);
            }
            StrikeWithTrinket(effect, mobs, strike, BONK_KIT_TRINKET_LIGHTNING);
        }
        else if (effect == BONK_TRINKET_EMBER)
            StrikeWithTrinket(effect, GetMobsInRange(trinket.radius, m_player), strike, BONK_KIT_TRINKET_FIRE);
        else if (effect == BONK_TRINKET_FROST)
        {
            std::vector<Creature*> mobs = GetMobsInRange(trinket.radius, victim);
            if (victim->IsAlive() && victim->IsCreature() && std::find(mobs.begin(), mobs.end(), victim) == mobs.end())
                mobs.push_back(static_cast<Creature*>(victim));
            victim->SendPlaySpellVisualKit(BONK_KIT_TRINKET_FROST);
            StrikeWithTrinket(effect, mobs, strike, 0);
        }
    }
}

void BonkRun::StrikeWithTrinket(BonkTrinketEffect effect, std::vector<Creature*> const& targets, float damage, uint32 kit)
{
    SpellEntry const* spellInfo = sSpellMgr.GetSpellEntry(BonkMgr::GetTrinket(effect).spellId);
    if (!spellInfo || damage <= 0.0f)
        return;
    for (Creature* mob : targets)
    {
        if (!mob->IsAlive())
            continue;
        if (kit)
            mob->SendPlaySpellVisualKit(kit);
        DealSpellHit(spellInfo, mob, damage);
    }
}

float BonkRun::GetTrinketHaste() const
{
    return m_trinketHasteUntilMs > m_totalMs ? BonkMgr::GetTrinket(BONK_TRINKET_HASTE).value : 0.0f;
}

float BonkRun::GetTrinketPower() const
{
    return m_trinketPowerUntilMs > m_totalMs ? BonkMgr::GetTrinket(BONK_TRINKET_WRATH).value : 0.0f;
}

float BonkRun::GetTrinketMoveSpeed() const
{
    return m_trinketSpeedUntilMs ? BonkMgr::GetTrinket(BONK_TRINKET_WIND).value : 0.0f;
}
