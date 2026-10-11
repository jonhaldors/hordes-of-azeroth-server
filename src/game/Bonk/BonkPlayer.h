/*
 * Vanilla Bonk: per-player game-mode state. Every Player owns one.
 *
 * Talks to the VanillaBonk client addon with system messages that start
 * with "BONK~" (the addon hides them from chat). The addon answers with
 * `.bonk` chat commands.
 */

#ifndef MANGOS_BONKPLAYER_H
#define MANGOS_BONKPLAYER_H

#include "Common.h"

#include <map>
#include <memory>
#include <string>

class BonkRun;
class Player;
struct BonkTier;

class BonkPlayer
{
    public:
        explicit BonkPlayer(Player* player);
        ~BonkPlayer();

        void OnLogin();
        void OnLogout();
        void OnSave();
        void Update(uint32 diff);

        BonkRun* GetRun() const { return m_run.get(); }
        bool StartRun(BonkTier const& tier, std::string& error);
        void StopRun();

        // Tier progress is shared by every character on the account.
        uint32 GetBestStage(uint32 tierId) const;
        bool IsTierUnlocked(BonkTier const& tier) const;
        std::string GetUnlockHint(BonkTier const& tier) const;
        void RecordStage(uint32 tierId, uint32 stage);

        // This character's record over all its runs, shown on the hero screen.
        void RecordRun(uint32 stage, uint32 seconds, uint32 kills, uint32 goldEarned);

        void SetAddonReady() { m_addonReady = true; }
        bool IsAddonReady() const { return m_addonReady; }

        // Protocol line for the addon; dropped when the addon is not loaded.
        void SendAddon(std::string const& line) const;
        // Short message: a center-screen notice with the addon, chat without it.
        void Notify(std::string const& text) const;
        void SendTierList() const;

        // Level 1, alive, full health: the state a character has outside a run.
        void ResetCharacter();
        bool IsAtCamp() const;
        void TeleportToCamp();

    private:
        struct Record
        {
            uint32 runs = 0;
            uint32 bestStage = 0;
            uint32 bestSeconds = 0;
            uint32 kills = 0;
            uint32 goldEarned = 0;
        };

        // Writes every hero's record for the client's hero screen (BonkStats.lua).
        void WriteHeroScreenStats() const;

        Player* m_player;
        std::unique_ptr<BonkRun> m_run;
        bool m_addonReady = false;
        bool m_campCheckPending = false;
        bool m_goldLoaded = false;
        std::map<uint32, uint32> m_bestStages;  // tier id -> furthest stage reached
        Record m_record;
};

#endif
