/*
 * Vanilla Bonk: one run. The character starts at level 1 and plays a chain
 * of stages, each on a map from the chosen tier's pool. Enemies spawn
 * around them in growing waves, weapons fire on their own, and every level
 * up or opened chest pauses the run to offer three upgrades. When a stage's
 * timer runs out its boss arrives; killing it stops the spawns and opens a
 * portal to the next, harder stage. The run is endless: it ends when the
 * character dies or goes home through a portal. Gold earned is kept either
 * way.
 */

#ifndef MANGOS_BONKRUN_H
#define MANGOS_BONKRUN_H

#include "BonkMgr.h"
#include "ObjectGuid.h"

#include <map>
#include <set>
#include <string>
#include <vector>

class Creature;
class GameObject;
class Player;

class BonkRun
{
    public:
        BonkRun(Player* player, BonkTier const& tier);

        bool Begin(std::string& error);
        void Update(uint32 diff);
        void Abort();       // logout: clean up without moving the player
        void Surrender();   // `.bonk stop`

        bool IsActive() const { return m_state == STATE_RUNNING; }
        bool IsPaused() const { return m_paused; }
        bool IsFinished() const { return m_state == STATE_FINISHED; }
        bool IsPortalOpen() const { return m_state == STATE_PORTAL; }
        BonkMap const& GetMap() const { return m_map; }
        BonkTier const& GetTier() const { return m_tier; }
        uint32 GetStage() const { return m_stage; }

        void OnMobKilled(Creature* mob, BonkSpawnKind kind);
        void OnMobStrayed(Creature* mob, BonkSpawnKind kind);
        bool Pick(uint32 choice, std::string& error);

        // Chests and the portal call these when the player uses them.
        void UseChest(GameObject* chest);
        bool IsOwnPortal(GameObject const* portal) const;
        BonkMap const* GetNextMap() const;
        void EnterNextStage();
        void GoHome();

        // Everything the addon shows, for when it (re)connects mid-run.
        void SendFullState() const;

    private:
        enum State
        {
            STATE_ENTERING,     // teleporting into a stage
            STATE_RUNNING,
            STATE_PORTAL,       // stage boss dead, waiting at the portal
            STATE_ENDING,       // results on screen, about to return to camp
            STATE_FINISHED,
        };

        enum OfferSource
        {
            OFFER_LEVEL,
            OFFER_CHEST,
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

        BonkMap const* ChooseNextMap();
        bool TeleportToStage(BonkMap const& map, std::string& error);
        void StartStage();
        void UpdateRunning(uint32 diff);
        void UpdateOffers();
        void UpdateEvents();
        void UpdateSpawns(uint32 diff);
        void UpdateWeapons(uint32 diff);
        void UpdateBoundary(uint32 diff);
        void UpdateRecovery(uint32 diff);
        void RunEvent(BonkMapEvent const& event);
        void OpenPortal(Creature* boss);
        void PlacePortal(float x, float y, float baseZ);

        void PruneMobs();
        BonkMapCreature const* PickCreature(BonkSpawnKind kind) const;
        bool FindSpawnPosition(float& x, float& y, float& z, float minDistance, float maxDistance) const;
        Creature* SpawnMob(BonkMapCreature const& creature);
        std::vector<Creature*> GetMobsInRange(float range) const;
        void DespawnMobs();

        GameObject* PlaceObject(uint32 entry, float x, float y, float z);
        bool SpawnChest(bool free, float x, float y, float z);
        bool SpawnChestNearby();
        uint32 GetChestCost() const;
        void DespawnObjects();

        uint32 GetMobLevel() const;
        float GetMobHealth(uint32 level) const;
        float GetStageHealthMultiplier() const;
        float GetStageDamageMultiplier() const;
        float GetPassiveTotal(BonkPassiveEffect effect) const;
        float GetWeaponDamage(OwnedWeapon const& owned) const;
        uint32 GetWeaponCooldown(OwnedWeapon const& owned) const;
        void FireWeapon(OwnedWeapon& owned);
        void AddWeapon(BonkWeapon const& weapon);
        void ApplyPassives();

        void GiveExperience(uint32 xp);
        void GiveGold(uint32 copper);

        bool OfferUpgrades(OfferSource source);
        std::string DescribeOffer(Offer const& offer, std::string* icon, uint8* maxLevel) const;

        void SendState() const;
        void SendLoadout() const;
        void SendOffer() const;

        void Finish(bool wentHome);
        void Exit();
        void CleanUp();

        Player* m_player;
        BonkTier const m_tier;
        BonkMap m_map;
        uint32 m_stage = 1;
        uint32 m_nextMapId = 0;
        std::set<uint32> m_playedMaps;      // this lap through the tier's pool
        State m_state = STATE_ENTERING;
        bool m_paused = false;
        bool m_wentHome = false;

        uint32 m_enterTimer = 0;
        uint32 m_elapsedMs = 0;             // this stage
        uint32 m_totalMs = 0;               // the whole run
        size_t m_nextEvent = 0;
        int32 m_spawnTimer = 0;
        int32 m_eliteTimer = 0;
        uint32 m_boundaryTimer = 0;
        uint32 m_recoveryTimer = 0;
        uint32 m_stateTimer = 0;
        int32 m_endTimer = 0;

        bool m_bossSpawned = false;
        ObjectGuid m_bossGuid;
        std::vector<ObjectGuid> m_mobs;
        std::vector<ObjectGuid> m_chests;
        ObjectGuid m_portalGuid;

        uint32 m_kills = 0;
        uint32 m_goldEarned = 0;
        uint32 m_coins = 0;
        uint32 m_chestsBought = 0;
        uint32 m_lastLevel = 1;
        uint32 m_pendingLevels = 0;
        uint32 m_pendingChests = 0;

        std::vector<OwnedWeapon> m_weapons;
        std::map<uint32, uint8> m_passiveRanks;
        std::vector<Offer> m_offer;
        OfferSource m_offerSource = OFFER_LEVEL;

        float m_safeX = 0.0f, m_safeY = 0.0f, m_safeZ = 0.0f;
};

#endif
