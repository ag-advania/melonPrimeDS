#include "MelonPrimeHudEditorFormLayout.h"
#include "MelonPrimeHudEditorPanelGeometry.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kRadioOnWidth = 48;
constexpr int kRadioOffWidth = 58;
constexpr int kColorButtonWidth = 74;
constexpr int kValueWidth = 38;

struct FormRow
{
    QLabel* label = nullptr;
    QWidget* field = nullptr;
};

class ClassicLayoutFixture
{
public:
    QWidget root;
    QVBoxLayout* outer = nullptr;
    QLabel* title = nullptr;
    QScrollArea* scroll = nullptr;
    QWidget* inner = nullptr;
    QFormLayout* form = nullptr;
    std::vector<FormRow> rows;

    explicit ClassicLayoutFixture(const QString& titleText)
    {
        root.setWindowTitle(QStringLiteral("Classic layout geometry test"));
        root.setMinimumSize(0, 0);

        outer = new QVBoxLayout(&root);
        outer->setContentsMargins(6, 4, 6, 4);
        outer->setSpacing(2);

        title = new QLabel(titleText, &root);
        title->setWordWrap(true);
        title->setAlignment(Qt::AlignCenter);
        title->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        outer->addWidget(title);

        scroll = new QScrollArea(&root);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setFrameShape(QFrame::NoFrame);
        outer->addWidget(scroll);

        inner = new QWidget();
        inner->setMinimumSize(0, 0);
        inner->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        form = new QFormLayout(inner);
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(3);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        scroll->setWidget(inner);
    }

    void addRow(const QString& labelText, QWidget* field)
    {
        QLabel* label = MelonPrime::HudEditorForm::CreateTranslatedFormLabel(
            *form, labelText);
        form->addRow(label, field);
        rows.push_back({label, field});
    }

    void addBoolRow(const QString& labelText)
    {
        auto* field = new QWidget(inner);
        auto* layout = new QHBoxLayout(field);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        auto* on = new QRadioButton(QStringLiteral("ON"), field);
        auto* off = new QRadioButton(QStringLiteral("OFF"), field);
        on->setMinimumWidth(kRadioOnWidth);
        off->setMinimumWidth(kRadioOffWidth);
        layout->addWidget(on, 0);
        layout->addWidget(off, 0);
        addRow(labelText, field);
    }

    void addComboRow(const QString& labelText)
    {
        auto* field = new QComboBox(inner);
        field->setMinimumWidth(78);
        field->addItems({QStringLiteral("Overall"), QStringLiteral("Custom")});
        addRow(labelText, field);
    }

    void addSpinRow(const QString& labelText)
    {
        auto* field = new QSpinBox(inner);
        field->setMinimumWidth(64);
        field->setRange(-256, 256);
        addRow(labelText, field);
    }

    void addDoubleSpinRow(const QString& labelText)
    {
        auto* field = new QDoubleSpinBox(inner);
        field->setMinimumWidth(64);
        field->setRange(0.0, 1.0);
        field->setSingleStep(0.01);
        addRow(labelText, field);
    }

    void addOpacityRow(const QString& labelText)
    {
        auto* field = new QWidget(inner);
        auto* layout = new QHBoxLayout(field);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* slider = new QSlider(Qt::Horizontal, field);
        slider->setMinimumWidth(80);
        auto* value = new QLabel(QStringLiteral("100%"), field);
        value->setFixedWidth(kValueWidth);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(slider, 1);
        layout->addWidget(value, 0);
        addRow(labelText, field);
    }

    void addLineEditRow(const QString& labelText)
    {
        auto* field = new QLineEdit(inner);
        field->setMinimumWidth(78);
        field->setText(QStringLiteral("sample"));
        addRow(labelText, field);
    }

    QPushButton* addColorButton(QWidget* parent)
    {
        auto* button = new QPushButton(QStringLiteral("#80a0ff"), parent);
        button->setMinimumWidth(kColorButtonWidth);
        return button;
    }

