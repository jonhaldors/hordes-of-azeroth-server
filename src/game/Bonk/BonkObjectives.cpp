#include "BonkObjectives.h"

#include "BonkPlayer.h"
#include "GameObject.h"
#include "Map.h"
#include "Player.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace
{
    // gameobject_template: the pillar for each kind, BONK_GO_OBJECTIVE + kind.
    uint32 const BONK_GO_OBJECTIVE = 900020;
    // A pillar outlives any stage; it is taken away with its objective.
    uint32 const BONK_OBJECTIVE_PILLAR_SEC = 3600;
    // Seen from this far, well past the map's usual sight (100 yards on
    // continents), the way large objects are.
    float const BONK_OBJECTIVE_SIGHT = VISIBILITY_DISTANCE_GIGANTIC;
    // The hero this close takes the pillar down, this far puts it back up.
    float const BONK_PILLAR_LOWER = 25.0f;
    float const BONK_PILLAR_RAISE = 40.0f;
    uint32 const BONK_PILLAR_CHECK_MS = 500;

    float DistanceSq(Player const* hero, BonkObjective const& objective)
    {
        float const dx = hero->GetPositionX() - objective.x;
        float const dy = hero->GetPositionY() - objective.y;
        return dx * dx + dy * dy;
    }

    char const* const KIND_NAMES[MAX_BONK_OBJECTIVE_KIND] = { "shrine", "vendor", "event", "extract" };

    std::string FormatCoordinate(float value)
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f", value);
        return text;
    }
}

char const* GetBonkObjectiveKindName(BonkObjectiveKind kind)
{
    return kind < MAX_BONK_OBJECTIVE_KIND ? KIND_NAMES[kind] : "";
}

bool ParseBonkObjectiveKind(char const* name, BonkObjectiveKind& kind)
{
    for (uint8 i = 0; i < MAX_BONK_OBJECTIVE_KIND; ++i)
        if (!strcmp(name, KIND_NAMES[i]))
        {
            kind = BonkObjectiveKind(i);
            return true;
        }
    return false;
}

uint32 BonkObjectives::Add(Player* hero, BonkObjectiveKind kind, float x, float y, float z, std::string const& label, uint32 seconds,
    std::string const& icon, std::string const& description)
{
    BonkObjective objective;
    objective.id = m_nextId++;
    objective.kind = kind;
    objective.x = x;
    objective.y = y;
    objective.z = z;
    objective.label = label;
    objective.icon = icon;
    objective.description = description;
    objective.leftMs = seconds * IN_MILLISECONDS;
    if (DistanceSq(hero, objective) > BONK_PILLAR_LOWER * BONK_PILLAR_LOWER)
        RaisePillar(hero, objective);

    m_objectives.push_back(objective);
    Send(hero, objective);
    return objective.id;
}

void BonkObjectives::Remove(Player* hero, uint32 id)
{
    auto const itr = std::find_if(m_objectives.begin(), m_objectives.end(), [id](BonkObjective const& objective) { return objective.id == id; });
    if (itr == m_objectives.end())
        return;

    Despawn(hero, *itr);
    hero->GetBonkPlayer()->SendAddon("objectiveclear~" + std::to_string(id));
    m_objectives.erase(itr);
}

void BonkObjectives::Clear(Player* hero)
{
    for (BonkObjective const& objective : m_objectives)
        Despawn(hero, objective);
    m_objectives.clear();
    hero->GetBonkPlayer()->SendAddon("objectiveclear");
}

std::vector<BonkObjective> BonkObjectives::Update(Player* hero, uint32 diff)
{
    m_pillarTimer += diff;
    if (m_pillarTimer >= BONK_PILLAR_CHECK_MS)
    {
        m_pillarTimer = 0;
        for (BonkObjective& objective : m_objectives)
        {
            float const distanceSq = DistanceSq(hero, objective);
            if (!objective.pillar.IsEmpty() && distanceSq < BONK_PILLAR_LOWER * BONK_PILLAR_LOWER)
            {
                Despawn(hero, objective);
                objective.pillar.Clear();
            }
            else if (objective.pillar.IsEmpty() && distanceSq > BONK_PILLAR_RAISE * BONK_PILLAR_RAISE)
                RaisePillar(hero, objective);
        }
    }

    std::vector<BonkObjective> gone;
    for (BonkObjective& objective : m_objectives)
    {
        if (!objective.leftMs)
            continue;
        if (objective.leftMs > diff)
            objective.leftMs -= diff;
        else
            gone.push_back(objective);
    }
    for (BonkObjective const& objective : gone)
        Remove(hero, objective.id);
    return gone;
}

void BonkObjectives::Resend(Player* hero) const
{
    hero->GetBonkPlayer()->SendAddon("objectiveclear");
    for (BonkObjective const& objective : m_objectives)
        Send(hero, objective);
}

BonkObjective const* BonkObjectives::Get(uint32 id) const
{
    for (BonkObjective const& objective : m_objectives)
        if (objective.id == id)
            return &objective;
    return nullptr;
}

void BonkObjectives::Send(Player* hero, BonkObjective const& objective) const
{
    // Seconds rounded up, so a countdown never shows 0 while it stands.
    uint32 const seconds = (objective.leftMs + IN_MILLISECONDS - 1) / IN_MILLISECONDS;
    hero->GetBonkPlayer()->SendAddon("objective~" + std::to_string(objective.id) + "~" + GetBonkObjectiveKindName(objective.kind) + "~" +
        FormatCoordinate(objective.x) + "~" + FormatCoordinate(objective.y) + "~" + std::to_string(hero->GetMapId()) + "~" +
        objective.label + "~" + std::to_string(seconds) + "~" + objective.icon + "~" + objective.description);
}

void BonkObjectives::RaisePillar(Player* hero, BonkObjective& objective) const
{
    GameObject* pillar = hero->SummonGameObject(BONK_GO_OBJECTIVE + objective.kind, objective.x, objective.y, objective.z, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, BONK_OBJECTIVE_PILLAR_SEC, false);
    if (!pillar)
        return;
    // Only active objects are seen past the usual distance.
    pillar->SetVisibilityModifier(BONK_OBJECTIVE_SIGHT);
    pillar->SetActiveObjectState(true);
    objective.pillar = pillar->GetObjectGuid();
}

void BonkObjectives::Despawn(Player* hero, BonkObjective const& objective) const
{
    if (Map* map = hero->FindMap())
        if (GameObject* pillar = map->GetGameObject(objective.pillar))
            pillar->Delete();
}
