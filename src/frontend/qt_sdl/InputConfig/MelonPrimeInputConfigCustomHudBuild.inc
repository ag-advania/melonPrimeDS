// Unity fragment included by MelonPrimeInputConfig.cpp; do not add to CMakeLists.

    QVBoxLayout* vlay = ui->crosshairVLayout;
    const int spacerIdx = vlay->count() - 1;
    int insertPos = spacerIdx;

    // ── Widget factory: creates one widget for a HudWidgetProp and adds it to a QFormLayout ──
    auto createPropWidget = [&](QWidget* parent, QFormLayout* form, const HudWidgetProp& p) {
        QString objName = cfgKeyToObjName(p.cfgKey);

        switch (p.type) {
        case HWType::Label: {
            auto* lbl = new QLabel(QString::fromUtf8(p.label), parent);
            lbl->setWordWrap(true);
            lbl->setStyleSheet(QStringLiteral("color: #999;"));
            form->addRow(lbl);
            break;
        }
        case HWType::Bool: {
            auto* cb = new QCheckBox(parent);
            cb->setObjectName(objName);
            cb->setChecked(instcfg.GetBool(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), cb);
            m_hudWidgets[p.cfgKey] = cb;
            connect(cb, &QCheckBox::toggled, this, [this, key = std::string(p.cfgKey)](bool val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetBool(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Int: {
            auto* rowW = new QWidget(parent);
            auto* hlay = new QHBoxLayout(rowW);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(4);

            auto* slider = new QSlider(Qt::Horizontal, rowW);
            slider->setRange(p.min, p.max);
            slider->setSingleStep(p.step);
            slider->setPageStep(p.step * 5);
            slider->setValue(instcfg.GetInt(p.cfgKey));

            auto* sb = new QSpinBox(rowW);
            sb->setObjectName(objName);
            sb->setRange(p.min, p.max);
            sb->setSingleStep(p.step);
            sb->setValue(instcfg.GetInt(p.cfgKey));
            sb->setFixedWidth(78);

            hlay->addWidget(slider, 1);
            hlay->addWidget(sb);

            // keep slider and spinbox in sync
            connect(slider, &QSlider::valueChanged, sb, &QSpinBox::setValue);
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);

            form->addRow(QString::fromUtf8(p.label), rowW);
            m_hudWidgets[p.cfgKey] = sb;
            connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Float: {
            auto* dsb = new QDoubleSpinBox(parent);
            dsb->setObjectName(objName);
            dsb->setRange(0.0, 1.0);
            dsb->setSingleStep(0.05);
            dsb->setDecimals(2);
            dsb->setValue(instcfg.GetDouble(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), dsb);
            m_hudWidgets[p.cfgKey] = dsb;
            connect(dsb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key = std::string(p.cfgKey)](double val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetDouble(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Double: {
            auto* dsb = new QDoubleSpinBox(parent);
            dsb->setObjectName(objName);
            dsb->setRange(p.min * 0.01, p.max * 0.01);
            dsb->setSingleStep(p.step * 0.01);
            dsb->setDecimals(2);
            dsb->setValue(instcfg.GetDouble(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), dsb);
            m_hudWidgets[p.cfgKey] = dsb;
            connect(dsb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key = std::string(p.cfgKey)](double val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetDouble(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::String: {
            auto* le = new QLineEdit(parent);
            le->setObjectName(objName);
            le->setText(QString::fromStdString(instcfg.GetString(p.cfgKey)));
            form->addRow(QString::fromUtf8(p.label), le);
            m_hudWidgets[p.cfgKey] = le;
            connect(le, &QLineEdit::textChanged, this, [this, key = std::string(p.cfgKey)](const QString& val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetString(key, val.toStdString());
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::FontMode: {
            // 0=Default (MPH), 1=System Font, 2=Font File. Stored as int (generic save works).
            // The System/File rows are enabled by mode via the wiring block after the build loop.
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Default (MPH)"));
            combo->addItem(QStringLiteral("System Font"));
            combo->addItem(QStringLiteral("Font File"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::ZoomTransitionStyle: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Fade"));
            combo->addItem(QStringLiteral("Staged"));
            combo->addItem(QStringLiteral("Glitch"));
            combo->addItem(QStringLiteral("Glitch2"));
            combo->addItem(QStringLiteral("Snap"));
            combo->addItem(QStringLiteral("Digital"));
            combo->addItem(QStringLiteral("Pulse Wave"));
            combo->addItem(QStringLiteral("Magic Circle"));
            combo->addItem(QStringLiteral("SF Movie"));
            combo->addItem(QStringLiteral("Tactical Lock"));
            combo->addItem(QStringLiteral("Sniper Optics"));
            combo->addItem(QStringLiteral("Drone LIDAR"));
            combo->addItem(QStringLiteral("Beam Charge"));
            combo->setCurrentIndex(std::clamp(instcfg.GetInt(p.cfgKey), p.min, p.max));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::FontFamily: {
            // System-font picker: a "pick font" button (left) + an editable QFontComboBox (right).
            // QFontComboBox is a QComboBox, so all generic loops that iterate m_hudWidgets
            // special-case it (family string) BEFORE the QComboBox branch.
            auto* rowW = new QWidget(parent);
            auto* hlay = new QHBoxLayout(rowW);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(4);

            auto* pick = new QPushButton(QStringLiteral("Aa…"), rowW);
            pick->setToolTip(QStringLiteral("Pick a system font…"));
            pick->setFixedWidth(40);

            auto* combo = new QFontComboBox(rowW);
            combo->setObjectName(objName);
            const QString fam = QString::fromStdString(instcfg.GetString(p.cfgKey));
            if (!fam.isEmpty())
                combo->setCurrentFont(QFont(fam));

            hlay->addWidget(pick);      // left: font picker button
            hlay->addWidget(combo, 1);  // right: editable family combo

            form->addRow(QString::fromUtf8(p.label), rowW);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, &QFontComboBox::currentFontChanged, this, [this, key = std::string(p.cfgKey)](const QFont& f) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetString(key, f.family().toStdString());
                invalidateHudAndRefreshPreviews();
            });
            connect(pick, &QPushButton::clicked, this, [this, combo]() {
                static const int kW[9] = { 100, 200, 300, 400, 500, 600, 700, 800, 900 };
                auto& cfg = emuInstance->getLocalConfig();

                // Open the dialog pre-filled with the current HUD font settings.
                QFont init = combo->currentFont();
                const int curSize = cfg.GetInt(MP_HUD_PROP_KEY_HudFontSize);
                if (curSize > 0) init.setPointSize(std::clamp(curSize, 4, 64));
                init.setWeight(static_cast<QFont::Weight>(kW[std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_HudFontWeight), 0, 8)]));
                init.setItalic(cfg.GetBool(MP_HUD_PROP_KEY_HudFontItalic));
                init.setUnderline(cfg.GetBool(MP_HUD_PROP_KEY_HudFontUnderline));
                init.setStrikeOut(cfg.GetBool(MP_HUD_PROP_KEY_HudFontStrikeOut));

                bool ok = false;
                const QFont chosen = QFontDialog::getFont(
                    &ok, init, this, MelonPrime::UiText::Tr("Select HUD Font"));
                if (!ok) return;

                // Apply the whole selection back into the section's widgets, which write config
                // + refresh previews via their own change handlers.
                auto setCombo = [this](const char* key, int idx) {
                    auto it = m_hudWidgets.find(std::string(key));
                    if (it != m_hudWidgets.end())
                        if (auto* c = qobject_cast<QComboBox*>(it->second)) c->setCurrentIndex(idx);
                };
                auto setSpin = [this](const char* key, int v) {
                    auto it = m_hudWidgets.find(std::string(key));
                    if (it != m_hudWidgets.end())
                        if (auto* sb = qobject_cast<QSpinBox*>(it->second)) sb->setValue(v);
                };
                auto setCheck = [this](const char* key, bool v) {
                    auto it = m_hudWidgets.find(std::string(key));
                    if (it != m_hudWidgets.end())
                        if (auto* cb = qobject_cast<QCheckBox*>(it->second)) cb->setChecked(v);
                };

                combo->setCurrentFont(chosen);  // family -> currentFontChanged writes + refreshes

                int sz = chosen.pointSize();
                if (sz <= 0) sz = chosen.pixelSize();
                if (sz > 0) setSpin(MP_HUD_PROP_KEY_HudFontSize, std::clamp(sz, 4, 64));

                const int wval = chosen.weight();
                int wIdx = 3, wBest = 1 << 30;
                for (int i = 0; i < 9; ++i) {
                    const int d = (kW[i] > wval) ? (kW[i] - wval) : (wval - kW[i]);
                    if (d < wBest) { wBest = d; wIdx = i; }
                }
                setCombo(MP_HUD_PROP_KEY_HudFontWeight, wIdx);

                setCheck(MP_HUD_PROP_KEY_HudFontItalic,    chosen.italic());
                setCheck(MP_HUD_PROP_KEY_HudFontUnderline, chosen.underline());
                setCheck(MP_HUD_PROP_KEY_HudFontStrikeOut, chosen.strikeOut());
            });
            break;
        }
        case HWType::FontFile: {
            // QLineEdit (path, registered in m_hudWidgets -> generic string save) + Browse button.
            auto* rowW = new QWidget(parent);
            auto* hlay = new QHBoxLayout(rowW);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(4);

            auto* le = new QLineEdit(rowW);
            le->setObjectName(objName);
            le->setText(QString::fromStdString(instcfg.GetString(p.cfgKey)));

            auto* btn = new QPushButton(QStringLiteral("Browse…"), rowW);
            btn->setFixedWidth(78);

            hlay->addWidget(le, 1);
            hlay->addWidget(btn);

            form->addRow(QString::fromUtf8(p.label), rowW);
            m_hudWidgets[p.cfgKey] = le;
            connect(le, &QLineEdit::textChanged, this, [this, key = std::string(p.cfgKey)](const QString& val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetString(key, val.toStdString());
                invalidateHudAndRefreshPreviews();
            });
            connect(btn, &QPushButton::clicked, this, [this, le]() {
                const QString path = QFileDialog::getOpenFileName(
                    this, MelonPrime::UiText::Tr("Select HUD Font File"), QString(),
                    MelonPrime::UiText::Tr("Font files (*.ttf *.otf *.ttc);;All files (*)"));
                if (!path.isEmpty())
                    le->setText(path);  // textChanged handler writes config + refreshes
            });
            break;
        }
        case HWType::FontWeight: {
            // Stores the weight index 0..8 (Thin..Black); the resolver maps it to QFont::Weight.
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            static const char* kWeightLabels[9] = {
                "Thin", "Extra Light", "Light", "Normal", "Medium",
                "Semi Bold", "Bold", "Extra Bold", "Black" };
            for (int wgt = 0; wgt < 9; ++wgt)
                combo->addItem(QString::fromUtf8(kWeightLabels[wgt]));
            combo->setCurrentIndex(std::clamp(instcfg.GetInt(p.cfgKey), 0, 8));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Anchor9: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            for (int a = 0; a < 9; ++a)
                combo->addItem(QString::fromUtf8(kAnchorLabels[a]));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Align3: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            for (int a = 0; a < 3; ++a)
                combo->addItem(QString::fromUtf8(kAlignLabels[a]));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::DotShape: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Square"));
            combo->addItem(QStringLiteral("Circle"));
            combo->setCurrentIndex(std::clamp(instcfg.GetInt(p.cfgKey), 0, 1));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::HorizVert: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Horizontal"));
            combo->addItem(QStringLiteral("Vertical"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::RelIndep: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Relative to Text"));
            combo->addItem(QStringLiteral("Independent"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::PosMode3: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Gauge \u2192 Text"));
            combo->addItem(QStringLiteral("Independent"));
            combo->addItem(QStringLiteral("Text \u2192 Gauge"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::GaugeAnchor5: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Below"));
            combo->addItem(QStringLiteral("Above"));
            combo->addItem(QStringLiteral("Right"));
            combo->addItem(QStringLiteral("Left"));
            combo->addItem(QStringLiteral("Center"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::AnchorY3: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Top"));
            combo->addItem(QStringLiteral("Center"));
            combo->addItem(QStringLiteral("Bottom"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::LabelPos5: {
            auto* combo = new QComboBox(parent);
            combo->setObjectName(objName);
            combo->addItem(QStringLiteral("Above"));
            combo->addItem(QStringLiteral("Below"));
            combo->addItem(QStringLiteral("Left"));
            combo->addItem(QStringLiteral("Right"));
            combo->addItem(QStringLiteral("Center"));
            combo->setCurrentIndex(instcfg.GetInt(p.cfgKey));
            form->addRow(QString::fromUtf8(p.label), combo);
            m_hudWidgets[p.cfgKey] = combo;
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key = std::string(p.cfgKey)](int val) {
                if (!m_applyPreviewEnabled) return;
                emuInstance->getLocalConfig().SetInt(key, val);
                invalidateHudAndRefreshPreviews();
            });
            break;
        }
        case HWType::Color3: {
            // Outer container (vertical: preset combo on top, RGB spinboxes below)
            auto* colWidget = new QWidget(parent);
            auto* vlay = new QVBoxLayout(colWidget);
            vlay->setContentsMargins(0, 0, 0, 0);
            vlay->setSpacing(2);

            // ── Preset combo ──
            auto* presetCombo = new QComboBox(colWidget);
            static const int kPaletteCount = ArrayCount(kHudColorPalette);
            // Build pixmap icon for each preset colour
            for (int pi = 0; pi < kPaletteCount; ++pi) {
                const PresetColor& pc = kHudColorPalette[pi];
                QPixmap pm(14, 14);
                pm.fill(QColor(pc.r, pc.g, pc.b));
                presetCombo->addItem(QIcon(pm), QString::fromUtf8(kHudColorPaletteNames[pi]));
            }
            presetCombo->addItem(QStringLiteral("Custom"));   // index == kPaletteCount

            // ── RGB spinboxes row ──
            auto* rgbRow = new QWidget(colWidget);
            auto* hlay = new QHBoxLayout(rgbRow);
            hlay->setContentsMargins(0, 0, 0, 0);
            hlay->setSpacing(4);

            auto* sbR = new QSpinBox(rgbRow); sbR->setRange(0, 255); sbR->setPrefix(QStringLiteral("R:")); sbR->setObjectName(cfgKeyToObjName(p.cfgKey));
            auto* sbG = new QSpinBox(rgbRow); sbG->setRange(0, 255); sbG->setPrefix(QStringLiteral("G:")); sbG->setObjectName(cfgKeyToObjName(p.cfgKeyG));
            auto* sbB = new QSpinBox(rgbRow); sbB->setRange(0, 255); sbB->setPrefix(QStringLiteral("B:")); sbB->setObjectName(cfgKeyToObjName(p.cfgKeyB));

            sbR->setValue(instcfg.GetInt(p.cfgKey));
            sbG->setValue(instcfg.GetInt(p.cfgKeyG));
            sbB->setValue(instcfg.GetInt(p.cfgKeyB));

            auto* swatch = new QPushButton(rgbRow);
            swatch->setFixedSize(24, 24);
            auto updateSwatch = [sbR, sbG, sbB, swatch, presetCombo]() {
                swatch->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border: 1px solid #888;")
                    .arg(sbR->value()).arg(sbG->value()).arg(sbB->value()));
                // Sync preset combo
                int match = kPaletteCount; // default: Custom
                for (int pi = 0; pi < kPaletteCount; ++pi) {
                    const PresetColor& pc = kHudColorPalette[pi];
                    if (sbR->value() == pc.r && sbG->value() == pc.g && sbB->value() == pc.b) {
                        match = pi; break;
                    }
                }
                const bool old = presetCombo->blockSignals(true);
                presetCombo->setCurrentIndex(match);
                presetCombo->blockSignals(old);
            };
            updateSwatch();

            hlay->addWidget(sbR); hlay->addWidget(sbG); hlay->addWidget(sbB); hlay->addWidget(swatch);

            vlay->addWidget(presetCombo);
            vlay->addWidget(rgbRow);
            form->addRow(QString::fromUtf8(p.label), colWidget);

            m_hudWidgets[p.cfgKey]  = sbR;
            m_hudWidgets[p.cfgKeyG] = sbG;
            m_hudWidgets[p.cfgKeyB] = sbB;

            // Preset combo → update spinboxes + write config + refresh preview
            connect(presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, sbR, sbG, sbB, updateSwatch,
                 kR = std::string(p.cfgKey), kG = std::string(p.cfgKeyG), kB = std::string(p.cfgKeyB)](int idx) {
                    if (idx < 0 || idx >= kPaletteCount) return;
                    const PresetColor& pc = kHudColorPalette[idx];
                    const bool oldR = sbR->blockSignals(true);
                    const bool oldG = sbG->blockSignals(true);
                    const bool oldB = sbB->blockSignals(true);
                    sbR->setValue(pc.r); sbG->setValue(pc.g); sbB->setValue(pc.b);
                    sbR->blockSignals(oldR); sbG->blockSignals(oldG); sbB->blockSignals(oldB);
                    updateSwatch();
                    if (!m_applyPreviewEnabled) return;
                    emuInstance->getLocalConfig().SetInt(kR, pc.r);
                    emuInstance->getLocalConfig().SetInt(kG, pc.g);
                    emuInstance->getLocalConfig().SetInt(kB, pc.b);
                    invalidateHudAndRefreshPreviews();
                });
            // Spinbox → write config + refresh preview

            auto connectColorSpin = [this, updateSwatch](QSpinBox* sb, const std::string& key) {
                connect(sb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key, updateSwatch](int val) {
                    updateSwatch();
                    if (!m_applyPreviewEnabled) return;
                    emuInstance->getLocalConfig().SetInt(key, val);
                    invalidateHudAndRefreshPreviews();
                });
            };
            connectColorSpin(sbR, std::string(p.cfgKey));
            connectColorSpin(sbG, std::string(p.cfgKeyG));
            connectColorSpin(sbB, std::string(p.cfgKeyB));

            connect(swatch, &QPushButton::clicked, this, [this, sbR, sbG, sbB, updateSwatch]() {
                QColor init(sbR->value(), sbG->value(), sbB->value());
                QColor c = MelonPrime::ColorDialogPrefs::getColor(
                    this,
                    init,
                    MelonPrime::UiText::Tr("Pick Color"));
                if (c.isValid()) {
                    sbR->setValue(c.red());
                    sbG->setValue(c.green());
                    sbB->setValue(c.blue());
                    updateSwatch();
                }
            });
            break;
        }
        } // switch
    };

    // ── Helper: populate a QFormLayout from a props array ──
    auto populateProps = [&](QWidget* parent, QFormLayout* form, const HudWidgetProp* props, int count) {
        for (int i = 0; i < count; ++i)
            createPropWidget(parent, form, props[i]);
    };

    // ── Helper: create a collapsible sub-section toggle + body ──
    // Returns the section body widget. Adds toggle+body to destLayout.
    auto makeSubToggle = [&](QVBoxLayout* destLayout, const char* title, const char* cfgKey, int indent) -> QWidget*
    {
        auto* btn = new QPushButton(this);
        btn->setCheckable(true);
        btn->setFlat(true);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; font-weight: bold; padding: 3px; padding-left: %1px; } "
            "QPushButton::checked { font-weight: bold; }").arg(indent));

        bool expanded = instcfg.GetBool(cfgKey);
        btn->setChecked(expanded);
        const QString label = QString::fromUtf8(title);
        btn->setText((expanded ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + label);

        connect(btn, &QPushButton::toggled, [btn, label](bool checked) {
            btn->setText(
                (checked ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 "))
                + MelonPrime::UiText::Tr(label));
        });

        m_hudToggles.push_back({btn, std::string(cfgKey)});
        destLayout->addWidget(btn);

        auto* body = new QWidget(this);
        body->setVisible(expanded);
        connect(btn, &QPushButton::toggled, body, &QWidget::setVisible);
        destLayout->addWidget(body);

        return body;
    };

    // ── Build sub-section recursively (handles HudSubSec with optional children) ──
    std::function<void(QVBoxLayout*, const HudSubSec&, int)> buildSubSection;
    buildSubSection = [&](QVBoxLayout* destLayout, const HudSubSec& sub, int indent)
    {
        QWidget* body = makeSubToggle(destLayout, sub.title, sub.cfgToggleKey, indent);
        auto* bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(indent + 8, 2, 2, 2);
        bodyLayout->setSpacing(2);

        // Direct props
        if (sub.props && sub.propCount > 0) {
            auto* form = new QFormLayout();
            form->setContentsMargins(0, 0, 0, 0);
            form->setVerticalSpacing(4);
            populateProps(body, form, sub.props, sub.propCount);
            bodyLayout->addLayout(form);
        }

        // Nested children (e.g., Rank/Time Left/Time Limit inside "Rank / Time")
        for (int ci = 0; ci < sub.childCount; ++ci)
            buildSubSection(bodyLayout, sub.children[ci], indent + 8);
    };

    // ── Build main sections ──
    for (int si = 0; si < kHudMainSectionCount; ++si) {
        const HudMainSec& sec = kHudMainSections[si];

        // --- Main toggle button (top-level, bold + large) ---
        auto* mainBtn = new QPushButton(this);
        mainBtn->setCheckable(true);
        mainBtn->setFlat(true);
        mainBtn->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; font-weight: bold; font-size: 11pt; padding: 6px 4px; } "
            "QPushButton::checked { font-weight: bold; }"));

        bool expanded = instcfg.GetBool(sec.cfgToggleKey);
        mainBtn->setChecked(expanded);
        const QString label = QString::fromUtf8(sec.title);
        mainBtn->setText((expanded ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 ")) + label);

        connect(mainBtn, &QPushButton::toggled, [mainBtn, label](bool checked) {
            mainBtn->setText(
                (checked ? QString::fromUtf8("\u25BC ") : QString::fromUtf8("\u25B6 "))
                + MelonPrime::UiText::Tr(label));
        });

        m_hudToggles.push_back({mainBtn, std::string(sec.cfgToggleKey)});
        vlay->insertWidget(insertPos++, mainBtn);

        // --- Main section body with HBoxLayout: left = sub-sections, right = preview ---
        auto* mainBody = new QWidget(this);
        mainBody->setVisible(expanded);
        connect(mainBtn, &QPushButton::toggled, mainBody, &QWidget::setVisible);

        auto* hbox = new QHBoxLayout(mainBody);
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(4);

        // Left column: direct properties + sub-sections
        auto* leftCol = new QWidget(mainBody);
        auto* bodyLayout = new QVBoxLayout(leftCol);
        bodyLayout->setContentsMargins(8, 4, 4, 4);
        bodyLayout->setSpacing(2);

        // Direct properties (e.g., crosshair color/outline at group level)
        if (sec.directProps && sec.directPropCount > 0) {
            auto* form = new QFormLayout();
            form->setContentsMargins(0, 0, 0, 0);
            form->setVerticalSpacing(4);
            populateProps(leftCol, form, sec.directProps, sec.directPropCount);
            bodyLayout->addLayout(form);
        }

        // Sub-sections
        for (int si2 = 0; si2 < sec.subCount; ++si2)
            buildSubSection(bodyLayout, sec.subs[si2], 8);

        bodyLayout->addStretch();
        hbox->addWidget(leftCol, 1);

        // Right column: preview widget(s)
        if (sec.previewKind == 1) {
            // Crosshair: stack the normal crosshair preview and the fully-scoped
            // zoom-scope reticle preview, each with a small caption.
            auto* col = new QWidget(mainBody);
            auto* colLay = new QVBoxLayout(col);
            colLay->setContentsMargins(0, 0, 0, 0);
            colLay->setSpacing(2);
            auto addLabeledPreview = [&](const char* caption, HudPreviewWidget* pw) {
                auto* lbl = new QLabel(QString::fromUtf8(caption), col);
                lbl->setStyleSheet(QStringLiteral("color: #aaa; font-size: 8pt;"));
                colLay->addWidget(lbl);
                colLay->addWidget(pw);
                m_hudPreviews.push_back(pw);
            };
            addLabeledPreview("Normal", new CrosshairPreviewWidget(emuInstance, col));
            addLabeledPreview("Zoomed (Scope)", new ZoomScopePreviewWidget(emuInstance, col));
            colLay->addStretch();
            hbox->addWidget(col, 0);
        } else {
            HudPreviewWidget* preview = nullptr;
            switch (sec.previewKind) {
            case 2: preview = new HpAmmoPreviewWidget(emuInstance, mainBody); break;
            case 3: preview = new MatchStatusPreviewWidget(emuInstance, mainBody); break;
            case 4: preview = new RadarPreviewWidget(emuInstance, mainBody); break;
            }
            if (preview) {
                hbox->addWidget(preview, 0);
                m_hudPreviews.push_back(preview);
            }
        }

        vlay->insertWidget(insertPos++, mainBody);
    }

    // ── PosMode3 grayout: disable irrelevant rows when Position Mode changes ──
    auto wirePosModeGrayout = [this](
        const char* modeKey,
        std::vector<std::string> mode0Keys,
        std::vector<std::string> mode12Keys,
        std::vector<std::string> mode2Keys)
    {
        auto it0 = m_hudWidgets.find(std::string(modeKey));
        if (it0 == m_hudWidgets.end()) return;
        auto* combo = qobject_cast<QComboBox*>(it0->second);
        if (!combo) return;

        auto applyState = [this, mode0Keys, mode12Keys, mode2Keys](int mode) {
            auto setRowEnabled = [this](const std::string& key, bool enabled) {
                auto it = m_hudWidgets.find(key);
                if (it == m_hudWidgets.end()) return;
                QWidget* w = it->second;
                // P_INT rows wrap slider+spinbox in a QHBoxLayout container.
                // Operate on that container so both children are affected and
                // labelForField can find the row via the parent QFormLayout.
                QWidget* rowWidget = w;
                if (w->parentWidget() && qobject_cast<QHBoxLayout*>(w->parentWidget()->layout()))
                    rowWidget = w->parentWidget();
                rowWidget->setEnabled(enabled);
                if (QWidget* par = rowWidget->parentWidget())
                    if (auto* fl = qobject_cast<QFormLayout*>(par->layout()))
                        if (QWidget* lw = fl->labelForField(rowWidget))
                            lw->setEnabled(enabled);
            };
            for (const auto& k : mode0Keys)  setRowEnabled(k, mode == 0);
            for (const auto& k : mode12Keys) setRowEnabled(k, mode >= 1);
            for (const auto& k : mode2Keys)  setRowEnabled(k, mode == 2);
        };

        applyState(combo->currentIndex());
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [applyState](int val) { applyState(val); });
    };

    wirePosModeGrayout(MP_HUD_PROP_KEY_HudHpGaugePosMode,
        {MP_HUD_PROP_KEY_HudHpGaugeAnchor,
         MP_HUD_PROP_KEY_HudHpGaugeOffsetX,
         MP_HUD_PROP_KEY_HudHpGaugeOffsetY},
        {MP_HUD_PROP_KEY_HudHpGaugePosX,
         MP_HUD_PROP_KEY_HudHpGaugePosY},
        {MP_HUD_PROP_KEY_HudHpTextAnchor,
         MP_HUD_PROP_KEY_HudHpTextOffsetX,
         MP_HUD_PROP_KEY_HudHpTextOffsetY});

    wirePosModeGrayout(MP_HUD_PROP_KEY_HudAmmoGaugePosMode,
        {MP_HUD_PROP_KEY_HudAmmoGaugeAnchor,
         MP_HUD_PROP_KEY_HudAmmoGaugeOffsetX,
         MP_HUD_PROP_KEY_HudAmmoGaugeOffsetY},
        {MP_HUD_PROP_KEY_HudAmmoGaugePosX,
         MP_HUD_PROP_KEY_HudAmmoGaugePosY},
        {MP_HUD_PROP_KEY_HudAmmoTextAnchor,
         MP_HUD_PROP_KEY_HudAmmoTextOffsetX,
         MP_HUD_PROP_KEY_HudAmmoTextOffsetY});

    // Weapon Icon Position: mode 0=Relative (offset from text), 1=Independent (own anchor+pos)
    wirePosModeGrayout(MP_HUD_PROP_KEY_HudWeaponIconMode,
        {MP_HUD_PROP_KEY_HudWeaponIconOffsetX,
         MP_HUD_PROP_KEY_HudWeaponIconOffsetY},
        {MP_HUD_PROP_KEY_HudWeaponIconPosAnchor,
         MP_HUD_PROP_KEY_HudWeaponIconPosX,
         MP_HUD_PROP_KEY_HudWeaponIconPosY},
        {});

    // Bomb Icon Position: mode 0=Relative (offset from bomb text), 1=Independent (own anchor+pos)
    wirePosModeGrayout(MP_HUD_PROP_KEY_HudBombLeftIconMode,
        {MP_HUD_PROP_KEY_HudBombLeftIconOfsX,
         MP_HUD_PROP_KEY_HudBombLeftIconOfsY},
        {MP_HUD_PROP_KEY_HudBombLeftIconPosAnchor,
         MP_HUD_PROP_KEY_HudBombLeftIconPosX,
         MP_HUD_PROP_KEY_HudBombLeftIconPosY},
        {});

    // HUD Font: enable the System Font picker only in mode 1, the Font File row only in mode 2.
    {
        auto it = m_hudWidgets.find(std::string(MP_HUD_PROP_KEY_HudFontMode));
        if (it != m_hudWidgets.end()) {
            if (auto* modeCombo = qobject_cast<QComboBox*>(it->second)) {
                auto setRowEnabled = [this](const char* key, bool enabled) {
                    auto wit = m_hudWidgets.find(std::string(key));
                    if (wit == m_hudWidgets.end()) return;
                    QWidget* w = wit->second;
                    // FontFile wraps QLineEdit+Browse in a QHBoxLayout container; operate on it
                    // so both children and the form-row label toggle together.
                    QWidget* rowWidget = w;
                    if (w->parentWidget() && qobject_cast<QHBoxLayout*>(w->parentWidget()->layout()))
                        rowWidget = w->parentWidget();
                    rowWidget->setEnabled(enabled);
                    if (QWidget* par = rowWidget->parentWidget())
                        if (auto* fl = qobject_cast<QFormLayout*>(par->layout()))
                            if (QWidget* lw = fl->labelForField(rowWidget))
                                lw->setEnabled(enabled);
                };
                auto applyFontState = [setRowEnabled](int mode) {
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontFamily,    mode == 1);
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontFile,      mode == 2);
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontSize,      mode != 0);
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontWeight,    mode != 0);
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontItalic,    mode != 0);
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontUnderline, mode != 0);
                    setRowEnabled(MP_HUD_PROP_KEY_HudFontStrikeOut, mode != 0);
                };
                applyFontState(modeCombo->currentIndex());
                connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        this, [applyFontState](int val) { applyFontState(val); });
            }
        }
    }