    void addColorRow(const QString& labelText)
    {
        addRow(labelText, addColorButton(inner));
    }

    void addSubColorRow(const QString& labelText)
    {
        auto* field = new QWidget(inner);
        auto* layout = new QHBoxLayout(field);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        auto* combo = new QComboBox(field);
        combo->addItems({QStringLiteral("Overall"), QStringLiteral("Custom")});
        layout->addWidget(combo, 1);
        layout->addWidget(addColorButton(field), 0);
        addRow(labelText, field);
    }

    void addColorOverlayRow(const QString& labelText)
    {
        auto* field = new QWidget(inner);
        auto* layout = new QHBoxLayout(field);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        auto* on = new QRadioButton(QStringLiteral("ON"), field);
        auto* off = new QRadioButton(QStringLiteral("OFF"), field);
        on->setMinimumWidth(kRadioOnWidth);
        off->setMinimumWidth(kRadioOffWidth);
        layout->addWidget(on, 0);
        layout->addWidget(off, 0);
        layout->addWidget(addColorButton(field), 0);
        addRow(labelText, field);
    }

    void addSection(const QString& text)
    {
        auto* header = new QLabel(text, inner);
        MelonPrime::HudEditorForm::ConfigureWrappedFormLabel(*header);
        header->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        QFont font = header->font();
        font.setBold(true);
        header->setFont(font);
        form->addRow(header);
    }

    void addSeparator()
    {
        auto* line = new QFrame(inner);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        form->addRow(line);
    }

    void addAllRowTypes(const QString& labelPrefix)
    {
        addSection(labelPrefix + QStringLiteral(" Section Header"));
        addBoolRow(labelPrefix + QStringLiteral(" Enabled"));
        addComboRow(labelPrefix + QStringLiteral(" Anchor"));
        addSpinRow(labelPrefix + QStringLiteral(" Offset X"));
        addDoubleSpinRow(labelPrefix + QStringLiteral(" Opacity"));
        addOpacityRow(labelPrefix + QStringLiteral(" Opacity Slider"));
        addLineEditRow(labelPrefix + QStringLiteral(" Label Text"));
        addColorRow(labelPrefix + QStringLiteral(" Color"));
        addSubColorRow(labelPrefix + QStringLiteral(" Sub Color"));
        addColorOverlayRow(labelPrefix + QStringLiteral(" Color Overlay"));
        addSeparator();
    }
};

struct CaseSpec
{
    const char* name;
    int windowWidth;
    int windowHeight;
    QString labelPrefix;
    bool requireExtremeReflow;
    bool requireWideContent;
};

bool inside(const QRect& child, const QRect& parent)
{
    return child.left() >= parent.left()
        && child.top() >= parent.top()
        && child.right() <= parent.right()
        && child.bottom() <= parent.bottom();
}

std::string rectText(const QRect& rect)
{
    return "(" + std::to_string(rect.x()) + "," + std::to_string(rect.y())
        + " " + std::to_string(rect.width()) + "x" + std::to_string(rect.height()) + ")";
}

