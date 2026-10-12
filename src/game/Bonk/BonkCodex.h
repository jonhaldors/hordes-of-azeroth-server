/*
 * Vanilla Bonk: the spellbook.
 *
 * Every spell (bonk_weapon), passive (bonk_passive) and evolution a hero can
 * find in a run, with how each grows and what still waits on a quest. The
 * spellbook NPC at camp, or /spellbook, opens it in the addon.
 */

#ifndef MANGOS_BONKCODEX_H
#define MANGOS_BONKCODEX_H

#include <string>

class Player;

namespace BonkCodex
{
    // The whole book for the addon: "cdx~npc~classMask", a few lines per
    // spell and passive, then "cdxend". `npc` is empty when no NPC opened it.
    void Send(Player* player, std::string const& npc);
}

#endif
