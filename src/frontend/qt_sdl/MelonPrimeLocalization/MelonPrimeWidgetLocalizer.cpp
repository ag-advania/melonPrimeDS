#include "MelonPrimeWidgetLocalizer.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFontComboBox>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QVariant>

namespace MelonPrime::UiText
{

QString TrWidgetText(const QWidget* widget, const QString& text)
{
    if (!IsMenuTranslationActive() || text.isEmpty())
        return text;

    const QString objectTranslated = TranslateByObjectName(widget, text);
    if (objectTranslated != text)
        return objectTranslated;

    return Tr(text);
}

QString SourcePropertyText(QWidget* widget, const char* propertyName, const QString& current)
{
    if (!widget)
        return current;

    const QVariant stored = widget->property(propertyName);
    if (stored.isValid())
        return stored.toString();

    widget->setProperty(propertyName, current);
    return current;
}

QStringList SourcePropertyTextList(QWidget* widget, const char* propertyName, const QStringList& current)
{
    if (!widget)
        return current;

    const QVariant stored = widget->property(propertyName);
    if (stored.isValid())
        return stored.toStringList();

    widget->setProperty(propertyName, current);
    return current;
}

QString SourceObjectPropertyText(QObject* object, const char* propertyName, const QString& current)
{
    if (!object)
        return current;

    const QVariant stored = object->property(propertyName);
    if (stored.isValid())
        return stored.toString();

    object->setProperty(propertyName, current);
    return current;
}

void LocalizeWidgetTextProperties(QWidget* widget)
{
    if (!widget)
        return;

    const QString windowTitle = widget->windowTitle();
    if (!windowTitle.isEmpty())
        widget->setWindowTitle(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_window_title", windowTitle)));

    const QString toolTip = widget->toolTip();
    if (!toolTip.isEmpty())
        widget->setToolTip(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_tooltip", toolTip)));

    const QString whatsThis = widget->whatsThis();
    if (!whatsThis.isEmpty())
        widget->setWhatsThis(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_whatsthis", whatsThis)));

    const QString statusTip = widget->statusTip();
    if (!statusTip.isEmpty())
        widget->setStatusTip(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_statustip", statusTip)));
}

void LocalizeWidgetTree(QWidget* root)
{
    if (!root)
        return;

    LocalizeWidgetTextProperties(root);

    for (QLabel* label : root->findChildren<QLabel*>())
    {
        label->setText(TrWidgetText(
            label,
            SourcePropertyText(label, "_melonprime_src_text", label->text())));
        LocalizeWidgetTextProperties(label);
    }

    for (QAbstractButton* button : root->findChildren<QAbstractButton*>())
    {
        button->setText(TrWidgetText(
            button,
            SourcePropertyText(button, "_melonprime_src_text", button->text())));
        LocalizeWidgetTextProperties(button);
    }

    for (QGroupBox* group : root->findChildren<QGroupBox*>())
    {
        group->setTitle(TrWidgetText(
            group,
            SourcePropertyText(group, "_melonprime_src_title", group->title())));
        LocalizeWidgetTextProperties(group);
    }

    for (QTabWidget* tabs : root->findChildren<QTabWidget*>())
    {
        QStringList tabTexts;
        tabTexts.reserve(tabs->count());
        for (int i = 0; i < tabs->count(); ++i)
            tabTexts.append(tabs->tabText(i));
        const QStringList sourceTabTexts =
            SourcePropertyTextList(tabs, "_melonprime_src_tab_texts", tabTexts);
        for (int i = 0; i < tabs->count(); ++i)
        {
            const QString source =
                (i < sourceTabTexts.size()) ? sourceTabTexts.at(i) : tabs->tabText(i);
            tabs->setTabText(i, Tr(source));
        }
        LocalizeWidgetTextProperties(tabs);
    }

    for (QComboBox* combo : root->findChildren<QComboBox*>())
    {
        if (qobject_cast<QFontComboBox*>(combo))
            continue;
        for (int i = 0; i < combo->count(); ++i)
        {
            static constexpr int kSourceItemTextRole = Qt::UserRole + 30001;
            const QVariant stored = combo->itemData(i, kSourceItemTextRole);
            const QString source = stored.isValid() ? stored.toString() : combo->itemText(i);
            if (!stored.isValid())
                combo->setItemData(i, source, kSourceItemTextRole);
            combo->setItemText(i, Tr(source));
        }
        LocalizeWidgetTextProperties(combo);
    }

    for (QLineEdit* lineEdit : root->findChildren<QLineEdit*>())
    {
        const QString placeholder = lineEdit->placeholderText();
        if (!placeholder.isEmpty())
            lineEdit->setPlaceholderText(TrWidgetText(
                lineEdit,
                SourcePropertyText(lineEdit, "_melonprime_src_placeholder", placeholder)));
        LocalizeWidgetTextProperties(lineEdit);
    }

    for (QPlainTextEdit* textEdit : root->findChildren<QPlainTextEdit*>())
    {
        const QString placeholder = textEdit->placeholderText();
        if (!placeholder.isEmpty())
            textEdit->setPlaceholderText(TrWidgetText(
                textEdit,
                SourcePropertyText(textEdit, "_melonprime_src_placeholder", placeholder)));
        LocalizeWidgetTextProperties(textEdit);
    }
}

void LocalizeActionTextProperties(QAction* action)
{
    if (!action)
        return;

    const QString toolTip = action->toolTip();
    if (!toolTip.isEmpty())
        action->setToolTip(Tr(SourceObjectPropertyText(
            action,
            "_melonprime_src_action_tooltip",
            toolTip)));

    const QString whatsThis = action->whatsThis();
    if (!whatsThis.isEmpty())
        action->setWhatsThis(Tr(SourceObjectPropertyText(
            action,
            "_melonprime_src_action_whatsthis",
            whatsThis)));

    const QString statusTip = action->statusTip();
    if (!statusTip.isEmpty())
        action->setStatusTip(Tr(SourceObjectPropertyText(
            action,
            "_melonprime_src_action_statustip",
            statusTip)));
}

void SetLocalizedActionText(QAction* action, const QString& sourceText)
{
    if (!action)
        return;

    action->setProperty("_melonprime_src_action_text", sourceText);
    action->setText(Tr(sourceText));
    LocalizeActionTextProperties(action);
}

void LocalizeAction(QAction* action);

void LocalizeMenu(QMenu* menu)
{
    if (!menu)
        return;

    const QString title = SourceObjectPropertyText(
        menu,
        "_melonprime_src_menu_title",
        menu->title());
    menu->setTitle(Tr(title));
    SetLocalizedActionText(menu->menuAction(), title);

    for (QAction* action : menu->actions())
        LocalizeAction(action);
}

void LocalizeAction(QAction* action)
{
    if (!action || action->isSeparator())
        return;

    if (QMenu* submenu = action->menu())
    {
        LocalizeMenu(submenu);
        return;
    }

    const QString source = SourceObjectPropertyText(
        action,
        "_melonprime_src_action_text",
        action->text());
    SetLocalizedActionText(action, source);
}

void LocalizeMenuBar(QMenuBar* menuBar)
{
    if (!menuBar)
        return;

    for (QAction* action : menuBar->actions())
        LocalizeAction(action);
}

namespace {

void wireMelonDsDialogDynamicLabels(QWidget* dialog)
{
    if (!dialog || !IsMenuTranslationActive())
        return;

    // CheatsDialog sets chkItemOption text in selection handlers (upstream .cpp).
    // Re-translate after each selection change from MelonPrime side only.
    if (dialog->objectName() != QStringLiteral("CheatsDialog"))
        return;

    auto* tree = dialog->findChild<QTreeView*>(QStringLiteral("tvCodeList"));
    auto* chk = dialog->findChild<QCheckBox*>(QStringLiteral("chkItemOption"));
    if (!tree || !chk)
        return;

    auto* sel = tree->selectionModel();
    if (!sel)
        return;

    const char* hookKey = "_melonprime_cheats_option_hook";
    if (dialog->property(hookKey).toBool())
        return;
    dialog->setProperty(hookKey, true);

    auto relocalizeOption = [chk]() {
        if (!IsMenuTranslationActive() || !chk)
            return;
        const QString raw = chk->text();
        const QString translated = Tr(raw);
        if (translated != raw)
            chk->setText(translated);
    };

    QObject::connect(
        sel,
        &QItemSelectionModel::selectionChanged,
        dialog,
        relocalizeOption,
        Qt::QueuedConnection);

    relocalizeOption();
}

class MelonDsLanPopupLocalizer final : public QObject
{
public:
    explicit MelonDsLanPopupLocalizer(QWidget* owner)
        : QObject(owner)
        , m_owner(owner)
    {
        qApp->installEventFilter(this);
    }

    ~MelonDsLanPopupLocalizer() override
    {
        qApp->removeEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!IsMenuTranslationActive() || event->type() != QEvent::Show || !m_owner)
            return QObject::eventFilter(watched, event);

        if (auto* box = qobject_cast<QMessageBox*>(watched))
        {
            if (box->parentWidget() != m_owner)
                return QObject::eventFilter(watched, event);

            const QString text = box->text();
            const QString translated = Tr(text);
            if (translated != text)
                box->setText(translated);
        }
        else if (auto* input = qobject_cast<QInputDialog*>(watched))
        {
            if (input->parentWidget() != m_owner)
                return QObject::eventFilter(watched, event);

            input->setWindowTitle(Tr(input->windowTitle()));
            input->setLabelText(Tr(input->labelText()));
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QWidget> m_owner;
};

class MelonDsLanClientDiscoveryLocalizer final : public QObject
{
public:
    MelonDsLanClientDiscoveryLocalizer(QTreeView* tree, QWidget* parent)
        : QObject(parent)
        , m_tree(tree)
    {
        m_timer.setInterval(500);
        connect(&m_timer, &QTimer::timeout, this, &MelonDsLanClientDiscoveryLocalizer::relocalizeStatusColumn);
        m_timer.start();
        relocalizeStatusColumn();
    }

private:
    void relocalizeStatusColumn()
    {
        if (!IsMenuTranslationActive() || !m_tree)
            return;

        auto* model = qobject_cast<QStandardItemModel*>(m_tree->model());
        if (!model)
            return;

        for (int row = 0; row < model->rowCount(); ++row)
        {
            QStandardItem* item = model->item(row, 2);
            if (!item)
                continue;

            static constexpr int kLanStatusSourceRole = Qt::UserRole + 30002;
            const QVariant stored = item->data(kLanStatusSourceRole);
            const QString source = stored.isValid() ? stored.toString() : item->text();
            if (!stored.isValid())
                item->setData(source, kLanStatusSourceRole);
            item->setText(Tr(source));
        }
    }

    QPointer<QTreeView> m_tree;
    QTimer m_timer;
};

void ensureLanRuntimeHooks(QWidget* dialog)
{
    if (!dialog || dialog->property("_melonprime_lan_runtime_hook").toBool())
        return;

    dialog->setProperty("_melonprime_lan_runtime_hook", true);
    new MelonDsLanPopupLocalizer(dialog);
}

void wireMelonDsLANDialogLabels(QWidget* dialog)
{
    if (!dialog || !IsMenuTranslationActive())
        return;

    const QString dialogName = dialog->objectName();

    auto localizeLanWarning = [](QLabel* label)
    {
        if (!label)
            return;
        label->setText(QStringLiteral("<html><head/><body><p>%1</p><p>%2</p></body></html>")
            .arg(Tr(QStringLiteral("Warning: LAN requires low network latency to work.")))
            .arg(Tr(QStringLiteral("Do not expect it to work through a VPN or any sort of tunnel."))));
    };

    if (dialogName == QStringLiteral("LANStartHostDialog"))
    {
        localizeLanWarning(dialog->findChild<QLabel*>(QStringLiteral("label_3")));
        ensureLanRuntimeHooks(dialog);
        return;
    }

    if (dialogName != QStringLiteral("LANStartClientDialog"))
        return;

    localizeLanWarning(dialog->findChild<QLabel*>(QStringLiteral("label_2")));
    ensureLanRuntimeHooks(dialog);

    if (auto* box = dialog->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox")))
    {
        if (auto* ok = box->button(QDialogButtonBox::Ok))
        {
            const QString source = SourceObjectPropertyText(
                ok,
                "_melonprime_src_text",
                QStringLiteral("Connect"));
            ok->setText(Tr(source));
        }

        for (QAbstractButton* btn : box->buttons())
        {
            const QString source = SourceObjectPropertyText(
                btn,
                "_melonprime_src_text",
                btn->text());
            if (source == QStringLiteral("Direct connect..."))
                btn->setText(Tr(source));
        }
    }

    if (auto* tree = dialog->findChild<QTreeView*>(QStringLiteral("tvAvailableGames")))
    {
        if (auto* model = qobject_cast<QStandardItemModel*>(tree->model()))
        {
            const QStringList sourceHeaders = SourcePropertyTextList(
                tree,
                "_melonprime_lan_client_headers",
                {
                    QStringLiteral("Name"),
                    QStringLiteral("Players"),
                    QStringLiteral("Status"),
                    QStringLiteral("Host IP"),
                });
            QStringList translated;
            translated.reserve(sourceHeaders.size());
            for (const QString& header : sourceHeaders)
                translated.append(Tr(header));
            model->setHorizontalHeaderLabels(translated);
        }

        new MelonDsLanClientDiscoveryLocalizer(tree, dialog);
    }
}

class MelonDsDialogShowLocalizer final : public QObject
{
public:
    explicit MelonDsDialogShowLocalizer(QWidget* dialog)
        : QObject(dialog)
        , m_dialog(dialog)
    {
        m_dialog->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != m_dialog || m_done)
            return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::Show)
        {
            m_done = true;
            LocalizeMelonDsDialog(m_dialog);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_dialog;
    bool m_done = false;
};

} // namespace

void InstallMelonDsDialogShowLocalizer(QWidget* dialog)
{
    if (!dialog || !IsMenuTranslationActive())
        return;
    new MelonDsDialogShowLocalizer(dialog);
}

void LocalizeMelonDsDialog(QWidget* dialog)
{
    if (!IsMenuTranslationActive() || !dialog)
        return;
    LocalizeWidgetTree(dialog);
    wireMelonDsLANDialogLabels(dialog);
    wireMelonDsDialogDynamicLabels(dialog);
}

} // namespace MelonPrime::UiText