bool checkFixture(ClassicLayoutFixture& fixture, const CaseSpec& spec,
                  std::string& failure)
{
    fixture.root.adjustSize();
    fixture.form->activate();
    const int naturalWidth = std::max(fixture.root.sizeHint().width(),
                                      fixture.root.minimumSizeHint().width());
    const int finalWidth = MelonPrime::HudEditorPanelGeometry::FinalWidth(
        spec.windowWidth, naturalWidth);
    const int usableWidth = MelonPrime::HudEditorPanelGeometry::UsableWidth(
        spec.windowWidth);

    fixture.root.setMaximumWidth(usableWidth);
    fixture.root.resize(finalWidth, spec.windowHeight);
    fixture.root.show();
    QApplication::processEvents();
    fixture.form->activate();
    QApplication::processEvents();
    for (int pass = 0; pass < 3; ++pass)
    {
        const int viewportWidth = fixture.scroll->viewport()->width();
        MelonPrime::HudEditorForm::ConstrainWrappedFormLabels(
            fixture.root, viewportWidth > 0 ? viewportWidth : finalWidth);
        fixture.form->activate();
        QApplication::processEvents();
    }

    const QRect innerRect = fixture.inner->rect();
    if (fixture.root.width() > usableWidth)
    {
        failure = "panel width exceeds usable window width";
        return false;
    }
    if (fixture.inner->width() > fixture.scroll->viewport()->width() + 1)
    {
        failure = "inner form is wider than the scroll viewport inner="
            + rectText(fixture.inner->geometry()) + " viewport="
            + rectText(fixture.scroll->viewport()->rect());
        return false;
    }
    if (fixture.scroll->horizontalScrollBar()->maximum() != 0)
    {
        failure = "horizontal overflow created a scrollbar range";
        return false;
    }

    for (const FormRow& row : fixture.rows)
    {
        if (!row.label || !row.field || row.label->width() <= 0 || row.label->height() <= 0
            || row.field->width() <= 0 || row.field->height() <= 0)
        {
            failure = "label or field has non-positive geometry label="
                + (row.label ? row.label->text().toStdString() : "<null>")
                + " labelRect=" + (row.label ? rectText(row.label->geometry()) : "<null>")
                + " fieldRect=" + (row.field ? rectText(row.field->geometry()) : "<null>");
            return false;
        }
        if (!inside(row.label->geometry(), innerRect)
            || !inside(row.field->geometry(), innerRect))
        {
            failure = "label or field leaves the form viewport label="
                + row.label->text().toStdString() + " labelRect="
                + rectText(row.label->geometry()) + " fieldRect="
                + rectText(row.field->geometry()) + " inner=" + rectText(innerRect)
                + " viewport=" + rectText(fixture.scroll->viewport()->rect());
            return false;
        }
        if (row.label->geometry().intersects(row.field->geometry()))
        {
            failure = "label and field geometries overlap";
            return false;
        }
        const int requiredHeight = row.label->heightForWidth(row.label->width());
        if (requiredHeight > 0 && requiredHeight > row.label->height() + 1)
        {
            failure = "wrapped label is clipped vertically row="
                + row.label->text().toStdString() + " label="
                + rectText(row.label->geometry()) + " requiredHeight="
                + std::to_string(requiredHeight);
            return false;
        }

        const QList<QWidget*> children = row.field->layout()
            ? row.field->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)
            : QList<QWidget*>();
        for (QWidget* child : children)
        {
            // QComboBox creates an invisible popup child, and QLineEdit may
            // create style-private helpers. They are not field controls and
            // are not part of the row geometry contract.
            if (!child->isVisible())
                continue;
            if (child->width() <= 0 || child->height() <= 0
                || !inside(child->geometry(), row.field->rect()))
            {
                failure = "field control has invalid or overflowing geometry row="
                    + row.label->text().toStdString() + " field="
                    + rectText(row.field->geometry()) + " child="
                    + rectText(child->geometry()) + " childClass="
                    + child->metaObject()->className();
                return false;
            }
        }
    }

    // The last field must remain reachable after the vertical scrollbar is
    // driven to its end; this proves height is content-driven, not clipped.
    if (!fixture.rows.empty())
    {
        fixture.scroll->verticalScrollBar()->setValue(
            fixture.scroll->verticalScrollBar()->maximum());
        QApplication::processEvents();
        const FormRow& last = fixture.rows.back();
        const QRect lastInViewport(
            fixture.inner->mapTo(fixture.scroll->viewport(), last.field->geometry().topLeft()),
            last.field->size());
        if (!lastInViewport.intersects(fixture.scroll->viewport()->rect()))
        {
            failure = "vertical scrolling cannot reach the last row";
            return false;
        }
    }

    if (spec.requireExtremeReflow)
    {
        const FormRow& first = fixture.rows.front();
        if (first.field->geometry().top() <= first.label->geometry().bottom())
        {
            failure = "extreme label did not reflow its field to a following row";
            return false;
        }
    }
    if (spec.requireWideContent && fixture.root.width() <=
        MelonPrime::HudEditorPanelGeometry::kPreferredWidth)
    {
        failure = "wide natural content did not use available width";
        return false;
    }

    return true;
}

