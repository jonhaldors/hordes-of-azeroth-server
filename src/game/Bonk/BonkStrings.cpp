#include "BonkStrings.h"

std::vector<BonkStringText> const& GetBonkStringTexts()
{
    static std::vector<BonkStringText> const texts =
    {
        { BONK_STRING_LANGUAGE_USAGE,   "Usage: .bonk lang <locale>, for example .bonk lang zhCN. Current language: {1}." },
        { BONK_STRING_TIER_UNLOCKED,    "You unlocked {1} difficulty!" },
        { BONK_STRING_LANGUAGE_UNKNOWN, "Unknown language {1}. Use one of: {2}." },
        { BONK_STRING_LANGUAGE_SET,     "Language set to {1}." },
    };
    return texts;
}
