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

#include <memory>
#include <string>

class BonkRun;
class Player;
struct BonkMap;

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
        bool StartRun(BonkMap const& map, std::string& error);
        void StopRun();

        void SetAddonReady() { m_addonReady = true; }
        bool IsAddonReady() const { return m_addonReady; }

        // Protocol line for the addon; dropped when the addon is not loaded.
        void SendAddon(std::string const& line) const;
        // Short message: a center-screen notice with the addon, chat without it.
        void Notify(std::string const& text) const;
        void SendMapList() const;

        // Level 1, alive, full health: the state a character has outside a run.
        void ResetCharacter();
        bool IsAtCamp() const;
        void TeleportToCamp();

    private:
        Player* m_player;
        std::unique_ptr<BonkRun> m_run;
        bool m_addonReady = false;
        bool m_campCheckPending = false;
        bool m_goldLoaded = false;
};

#endif
