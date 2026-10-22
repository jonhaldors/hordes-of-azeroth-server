#include "BonkRun.h"

namespace
{
    uint32 const BONK_WEAPON_IMP = 900;
}

uint32 BonkRun::GetWarlockPetFamily(OwnedWeapon const& owned) const
{
    return GetRootWeapon(owned.weapon) == BONK_WEAPON_IMP ? BONK_PET_FAMILY_IMP : 0;
}

bool BonkRun::IsCasterPetFamily(BonkPetFamily const& family) const
{
    return family.id == BONK_PET_FAMILY_IMP;
}
