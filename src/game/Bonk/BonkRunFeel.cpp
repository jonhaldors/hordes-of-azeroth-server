#include "BonkRun.h"
#include "BonkMgr.h"
#include "Creature.h"
#include "Item.h"
#include "Player.h"
#include "Timer.h"

namespace
{
    uint32 const BONK_KIT_HIT_DAGGER        = 7505;
    uint32 const BONK_KIT_HIT_SWORD         = 7506;
    uint32 const BONK_KIT_HIT_SWORD2        = 7507;
    uint32 const BONK_KIT_HIT_AXE           = 7508;
    uint32 const BONK_KIT_HIT_AXE2          = 7509;
    uint32 const BONK_KIT_HIT_MACE          = 7510;
    uint32 const BONK_KIT_HIT_MACE2         = 7511;
    uint32 const BONK_KIT_HIT_UNARMED       = 7512;
    uint32 const BONK_KIT_SHAKE_LIGHT       = 7513;
    uint32 const BONK_KIT_SHAKE_HEAVY       = 7514;
    uint32 const BONK_KIT_KILL_BURST        = 220;

    uint32 const BONK_MELEE_HIT_SOUND_GAP_MS = 100;
    uint32 const BONK_KILL_BURST_GAP_MS      = 120;
    float const BONK_KILL_BURST_OVERKILL     = 1.0f;
    uint32 const BONK_CAMERA_SHAKE_GAP_MS    = 400;
    float const BONK_LIGHT_SHAKE_HIT_SHARE   = 0.10f;
    float const BONK_HEAVY_SHAKE_HIT_SHARE   = 0.25f;
    int32 const BONK_WEAPON_GLOW_CHECK_MS    = 500;

    struct BonkWeaponGlow
    {
        uint32 weaponId;
        uint32 enchantId;
    };

    BonkWeaponGlow const BONK_WEAPON_GLOWS[] =
    {
        { 605, 7 },
        { 622, 22 },
        { 623, 323 },
        { 704, 283 },
        { 720, 5 },
        { 721, 283 },
    };

    uint32 GetGlowEnchant(uint32 weaponId)
    {
        for (BonkWeaponGlow const& glow : BONK_WEAPON_GLOWS)
            if (glow.weaponId == weaponId)
                return glow.enchantId;
        return 0;
    }

    bool HasPassed(uint32& lastMs, uint32 gapMs)
    {
        uint32 const now = WorldTimer::getMSTime();
        if (lastMs && WorldTimer::getMSTimeDiff(lastMs, now) < gapMs)
            return false;
        lastMs = now;
        return true;
    }

    uint32 GetVisibleTempEnchantField(uint8 slot)
    {
        return PLAYER_VISIBLE_ITEM_1_0 + slot * MAX_VISIBLE_ITEM_OFFSET + 1 + TEMP_ENCHANTMENT_SLOT;
    }
}

uint32 BonkRun::GetMeleeHitSoundKit() const
{
    Item const* weapon = m_player->GetWeaponForAttack(BASE_ATTACK, true, true);
    if (!weapon)
        return BONK_KIT_HIT_UNARMED;
    switch (weapon->GetProto()->SubClass)
    {
        case ITEM_SUBCLASS_WEAPON_DAGGER:  return BONK_KIT_HIT_DAGGER;
        case ITEM_SUBCLASS_WEAPON_SWORD:   return BONK_KIT_HIT_SWORD;
        case ITEM_SUBCLASS_WEAPON_SWORD2:  return BONK_KIT_HIT_SWORD2;
        case ITEM_SUBCLASS_WEAPON_AXE:     return BONK_KIT_HIT_AXE;
        case ITEM_SUBCLASS_WEAPON_AXE2:
        case ITEM_SUBCLASS_WEAPON_POLEARM: return BONK_KIT_HIT_AXE2;
        case ITEM_SUBCLASS_WEAPON_MACE:    return BONK_KIT_HIT_MACE;
        case ITEM_SUBCLASS_WEAPON_MACE2:
        case ITEM_SUBCLASS_WEAPON_STAFF:   return BONK_KIT_HIT_MACE2;
        default:                           return BONK_KIT_HIT_UNARMED;
    }
}

void BonkRun::PlayMeleeHitFeel(Unit* victim, SpellEntry const* spell)
{
    if (!spell || spell->DmgClass != SPELL_DAMAGE_CLASS_MELEE || m_player->GetShapeshiftForm() != FORM_NONE)
        return;
    if (HasPassed(m_meleeHitSoundMs, BONK_MELEE_HIT_SOUND_GAP_MS))
        victim->SendPlaySpellVisualKit(GetMeleeHitSoundKit());
}

void BonkRun::PlayKillFeel(Unit* victim, uint32 damage, uint32 health)
{
    float const overkill = float(damage) - float(health);
    if (overkill < victim->GetMaxHealth() * BONK_KILL_BURST_OVERKILL)
        return;
    if (HasPassed(m_killBurstMs, BONK_KILL_BURST_GAP_MS))
        victim->SendPlaySpellVisualKit(BONK_KIT_KILL_BURST);
}

void BonkRun::ShakeCamera(bool heavy)
{
    if (HasPassed(m_cameraShakeMs, BONK_CAMERA_SHAKE_GAP_MS))
        m_player->SendPlaySpellVisualKit(heavy ? BONK_KIT_SHAKE_HEAVY : BONK_KIT_SHAKE_LIGHT);
}

void BonkRun::ShakeOnKill(BonkSpawnKind kind)
{
    if (kind == BONK_SPAWN_BOSS)
        ShakeCamera(true);
    else if (kind == BONK_SPAWN_MINIBOSS)
        ShakeCamera(false);
}

void BonkRun::ShakeOnHeroHit(uint32 damage)
{
    float const share = float(damage) / float(std::max(1u, m_player->GetMaxHealth()));
    if (share >= BONK_HEAVY_SHAKE_HIT_SHARE)
        ShakeCamera(true);
    else if (share >= BONK_LIGHT_SHAKE_HIT_SHARE)
        ShakeCamera(false);
}

void BonkRun::UpdateWeaponGlows(uint32 diff)
{
    m_weaponGlowTimer -= int32(diff);
    if (m_weaponGlowTimer > 0)
        return;
    m_weaponGlowTimer = BONK_WEAPON_GLOW_CHECK_MS;

    std::vector<uint32> glows;
    for (OwnedWeapon const& owned : m_weapons)
        if (uint32 const enchant = GetGlowEnchant(owned.weapon.id))
            glows.push_back(enchant);
    ShowWeaponGlow(EQUIPMENT_SLOT_MAINHAND, glows.size() > 0 ? glows[0] : 0);
    ShowWeaponGlow(EQUIPMENT_SLOT_OFFHAND, glows.size() > 1 ? glows[1] : glows.size() > 0 ? glows[0] : 0);
}

void BonkRun::ShowWeaponGlow(uint8 slot, uint32 enchantId)
{
    Item const* item = m_player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!item || item->GetProto()->Class != ITEM_CLASS_WEAPON)
        return;
    uint32 const shown = enchantId ? enchantId : item->GetEnchantmentId(TEMP_ENCHANTMENT_SLOT);
    uint32 const field = GetVisibleTempEnchantField(slot);
    if (m_player->GetUInt32Value(field) != shown)
        m_player->SetUInt32Value(field, shown);
}

void BonkRun::ClearWeaponGlows()
{
    ShowWeaponGlow(EQUIPMENT_SLOT_MAINHAND, 0);
    ShowWeaponGlow(EQUIPMENT_SLOT_OFFHAND, 0);
}
