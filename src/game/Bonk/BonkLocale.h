#ifndef MANGOS_BONKLOCALE_H
#define MANGOS_BONKLOCALE_H

#include "BonkStrings.h"
#include "Common.h"
#include "Policies/Singleton.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

enum BonkTextField : uint8
{
    BONK_FIELD_GLYPH_NAME        = 0,
    BONK_FIELD_GLYPH_DESCRIPTION = 1,
    BONK_FIELD_GLYPH_DRAWBACK    = 2,
    MAX_BONK_TEXT_FIELD
};

class BonkLocale
{
    public:
        void Load();

        std::string GetString(BonkString id, LocaleConstant locale, std::vector<std::string> const& args) const;
        std::string Localize(BonkTextField field, uint32 id, std::string const& english, LocaleConstant locale) const;
        bool Export(LocaleConstant locale, std::string& path) const;

        static bool ParseLocale(std::string const& name, LocaleConstant& locale);
        static std::string ListLocales();
        static uint32 SourceHash(std::string const& text);
        static std::string Substitute(std::string const& pattern, std::vector<std::string> const& args);

    private:
        typedef std::array<std::string, MAX_LOCALE> LocalizedText;

        struct StringTexts
        {
            uint32 hash = 0;
            LocalizedText current;
            LocalizedText outdated;
        };

        void LoadStrings();
        void LoadFields();

        std::unordered_map<uint32, StringTexts> m_strings;
        std::array<std::unordered_map<uint32, LocalizedText>, MAX_BONK_TEXT_FIELD> m_fields;
};

#define sBonkLocale MaNGOS::Singleton<BonkLocale>::Instance()

#endif