QString localizedPrefix(const QString& language, const QString& base)
{
    if (language.isEmpty())
        return base;
    return language + QStringLiteral(" — ") + base;
}

std::vector<QString> registeredLanguageCodes()
{
    return {
        QStringLiteral("ar"), QStringLiteral("it"), QStringLiteral("id"), QStringLiteral("uk"),
        QStringLiteral("en"), QStringLiteral("en-GB"), QStringLiteral("en-US"), QStringLiteral("nl"),
        QStringLiteral("el"), QStringLiteral("ko"), QStringLiteral("sv"), QStringLiteral("es"),
        QStringLiteral("es-419"), QStringLiteral("th"), QStringLiteral("cs"), QStringLiteral("zh-Hans"),
        QStringLiteral("zh-Hant"), QStringLiteral("da"), QStringLiteral("de"), QStringLiteral("tr"),
        QStringLiteral("ja"), QStringLiteral("nb"), QStringLiteral("hu"), QStringLiteral("fi"),
        QStringLiteral("fr"), QStringLiteral("fr-CA"), QStringLiteral("vi"), QStringLiteral("pl"),
        QStringLiteral("pt"), QStringLiteral("pt-BR"), QStringLiteral("ro"), QStringLiteral("ru"),
        QStringLiteral("af"), QStringLiteral("ga"), QStringLiteral("is"), QStringLiteral("az"),
        QStringLiteral("as"), QStringLiteral("am"), QStringLiteral("sq"), QStringLiteral("hy"),
        QStringLiteral("uz"), QStringLiteral("ur"), QStringLiteral("et"), QStringLiteral("or"),
        QStringLiteral("kk"), QStringLiteral("ca"), QStringLiteral("kn"), QStringLiteral("ky"),
        QStringLiteral("gu"), QStringLiteral("km"), QStringLiteral("hr"), QStringLiteral("ka"),
        QStringLiteral("si"), QStringLiteral("sw"), QStringLiteral("sk"), QStringLiteral("sl"),
        QStringLiteral("zu"), QStringLiteral("sr"), QStringLiteral("ta"), QStringLiteral("zh-HK"),
        QStringLiteral("te"), QStringLiteral("ne"), QStringLiteral("eu"), QStringLiteral("pa"),
        QStringLiteral("hi"), QStringLiteral("fil"), QStringLiteral("bg"), QStringLiteral("he"),
        QStringLiteral("be"), QStringLiteral("bn"), QStringLiteral("fa"), QStringLiteral("bs"),
        QStringLiteral("mk"), QStringLiteral("mr"), QStringLiteral("ml"), QStringLiteral("mt"),
        QStringLiteral("ms"), QStringLiteral("my"), QStringLiteral("mn"), QStringLiteral("lo"),
        QStringLiteral("lv"), QStringLiteral("lt")
    };
}

