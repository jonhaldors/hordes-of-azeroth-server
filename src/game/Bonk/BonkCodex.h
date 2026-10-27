#ifndef MANGOS_BONKCODEX_H
#define MANGOS_BONKCODEX_H

#include <string>

class BonkPlayer;
class Player;

namespace BonkCodex
{
    void Send(Player* player, std::string const& npc, bool deck = false);
    void SendKeywordGlossary(BonkPlayer* bonk);
}

#endif
