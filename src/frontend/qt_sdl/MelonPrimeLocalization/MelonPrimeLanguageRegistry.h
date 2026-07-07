#pragma once

#include "../MelonPrimeLocalization.h"

namespace MelonPrime::UiText
{

enum class TextDirection
{
    LeftToRight,
    RightToLeft,
};

enum class SplashFontGroup
{
    Latin,
    Japanese,
    ChineseSimplified,
    ChineseTraditional,
    Korean,
    Arabic,
    Thai,

    // Added for the 50-language expansion. Persian and Urdu reuse Arabic
    // (shared Perso-Arabic Unicode block); ChineseHongKong reuses
    // ChineseTraditional (same script); other new languages using a simple
    // Latin/Cyrillic alphabet reuse Latin.
    Devanagari,
    Bengali,
    Gurmukhi,
    Gujarati,
    Odia,
    Kannada,
    Malayalam,
    Tamil,
    Telugu,
    Sinhala,
    Khmer,
    Lao,
    Myanmar,
    Hebrew,
    Ethiopic,
    Georgian,
    Armenian,
};

struct LanguageInfo
{
    MenuLangId id;
    const char* stableCode;
    const char* displayName;
    MenuLangId translationBase;
    bool selectable;
    TextDirection direction;
    bool requiresShapedSplash;
    SplashFontGroup splashFontGroup;
};

const LanguageInfo* FindLanguageInfo(MenuLangId id);
const LanguageInfo& LanguageInfoOrEnglish(MenuLangId id);
MenuLangId ResolveTranslationLanguage(MenuLangId lang);
bool IsRightToLeftLanguage(MenuLangId lang);
bool RequiresShapedSplashText(MenuLangId lang);
SplashFontGroup SplashFontGroupForLanguage(MenuLangId lang);

} // namespace MelonPrime::UiText
