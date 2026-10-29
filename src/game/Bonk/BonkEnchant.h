#ifndef BONK_ENCHANT_H
#define BONK_ENCHANT_H

#include "Common.h"
#include "Policies/Singleton.h"

#include <map>
#include <mutex>
#include <string>

class Player;

uint32 const BONK_ENCHANTER_ENTRY = 900160;
uint8 const BONK_ENCHANT_NONE = 255;

struct BonkEnchant
{
    uint8 effect = BONK_ENCHANT_NONE;
    uint8 grade = 0;
    uint32 tier = 1;
    uint32 rolls = 0;

    bool IsSet() const { return effect != BONK_ENCHANT_NONE; }
};

class BonkEnchantStore
{
    public:
        void Open(Player* player);
        bool Roll(Player* player, uint8 slot, std::string& error);
        bool Keep(Player* player, std::string& error);
        void Discard(Player* player);
        void AddStats(Player const* player, float* passives);
        void Send(Player const* player);

    private:
        typedef std::map<uint8, BonkEnchant> SlotEnchants;
        struct PendingRoll
        {
            uint8 slot = 0;
            BonkEnchant enchant;
        };

        SlotEnchants& LoadLocked(uint32 guid);
        void SaveLocked(uint32 guid, uint8 slot, BonkEnchant const& enchant);

        std::mutex m_lock;
        std::map<uint32, SlotEnchants> m_enchants;
        std::map<uint32, PendingRoll> m_pending;
};

#define sBonkEnchants MaNGOS::Singleton<BonkEnchantStore>::Instance()

namespace BonkEnchanting
{
    bool IsEnchantSlot(uint8 slot);
    uint32 GetRollCost(Player const* player);
    float GetValue(BonkEnchant const& enchant);
}

#endif
