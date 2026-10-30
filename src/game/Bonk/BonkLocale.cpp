#include "BonkLocale.h"
#include "Config/Config.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Policies/SingletonImp.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <zlib.h>

INSTANTIATE_SINGLETON_1(BonkLocale);

namespace
{
    struct BonkFieldSource
    {
        BonkTextField field;
        char const* table;
        char const* column;
    };

    BonkFieldSource const FIELD_SOURCES[] =
    {
        { BONK_FIELD_GLYPH_NAME,        "bonk_glyph", "name"        },
        { BONK_FIELD_GLYPH_DESCRIPTION, "bonk_glyph", "description" },
        { BONK_FIELD_GLYPH_DRAWBACK,    "bonk_glyph", "drawback"    },
    };

    bool ReadLocale(std::string const& name, char const* table, uint32 id, LocaleConstant& locale)
    {
        locale = GetLocaleByName(name);
        if (locale != LOCALE_enUS)
            return true;

        sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `%s`: row %u has locale '%s', which is English or unknown, skipped.", table, id, name.c_str());
        return false;
    }

    std::string JsonString(std::string const& text)
    {
        std::string json = "\"";
        for (char c : text)
        {
            switch (c)
            {
                case '"':  json += "\\\""; break;
                case '\\': json += "\\\\"; break;
                case '\n': json += "\\n";  break;
                case '\r': json += "\\r";  break;
                case '\t': json += "\\t";  break;
                default:
                    if (uint8(c) < 0x20)
                    {
                        char escaped[8];
                        std::snprintf(escaped, sizeof(escaped), "\\u%04x", uint32(uint8(c)));
                        json += escaped;
                    }
                    else
                        json += c;
                    break;
            }
        }
        return json + "\"";
    }

    char const* TranslationStatus(std::string const& current, std::string const& outdated)
    {
        if (!current.empty())
            return "translated";
        return outdated.empty() ? "missing" : "outdated";
    }
}

void BonkLocale::Load()
{
    LoadStrings();
    LoadFields();
}

