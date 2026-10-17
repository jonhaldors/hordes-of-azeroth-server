#ifndef MANGOS_BONKCODEX_H
#define MANGOS_BONKCODEX_H

#include <string>

class Player;

namespace BonkCodex
{
    void Send(Player* player, std::string const& npc);
}

#endif
