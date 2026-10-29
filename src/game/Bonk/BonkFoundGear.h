#ifndef BONK_FOUND_GEAR_H
#define BONK_FOUND_GEAR_H

#include "Common.h"
#include "BonkMgr.h"
#include "Policies/Singleton.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Item;
class Player;

uint8 const BONK_FOUND_MAX_LINES = 2;
float const BONK_FOUND_TIER_GROWTH = 1.12f;
uint32 const BONK_FOUND_AWAKEN_COPPER = 5000000;

struct BonkFoundLine
{
    uint8 effect = 0;
    float roll = 0.0f;
};

struct BonkFoundGear
{
    uint32 tier = 0;
    std::vector<BonkFoundLine> lines;
};

class BonkFoundStore
{
    public:
        void Load();
        bool Get(uint32 itemGuid, BonkFoundGear& found) const;
        bool IsFound(uint32 itemGuid) const;
        void Set(uint32 itemGuid, BonkFoundGear const& found);
        void Forget(uint32 itemGuid);

    private:
        mutable std::mutex m_lock;
        std::unordered_map<uint32, BonkFoundGear> m_items;
};

#define sBonkFound MaNGOS::Singleton<BonkFoundStore>::Instance()

namespace BonkFound
{
    BonkFoundGear Roll(uint32 floorTier);
    BonkFoundGear Make(uint32 tier, uint32 lineCount);
    float LineValue(BonkFoundLine const& line, uint32 tier);
    uint32 GetItemTier(Item const* item);
    uint32 GetUpgradeCost(uint32 tier);
    bool IsAwakening(uint32 tier);
    std::string DescribeTier(uint32 tier);
    void AddWornStats(Item const* item, float* passives);
    std::string DescribeLines(BonkFoundGear const& found, uint32 tier);
    std::string DescribeExtra(Item const* item, uint32 tier);
    void Remember(Item const* item, BonkFoundGear const& found);
}

#endif