void BonkLocale::LoadStrings()
{
    m_strings.clear();

    for (BonkStringText const& text : GetBonkStringTexts())
    {
        StringTexts& texts = m_strings[text.id];
        if (!texts.current[LOCALE_enUS].empty())
            sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: string %u is defined twice in BonkStrings.cpp; the later text is used.", uint32(text.id));
        texts.current[LOCALE_enUS] = text.english;
        texts.hash = SourceHash(texts.current[LOCALE_enUS]);
    }

    uint32 translated = 0;
    uint32 outdated = 0;
    if (std::unique_ptr<QueryResult> result{ WorldDatabase.Query("SELECT `entry`, `locale`, `source_hash`, `content` FROM `bonk_string_locale`") })
    {
        do
        {
            Field* fields = result->Fetch();
            uint32 const entry = fields[0].GetUInt32();
            auto const itr = m_strings.find(entry);
            if (itr == m_strings.end())
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_string_locale`: entry %u is not a Bonk string, skipped.", entry);
                continue;
            }

            LocaleConstant locale;
            if (!ReadLocale(fields[1].GetCppString(), "bonk_string_locale", entry, locale))
                continue;

            StringTexts& texts = itr->second;
            if (fields[2].GetUInt32() != texts.hash)
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_MINIMAL, "Table `bonk_string_locale`: entry %u (%s) was translated from different English text, showing English until it is updated.",
                    entry, localeNames[locale]);
                texts.outdated[locale] = fields[3].GetCppString();
                ++outdated;
                continue;
            }

            texts.current[locale] = fields[3].GetCppString();
            ++translated;
        } while (result->NextRow());
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk strings, %u translations, %u out of date", uint32(m_strings.size()), translated, outdated);
}

void BonkLocale::LoadFields()
{
    for (auto& texts : m_fields)
        texts.clear();

    uint32 count = 0;
    for (BonkFieldSource const& source : FIELD_SOURCES)
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
            "SELECT `id`, `locale`, `%s` FROM `%s_locale` WHERE `%s` <> ''", source.column, source.table, source.column));
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            uint32 const id = fields[0].GetUInt32();
            LocaleConstant locale;
            if (!ReadLocale(fields[1].GetCppString(), source.table, id, locale))
                continue;

            m_fields[source.field][id][locale] = fields[2].GetCppString();
            ++count;
        } while (result->NextRow());
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u Bonk text translations", count);
}

std::string BonkLocale::GetString(BonkString id, LocaleConstant locale, std::vector<std::string> const& args) const
{
    auto const itr = m_strings.find(id);
    if (itr == m_strings.end())
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "Bonk: string %u has no English text in BonkStrings.cpp.", uint32(id));
        return "<bonk_string " + std::to_string(uint32(id)) + ">";
    }

    LocalizedText const& current = itr->second.current;
    bool const translated = locale < MAX_LOCALE && !current[locale].empty();
    return Substitute(translated ? current[locale] : current[LOCALE_enUS], args);
}

std::string BonkLocale::Localize(BonkTextField field, uint32 id, std::string const& english, LocaleConstant locale) const
{
    if (field >= MAX_BONK_TEXT_FIELD || locale == LOCALE_enUS || locale >= MAX_LOCALE)
        return english;

    auto const itr = m_fields[field].find(id);
    if (itr == m_fields[field].end() || itr->second[locale].empty())
        return english;
    return itr->second[locale];
}

bool BonkLocale::Export(LocaleConstant locale, std::string& path) const
{
    std::string directory = sConfig.GetStringDefault("LogsDir", "");
    if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
        directory += '/';
    path = directory + "bonk-strings-" + localeNames[locale] + ".json";

    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file)
        return false;

    file << "{\n  \"locale\": " << JsonString(localeNames[locale]) << ",\n  \"strings\": [";
    char const* separator = "\n";
    for (BonkStringText const& text : GetBonkStringTexts())
    {
        auto const itr = m_strings.find(text.id);
        if (itr == m_strings.end())
            continue;

        StringTexts const& texts = itr->second;
        std::string const& current = texts.current[locale];
        std::string const& outdated = texts.outdated[locale];
        file << separator << "    { \"id\": " << uint32(text.id) << ", \"hash\": " << texts.hash
             << ", \"english\": " << JsonString(texts.current[LOCALE_enUS])
             << ", \"translation\": " << JsonString(current.empty() ? outdated : current)
             << ", \"status\": \"" << TranslationStatus(current, outdated) << "\" }";
        separator = ",\n";
    }

    file << "\n  ],\n  \"fields\": [";
    separator = "\n";
    for (BonkFieldSource const& source : FIELD_SOURCES)
    {
        std::unique_ptr<QueryResult> result(WorldDatabase.PQuery(
            "SELECT `id`, `%s` FROM `%s` WHERE `%s` <> '' ORDER BY `id`", source.column, source.table, source.column));
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            uint32 const id = fields[0].GetUInt32();
            auto const itr = m_fields[source.field].find(id);
            std::string const translation = itr == m_fields[source.field].end() ? "" : itr->second[locale];
            file << separator << "    { \"table\": " << JsonString(source.table) << ", \"column\": " << JsonString(source.column) << ", \"id\": " << id
                 << ", \"english\": " << JsonString(fields[1].GetCppString())
                 << ", \"translation\": " << JsonString(translation)
                 << ", \"status\": \"" << TranslationStatus(translation, "") << "\" }";
            separator = ",\n";
        } while (result->NextRow());
    }

    file << "\n  ]\n}\n";
    return bool(file);
}

bool BonkLocale::ParseLocale(std::string const& name, LocaleConstant& locale)
{
    for (LocaleNameStr const* itr = &fullLocaleNameList[0]; itr->name; ++itr)
        if (name.size() == std::strlen(itr->name) && StringStartsWithCaseInsensitive(name, itr->name))
        {
            locale = itr->locale;
            return true;
        }
    return false;
}

std::string BonkLocale::ListLocales()
{
    std::string names;
    for (char const* name : localeNames)
        names += (names.empty() ? "" : ", ") + std::string(name);
    return names;
}

uint32 BonkLocale::SourceHash(std::string const& text)
{
    return uint32(crc32(0L, reinterpret_cast<Bytef const*>(text.data()), uInt(text.size())));
}

std::string BonkLocale::Substitute(std::string const& pattern, std::vector<std::string> const& args)
{
    std::string text;
    text.reserve(pattern.size());
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        bool const placeholder = pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' &&
            pattern[i + 1] >= '1' && pattern[i + 1] <= '9';
        if (!placeholder)
        {
            text += pattern[i];
            continue;
        }

        std::size_t const index = std::size_t(pattern[i + 1] - '1');
        if (index < args.size())
            text += args[index];
        i += 2;
    }
    return text;
}
