/*
 * Vanilla Bonk: one run. The character starts at level 1 in an arena,
 * enemies spawn around them in growing waves, weapons fire on their own,
 * and every level up pauses the run to offer three upgrades. Surviving
 * until the boss arrives and killing it wins the run; gold earned is kept
 * either way.
 */

#ifndef MANGOS_BONKRUN_H
#define MANGOS_BONKRUN_H

#include "BonkMgr.h"
#include "ObjectGuid.h"

#include <map>
#include <string>
#include <vector>

class Creature;
class Player;

class BonkRun
{
    public:
        BonkRun(Player* player, BonkMap const& map);

        bool Begin(std::string& error);
        void Update(uint32 diff);
        void Abort();       // logout: clean up without moving the player
        void Surrender();   // `.bonk stop`

        bool IsActive() const { return m_state == STATE_RUNNING; }
        bool IsPaused() const { return m_paused; }
        bool IsFinished() const { return m_state == STATE_FINISHED; }
        BonkMap const& GetMap() const { return m_map; }

        void OnMobKilled(Creature* mob, BonkSpawnKind kind);
        void OnMobStrayed(Creature* mob, BonkSpawnKind kind);
        bool Pick(uint32 choice, std::string& error);

        // Everything the addon shows, for when it (re)connects mid-run.
        void SendFullState() const;

    private:
        enum State
        {
            STATE_ENTERING,     // teleporting into the arena
            STATE_RUNNING,
            STATE_ENDING,       // results on screen, about to return to camp
            STATE_FINISHED,
        };

        struct OwnedWeapon
        {
            BonkWeapon weapon;
            uint8 level = 1;
            int32 timer = 0;
        };

        struct Offer
        {
            bool isWeapon = true;
            uint32 id = 0;
            uint8 newLevel = 1;
        };

        void StartRunning();
        void UpdateRunning(uint32 diff);
        void UpdateSpawns(uint32 diff);
        void UpdateWeapons(uint32 diff);
        void UpdateBoundary(uint32 diff);
        void UpdateRecovery(uint32 diff);

        void PruneMobs();
        BonkMapCreature const* PickCreature(BonkSpawnKind kind) const;
        bool FindSpawnPosition(float& x, float& y, float& z) const;
        void SpawnMob(BonkMapCreature const& creature);
        std::vector<Creature*> GetMobsInRange(float range) const;
        void DespawnMobs();

        uint32 GetMobLevel() const;
        float GetMobHealth(uint32 level) const;
        float GetPassiveTotal(BonkPassiveEffect effect) const;
        float GetWeaponDamage(OwnedWeapon const& owned) const;
        uint32 GetWeaponCooldown(OwnedWeapon const& owned) const;
        void FireWeapon(OwnedWeapon& owned);
        void AddWeapon(BonkWeapon const& weapon);
        void ApplyPassives();

        void GiveExperience(uint32 xp);
        void GiveGold(uint32 copper);

        void OfferUpgrades();
        std::string DescribeOffer(Offer const& offer, std::string* icon, uint8* maxLevel) const;

        void SendState() const;
        void SendLoadout() const;
        void SendOffer() const;

        void Finish(bool victory);
        void Exit();

        Player* m_player;
        BonkMap const m_map;
        State m_state = STATE_ENTERING;
        bool m_paused = false;
        bool m_victory = false;

        uint32 m_enterTimer = 0;
        uint32 m_elapsedMs = 0;
        int32 m_spawnTimer = 0;
        int32 m_eliteTimer = 0;
        uint32 m_boundaryTimer = 0;
        uint32 m_recoveryTimer = 0;
        uint32 m_stateTimer = 0;
        int32 m_endTimer = 0;

        bool m_bossSpawned = false;
        ObjectGuid m_bossGuid;
        std::vector<ObjectGuid> m_mobs;

        uint32 m_kills = 0;
        uint32 m_goldEarned = 0;
        uint32 m_lastLevel = 1;
        uint32 m_pendingLevels = 0;

        std::vector<OwnedWeapon> m_weapons;
        std::map<uint32, uint8> m_passiveRanks;
        std::vector<Offer> m_offer;

        float m_safeX = 0.0f, m_safeY = 0.0f, m_safeZ = 0.0f;
};

#endif