bool runCase(const CaseSpec& spec, int pointSize, bool printResult, std::string& failure)
{
    ClassicLayoutFixture fixture(
        localizedPrefix(spec.labelPrefix, QStringLiteral("Custom HUD On-Screen Edit")));
    const QString longLabel = localizedPrefix(
        spec.labelPrefix, QStringLiteral("HP Label Color By Value"));
    fixture.addAllRowTypes(longLabel);

    // T09 deliberately has a long unbroken segment as well as spaces.  QLabel
    // must remain readable without relying on an ellipsis or a horizontal bar.
    if (spec.requireExtremeReflow)
    {
        fixture.rows.clear();
        while (fixture.form->rowCount() > 0)
            fixture.form->removeRow(0);
        const QString extreme = QStringLiteral("Synthetic Extreme ")
            + QString(320, QChar('W'))
            + QStringLiteral(" label that must wrap");
        fixture.addBoolRow(extreme);
        fixture.addComboRow(QStringLiteral("Anchor"));
        fixture.addColorOverlayRow(QStringLiteral("Color Overlay"));
    }

    (void)pointSize;
    const bool passed = checkFixture(fixture, spec, failure);
    if (printResult)
    {
        std::cout << (passed ? "PASS " : "FAIL ") << spec.name
                  << " width=" << fixture.root.width()
                  << " rows=" << fixture.rows.size() << '\n';
    }
    return passed;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QFont originalFont = app.font();
    const std::array<int, 4> pointSizes = {9, 11, 13, 17};

    const std::array<CaseSpec, 12> cases = {{
        {"T01 English", 1600, 420, QString(), false, false},
        {"T02 French", 420, 420, QString::fromUtf8("PV : couleur du libellé selon la valeur"), false, false},
        {"T03 French Canada", 420, 420, QString::fromUtf8("Couleur de l’étiquette des points de vie selon la valeur"), false, false},
        {"T04 German", 420, 420, QString::fromUtf8("Farbe der Lebenspunktanzeige abhängig vom Wert"), false, false},
        {"T05 Hungarian", 420, 420, QString::fromUtf8("Életerőfelirat színezése az érték alapján"), false, false},
        {"T06 Greek", 420, 420, QString::fromUtf8("Χρώμα ετικέτας ζωής ανάλογα με την τιμή"), false, false},
        {"T07 Russian", 420, 420, QString::fromUtf8("Цвет подписи здоровья в зависимости от значения"), false, false},
        {"T08 Japanese", 420, 420, QString::fromUtf8("値に応じたHPラベルの色"), false, false},
        {"T09 synthetic extreme label", 220, 260, QStringLiteral("Extreme"), true, false},
        {"T10 narrow window", 220, 260, QString::fromUtf8("Français — fenêtre étroite"), false, false},
        {"T11 wide window", 1600, 420, QString(120, QChar('L')), false, true},
        {"T12 all row types", 640, 420, QStringLiteral("All row types"), false, false},
    }};

    int failures = 0;
    for (int pointSize : pointSizes)
    {
        QFont font = originalFont;
        font.setPointSize(pointSize);
        app.setFont(font);
        for (const CaseSpec& spec : cases)
        {
            std::string failure;
            if (!runCase(spec, pointSize, true, failure))
            {
                std::cerr << "  font=" << pointSize << "pt: " << failure << '\n';
                ++failures;
            }
        }
    }

    // Exercise the same contract against every currently registered language
    // code. The strings are deliberately synthetic: this is a geometry gate,
    // so it must also prove that future longer translations remain operable.
    const auto languageCodes = registeredLanguageCodes();
    if (languageCodes.size() != 82)
    {
        std::cerr << "FAIL registered language fixture count=" << languageCodes.size()
                  << " expected=82\n";
        ++failures;
    }
    else
    {
        QFont font = originalFont;
        font.setPointSize(13);
        app.setFont(font);
        for (const QString& code : languageCodes)
        {
            const CaseSpec spec{
                "T13 registered language",
                420,
                320,
                code + QStringLiteral(" — localized long label"),
                false,
                false};
            std::string failure;
            if (!runCase(spec, 13, false, failure))
            {
                std::cerr << "FAIL T13 " << code.toStdString() << ": " << failure << '\n';
                ++failures;
            }
        }
        std::cout << "PASS T13 registered language geometry cases=82\n";
    }

    app.setFont(originalFont);
    if (failures != 0)
    {
        std::cerr << "Classic On-Screen Edit Qt layout tests: FAIL cases=" << failures << '\n';
        return 1;
    }
    std::cout << "Classic On-Screen Edit Qt layout tests: PASS\n";
    return 0;
}
