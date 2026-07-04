#include "MelonPrimeSplashLocalization.h"

#include "MelonPrimeLanguageRegistry.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTextOption>

#include <algorithm>
#include <cstring>
#include <utility>

namespace MelonPrime::UiText
{

void ApplyNoRomSplashLocalization(char line0[256], char line1[256])
{
    static constexpr const char kLine0En[] = "File->Open ROM...";
    static constexpr const char kLine1En[] = "to get started";

    const QString q0 = IsMenuTranslationActive() ? Tr(kLine0En) : QString::fromUtf8(kLine0En);
    const QString q1 = IsMenuTranslationActive() ? Tr(kLine1En) : QString::fromUtf8(kLine1En);

    auto copyUtf8Bounded = [](char out[256], const QString& text)
    {
        QString truncated = text;
        QByteArray bytes = truncated.toUtf8();
        while (bytes.size() > 255 && !truncated.isEmpty())
        {
            truncated.chop(1);
            bytes = truncated.toUtf8();
        }

        const int n = static_cast<int>(std::min<qsizetype>(bytes.size(), 255));
        std::memcpy(out, bytes.constData(), static_cast<size_t>(n));
        out[n] = '\0';
    };

    copyUtf8Bounded(line0, q0);
    copyUtf8Bounded(line1, q1);
}

namespace {

constexpr unsigned kNoRomSplashIdLine0 = 0x80000000u;
constexpr unsigned kNoRomSplashIdLine1 = 0x80000001u;

[[nodiscard]] unsigned int SplashOsdRainbowColor(int inc) noexcept
{
    if (inc < 100) return 0xFFFF9B9B + (static_cast<unsigned int>(inc) << 8);
    if (inc < 200) return 0xFFFFFF9B - (static_cast<unsigned int>(inc - 100) << 16);
    if (inc < 300) return 0xFF9BFF9B + static_cast<unsigned int>(inc - 200);
    if (inc < 400) return 0xFF9BFFFF - (static_cast<unsigned int>(inc - 300) << 8);
    if (inc < 500) return 0xFF9B9BFF + (static_cast<unsigned int>(inc - 400) << 16);
    return 0xFFFF9BFF - static_cast<unsigned int>(inc - 500);
}

[[nodiscard]] QFont NoRomSplashUiFont()
{
    QFont font;
    switch (SplashFontGroupForLanguage(ActiveMenuLanguage())) {
    case SplashFontGroup::Japanese:
        font.setFamilies({
            QStringLiteral("Hiragino Sans"),
            QStringLiteral("Hiragino Kaku Gothic ProN"),
            QStringLiteral("Noto Sans CJK JP"),
            QStringLiteral("Yu Gothic UI"),
            QStringLiteral("Meiryo UI"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case SplashFontGroup::ChineseSimplified:
        font.setFamilies({
            QStringLiteral("PingFang SC"),
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Source Han Sans SC"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case SplashFontGroup::ChineseTraditional:
        font.setFamilies({
            QStringLiteral("PingFang TC"),
            QStringLiteral("Noto Sans CJK TC"),
            QStringLiteral("Microsoft JhengHei UI"),
            QStringLiteral("Source Han Sans TC"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case SplashFontGroup::Korean:
        font.setFamilies({
            QStringLiteral("Apple SD Gothic Neo"),
            QStringLiteral("Noto Sans CJK KR"),
            QStringLiteral("Malgun Gothic"),
            QStringLiteral("Nanum Gothic"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case SplashFontGroup::Arabic:
        font.setFamilies({
            QStringLiteral("Geeza Pro"),
            QStringLiteral("Noto Sans Arabic"),
            QStringLiteral("Arial"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case SplashFontGroup::Thai:
        font.setFamilies({
            QStringLiteral("Thonburi"),
            QStringLiteral("Noto Sans Thai"),
            QStringLiteral("Leelawadee UI"),
            QStringLiteral("Segoe UI"),
        });
        break;
    default:
        font.setFamilies({
            QStringLiteral("Segoe UI"),
            QStringLiteral("Helvetica Neue"),
            QStringLiteral("Arial"),
            QStringLiteral("Noto Sans"),
        });
        break;
    }
    font.setPixelSize(12);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace

bool UsesLocalizedSplashLayout()
{
    return IsMenuTranslationActive();
}

bool TryRenderNoRomSplashOsdItem(unsigned int id, const char* text, unsigned int color,
    int rainbowstart, int& rainbowend, int maxWidth, QImage* outBitmap)
{
    if (!outBitmap || !text || !text[0])
        return false;
    if (!IsMenuTranslationActive())
        return false;
    if (id != kNoRomSplashIdLine0 && id != kNoRomSplashIdLine1)
        return false;

    const QString qtext = QString::fromUtf8(text);
    if (qtext.isEmpty())
        return false;

    const QFont font = NoRomSplashUiFont();
    const QFontMetrics fm(font);

    const bool rainbow = (color == 0);
    unsigned int rainbowinc = 0;
    if (rainbowstart == -1)
    {
        const unsigned int ticks = static_cast<unsigned int>(QDateTime::currentMSecsSinceEpoch());
        rainbowinc = ((static_cast<unsigned char>(text[0]) * 17u) + (ticks * 13u)) % 600u;
    }
    else
    {
        rainbowinc = static_cast<unsigned int>(rainbowstart);
    }

    const int shadowPad = 1;
    int w = fm.horizontalAdvance(qtext);
    if (maxWidth > 0)
        w = std::min(w, maxWidth);
    const int h = fm.height();
    QImage bitmap(w + shadowPad, h + shadowPad, QImage::Format_ARGB32_Premultiplied);
    bitmap.fill(Qt::transparent);

    QPainter painter(&bitmap);
    painter.setFont(font);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int baseline = fm.ascent();
    const QColor shadowColor(0, 0, 0, 224);
    const bool needsShapedText = RequiresShapedSplashText(ActiveMenuLanguage());

    if (needsShapedText)
    {
        const unsigned int rgba = rainbow ? SplashOsdRainbowColor(static_cast<int>(rainbowinc))
                                        : (color | 0xFF000000u);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        if (IsRightToLeftLanguage(ActiveMenuLanguage()))
        {
            option.setTextDirection(Qt::RightToLeft);
            option.setAlignment(Qt::AlignRight | Qt::AlignTop);
        }
        else
        {
            option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
        }

        const QRect textRect(0, 0, w, h);
        painter.setPen(shadowColor);
        painter.drawText(textRect.translated(shadowPad, shadowPad), qtext, option);
        painter.setPen(QColor(static_cast<QRgb>(rgba)));
        painter.drawText(textRect, qtext, option);

        if (rainbow)
        {
            for (const QChar ch : qtext)
            {
                if (ch != QLatin1Char(' '))
                    rainbowinc = (rainbowinc + 30u) % 600u;
            }
        }

        rainbowend = static_cast<int>(rainbowinc);
        *outBitmap = std::move(bitmap);
        return true;
    }

    int x = 0;
    for (const QChar ch : qtext)
    {
        const unsigned int rgba = rainbow ? SplashOsdRainbowColor(static_cast<int>(rainbowinc))
                                        : (color | 0xFF000000u);
        const QColor mainColor(static_cast<QRgb>(rgba));

        const QString glyph(ch);
        painter.setPen(shadowColor);
        painter.drawText(x + shadowPad, baseline + shadowPad, glyph);
        painter.setPen(mainColor);
        painter.drawText(x, baseline, glyph);

        x += fm.horizontalAdvance(glyph);
        if (rainbow && ch != QLatin1Char(' '))
            rainbowinc = (rainbowinc + 30u) % 600u;
    }

    rainbowend = static_cast<int>(rainbowinc);
    *outBitmap = std::move(bitmap);
    return true;
}

} // namespace MelonPrime::UiText
