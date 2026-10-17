#include "BonkRun.h"
#include "BonkTelegraph.h"
#include "Player.h"
#include "SpellMgr.h"

#include <algorithm>

namespace
{
    uint32 const BONK_HERO_LOOK_SPELLS[] =
    {
        33447,
        33448,
        33449,
        33450,
        33451,
        33452,
        33453,
    };

    uint32 const BONK_HERO_RING_FIRST_SPELLS[] =
    {
        33402,
        33425,
    };

    uint32 const BONK_OLD_BLOOD_AURA_LOOK = 33401;

    static_assert(sizeof(BONK_HERO_LOOK_SPELLS) / sizeof(uint32) == size_t(BonkHeroLook::Count), "Every hero look needs a spell.");
    static_assert(sizeof(BONK_HERO_RING_FIRST_SPELLS) / sizeof(uint32) == size_t(BonkHeroRing::Count), "Every hero ring needs a first spell.");
}

void BonkRun::ShowHeroLook(BonkHeroLook look, bool shown)
{
    uint32 const spellId = BONK_HERO_LOOK_SPELLS[size_t(look)];
    shown = shown && m_player->IsAlive();
    if (shown == m_player->HasAura(spellId))
        return;
    if (!shown)
        m_player->RemoveAurasDueToSpell(spellId);
    else if (sSpellMgr.GetSpellEntry(spellId))
        m_player->CastSpell(m_player, spellId, true);
}

uint32 BonkRun::GetRingSpell(BonkHeroRing ring, float reach) const
{
    return BONK_HERO_RING_FIRST_SPELLS[size_t(ring)] + GetBonkStandInSizeIndex(reach);
}

void BonkRun::ShowHeroRing(BonkHeroRing ring, float reach)
{
    uint32 const spellId = GetRingSpell(ring, reach / std::max(0.1f, m_player->GetObjectScale()));
    if (!m_player->IsAlive() || !sSpellMgr.GetSpellEntry(spellId))
        return;
    if (spellId == m_heroRingSpells[size_t(ring)] && m_player->HasAura(spellId))
        return;
    RemoveHeroRingAuras(ring);
    m_player->CastSpell(m_player, spellId, true);
    m_heroRingSpells[size_t(ring)] = spellId;
}

void BonkRun::HideHeroRing(BonkHeroRing ring)
{
    if (m_heroRingSpells[size_t(ring)])
        RemoveHeroRingAuras(ring);
}

void BonkRun::RemoveHeroRingAuras(BonkHeroRing ring)
{
    uint32 const firstSpell = BONK_HERO_RING_FIRST_SPELLS[size_t(ring)];
    for (uint32 i = 0; i < GetBonkStandInSizeCount(); ++i)
        m_player->RemoveAurasDueToSpell(firstSpell + i);
    m_heroRingSpells[size_t(ring)] = 0;
}

void BonkRun::RemoveHeroLooks()
{
    for (uint32 spellId : BONK_HERO_LOOK_SPELLS)
        m_player->RemoveAurasDueToSpell(spellId);
    for (size_t ring = 0; ring < size_t(BonkHeroRing::Count); ++ring)
        RemoveHeroRingAuras(BonkHeroRing(ring));
    m_player->RemoveAurasDueToSpell(BONK_OLD_BLOOD_AURA_LOOK);
}
