#ifndef MANGOS_BONKSTRINGS_H
#define MANGOS_BONKSTRINGS_H

#include "Common.h"

#include <vector>

enum BonkString : uint32
{
    BONK_STRING_LANGUAGE_USAGE   = 1,
    BONK_STRING_TIER_UNLOCKED    = 2,
    BONK_STRING_LANGUAGE_UNKNOWN = 3,
    BONK_STRING_LANGUAGE_SET     = 4,
};

struct BonkStringText
{
    BonkString id;
    char const* english;
};

std::vector<BonkStringText> const& GetBonkStringTexts();

#endif
