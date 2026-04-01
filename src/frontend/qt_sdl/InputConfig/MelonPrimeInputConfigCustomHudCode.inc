namespace
{
template<typename T>
QList<T*> SortedChildren(QWidget* parent)
{
    QList<T*> widgets = parent->findChildren<T*>();
    std::sort(widgets.begin(), widgets.end(), [](const T* a, const T* b)
    {
        return a->objectName() < b->objectName();
    });
    return widgets;
}

template<typename TWidget, typename TGetter>
void ExportWidgetTable(toml::table& out, QWidget* parent, TGetter getter)
{
    for (TWidget* widget : SortedChildren<TWidget>(parent))
    {
        const QString name = widget->objectName();
        if (name.isEmpty() || name.startsWith("qt_"))
            continue;

        out[name.toStdString()] = getter(widget);
    }
}

template<typename TWidget, typename TSetter>
void ImportWidgetTable(const toml::value& root, const char* tableName, QWidget* parent, TSetter setter)
{
    if (!root.contains(tableName))
        return;

    const toml::value& tableValue = root.at(tableName);
    if (!tableValue.is_table())
        return;

    for (const auto& [key, value] : tableValue.as_table())
    {
        if (auto* widget = parent->findChild<TWidget*>(QString::fromStdString(key)))
            setter(widget, value);
    }
}
}

void MelonPrimeInputConfig::setupCustomHudCode()
{
    refreshCustomHudCodeOutput();
    setCustomHudCodeStatus("Generate sharable TOML for the current Custom HUD settings, or paste TOML below to apply it.", false);

    connect(ui->btnMetroidCustomHudCodeGenerate, &QPushButton::clicked, this, [this]
    {
        refreshCustomHudCodeOutput();
        setCustomHudCodeStatus("Output refreshed from the current Custom HUD settings.", false);
    });

    connect(ui->btnMetroidCustomHudCodeCopy, &QPushButton::clicked, this, [this]
    {
        QGuiApplication::clipboard()->setText(ui->pteMetroidCustomHudCodeOutput->toPlainText());
        setCustomHudCodeStatus("Custom HUD code copied to the clipboard.", false);
    });

    connect(ui->btnMetroidCustomHudCodeUseOutput, &QPushButton::clicked, this, [this]
    {
        ui->pteMetroidCustomHudCodeInput->setPlainText(ui->pteMetroidCustomHudCodeOutput->toPlainText());
        setCustomHudCodeStatus("Output copied into the input box.", false);
    });

    connect(ui->btnMetroidCustomHudCodeApply, &QPushButton::clicked, this, [this]
    {
        QString errorMessage;
        if (!applyCustomHudCode(ui->pteMetroidCustomHudCodeInput->toPlainText(), &errorMessage))
        {
            setCustomHudCodeStatus(errorMessage, true);
            return;
        }

        refreshCustomHudCodeOutput();
        setCustomHudCodeStatus("Custom HUD code applied to the dialog.", false);
    });
}

QString MelonPrimeInputConfig::buildCustomHudCode() const
{
    toml::table root;

    toml::table checkBoxes;
    ExportWidgetTable<QCheckBox>(checkBoxes, ui->tabCrosshair, [](const QCheckBox* widget)
    {
        return widget->isChecked();
    });
    root["check_boxes"] = checkBoxes;

    toml::table comboBoxes;
    ExportWidgetTable<QComboBox>(comboBoxes, ui->tabCrosshair, [](const QComboBox* widget)
    {
        return widget->currentIndex();
    });
    root["combo_boxes"] = comboBoxes;

    toml::table spinBoxes;
    ExportWidgetTable<QSpinBox>(spinBoxes, ui->tabCrosshair, [](const QSpinBox* widget)
    {
        return widget->value();
    });
    root["spin_boxes"] = spinBoxes;

    toml::table doubleSpinBoxes;
    ExportWidgetTable<QDoubleSpinBox>(doubleSpinBoxes, ui->tabCrosshair, [](const QDoubleSpinBox* widget)
    {
        return static_cast<double>(widget->value());
    });
    root["double_spin_boxes"] = doubleSpinBoxes;

    toml::table lineEdits;
    ExportWidgetTable<QLineEdit>(lineEdits, ui->tabCrosshair, [](const QLineEdit* widget)
    {
        return widget->text().toStdString();
    });
    root["line_edits"] = lineEdits;

    return QString::fromStdString(toml::format(toml::value(root)));
}

bool MelonPrimeInputConfig::applyCustomHudCode(const QString& code, QString* errorMessage)
{
    const QString trimmed = code.trimmed();
    if (trimmed.isEmpty())
    {
        if (errorMessage) *errorMessage = "Paste Custom HUD TOML into the input box first.";
        return false;
    }

    const bool prevApplyPreviewEnabled = m_applyPreviewEnabled;

    try
    {
        std::istringstream input(trimmed.toStdString());
        const toml::value root = toml::parse(input, "CustomHudCode");

        if (!root.is_table())
        {
            if (errorMessage) *errorMessage = "The pasted Custom HUD code is not a TOML table.";
            return false;
        }

        m_applyPreviewEnabled = false;

        ImportWidgetTable<QCheckBox>(root, "check_boxes", ui->tabCrosshair, [](QCheckBox* widget, const toml::value& value)
        {
            if (value.is_boolean())
                widget->setChecked(value.as_boolean());
        });

        ImportWidgetTable<QComboBox>(root, "combo_boxes", ui->tabCrosshair, [](QComboBox* widget, const toml::value& value)
        {
            if (value.is_integer())
                widget->setCurrentIndex(static_cast<int>(value.as_integer()));
        });

        ImportWidgetTable<QSpinBox>(root, "spin_boxes", ui->tabCrosshair, [](QSpinBox* widget, const toml::value& value)
        {
            if (value.is_integer())
                widget->setValue(static_cast<int>(value.as_integer()));
        });

        ImportWidgetTable<QDoubleSpinBox>(root, "double_spin_boxes", ui->tabCrosshair, [](QDoubleSpinBox* widget, const toml::value& value)
        {
            if (value.is_floating())
                widget->setValue(value.as_floating());
            else if (value.is_integer())
                widget->setValue(static_cast<double>(value.as_integer()));
        });

        ImportWidgetTable<QLineEdit>(root, "line_edits", ui->tabCrosshair, [](QLineEdit* widget, const toml::value& value)
        {
            if (value.is_string())
                widget->setText(QString::fromStdString(value.as_string()));
        });

        m_applyPreviewEnabled = prevApplyPreviewEnabled;
        applyVisualPreview();
        return true;
    }
    catch (const std::exception& ex)
    {
        m_applyPreviewEnabled = prevApplyPreviewEnabled;
        if (errorMessage) *errorMessage = QString("Failed to load Custom HUD code: %1").arg(QString::fromUtf8(ex.what()));
        return false;
    }
}

void MelonPrimeInputConfig::refreshCustomHudCodeOutput()
{
    ui->pteMetroidCustomHudCodeOutput->setPlainText(buildCustomHudCode());
}

void MelonPrimeInputConfig::setCustomHudCodeStatus(const QString& text, bool isError)
{
    ui->labelMetroidCustomHudCodeStatus->setText(text);
    ui->labelMetroidCustomHudCodeStatus->setStyleSheet(isError ? "color: #b00020;" : "color: #2e7d32;");
}


