#include "pcb_3d_window.h"
#include "../mcad/mcad_exporter.h"
#include "config_manager.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSlider>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>

#include "../items/pcb_item.h"

PCB3DWindow::PCB3DWindow(QGraphicsScene* scene, QWidget* parent)
    : QMainWindow(parent)
    , m_scene(scene) {
    setWindowTitle("PCB 3D Preview - Viora EDA");
    resize(1120, 760);

    m_view = new PCB3DView(this);
    m_view->setScene(scene);
    setCentralWidget(m_view);

    connect(m_view, &PCB3DView::componentPicked, this, &PCB3DWindow::componentPicked);

    setStyleSheet(
        "QMainWindow { background: #10151b; }"
        "QToolBar { background: #151b22; border-bottom: 1px solid #2c3642; spacing: 6px; padding: 6px; }"
        "QToolButton { color: #d0d7de; background: #1f2933; border: 1px solid #2f3a46; border-radius: 6px; padding: 4px 9px; }"
        "QToolButton:hover { background: #273342; }"
        "QToolButton:checked { background: #1f6feb; border-color: #388bfd; color: white; }"
        "QLabel { color: #9fb0c0; font-size: 11px; font-weight: 600; }"
        "QComboBox, QSlider { background: #1f2933; color: #d0d7de; border: 1px solid #2f3a46; border-radius: 5px; }"
    );

    QToolBar* toolbar = addToolBar("3D Controls");
    toolbar->setMovable(false);

    auto addToggle = [&](const QString& name, bool checked, auto slot) {
        QAction* a = toolbar->addAction(name);
        a->setCheckable(true);
        a->setChecked(checked);
        connect(a, &QAction::toggled, this, slot);
        return a;
    };

    toolbar->addWidget(new QLabel("Layers"));
    addToggle("Board", true, [this](bool on) { m_view->setShowSubstrate(on); });
    addToggle("Copper", true, [this](bool on) { m_view->setShowCopper(on); });
    addToggle("Silk", true, [this](bool on) { m_view->setShowSilkscreen(on); });
    addToggle("Grid", false, [this](bool on) { m_view->setShowGrid(on); });
    addToggle("Components", true, [this](bool on) { m_view->setShowComponents(on); });
    addToggle("Raytraced", false, [this](bool on) { m_view->setRaytracingEnabled(on); });

    toolbar->addSeparator();
    addToggle("Selected Only", false, [this](bool on) { m_view->setSelectedOnly(on); });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Projection"));
    QComboBox* projection = new QComboBox(this);
    projection->addItems({"Perspective", "Orthographic"});
    const QString savedProjection =
        ConfigManager::instance().toolProperty("PCB3DView", "ProjectionMode", "Perspective").toString();
    const QString initialProjection =
        (savedProjection.compare("Orthographic", Qt::CaseInsensitive) == 0) ? "Orthographic" : "Perspective";
    projection->setCurrentText(initialProjection);
    toolbar->addWidget(projection);
    connect(projection, &QComboBox::currentTextChanged, this, [this](const QString& mode) {
        ConfigManager::instance().setToolProperty("PCB3DView", "ProjectionMode", mode);
        m_view->setOrthographic(mode.compare("Orthographic", Qt::CaseInsensitive) == 0);
    });
    m_view->setOrthographic(initialProjection.compare("Orthographic", Qt::CaseInsensitive) == 0);

    const bool savedSpaceMouse = ConfigManager::instance()
                                     .toolProperty("PCB3DView", "SpaceMouseEnabled", false)
                                     .toBool();
    QAction* spaceMouse = addToggle("SpaceMouse", savedSpaceMouse, [this](bool on) {
        ConfigManager::instance().setToolProperty("PCB3DView", "SpaceMouseEnabled", on);
        if (!m_view->setSpaceMouseEnabled(on) && on) {
            ConfigManager::instance().setToolProperty("PCB3DView", "SpaceMouseEnabled", false);
        }
    });
    if (!m_view->setSpaceMouseEnabled(savedSpaceMouse) && savedSpaceMouse) {
        spaceMouse->blockSignals(true);
        spaceMouse->setChecked(false);
        spaceMouse->blockSignals(false);
        ConfigManager::instance().setToolProperty("PCB3DView", "SpaceMouseEnabled", false);
    }

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("View"));
    QComboBox* preset = new QComboBox(this);
    preset->addItems({"Isometric", "Top", "Bottom", "Front", "Back", "Flip Board", "Spin CW", "Spin CCW", "Spin Stop"});
    preset->setCurrentText("Isometric");
    toolbar->addWidget(preset);
    connect(preset, &QComboBox::currentTextChanged, this, [this](const QString& p) {
        m_view->setViewPreset(p);
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Net"));
    m_netCombo = new QComboBox(this);
    m_netCombo->setMinimumWidth(170);
    toolbar->addWidget(m_netCombo);
    connect(m_netCombo, &QComboBox::currentTextChanged, this, [this](const QString& n) {
        m_view->setNetFilter(n == "All Nets" ? QString() : n);
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Mask"));
    QSlider* maskAlpha = new QSlider(Qt::Horizontal, this);
    maskAlpha->setRange(5, 100);
    const int savedMaskAlpha = ConfigManager::instance()
                                   .toolProperty("PCB3DView", "MaskAlpha", 100)
                                   .toInt();
    maskAlpha->setValue(std::clamp(savedMaskAlpha, 5, 100));
    maskAlpha->setFixedWidth(110);
    toolbar->addWidget(maskAlpha);
    m_view->setSoldermaskAlpha(float(maskAlpha->value()) / 100.0f);
    connect(maskAlpha, &QSlider::valueChanged, this, [this](int v) {
        const int clamped = std::clamp(v, 5, 100);
        ConfigManager::instance().setToolProperty("PCB3DView", "MaskAlpha", clamped);
        m_view->setSoldermaskAlpha(float(clamped) / 100.0f);
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("X-Ray"));
    QSlider* xray = new QSlider(Qt::Horizontal, this);
    xray->setRange(5, 100);
    xray->setValue(100);
    xray->setFixedWidth(110);
    toolbar->addWidget(xray);
    connect(xray, &QSlider::valueChanged, this, [this](int v) {
        m_view->setSubstrateAlpha(float(v) / 100.0f);
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Explode"));
    QSlider* explode = new QSlider(Qt::Horizontal, this);
    explode->setRange(0, 100);
    const int savedExplodePct = ConfigManager::instance()
                                    .toolProperty("PCB3DView", "ExplodePercent", 0)
                                    .toInt();
    explode->setValue(std::clamp(savedExplodePct, 0, 100));
    explode->setFixedWidth(110);
    toolbar->addWidget(explode);
    m_view->setExplodeAmount(float(explode->value()) * 0.30f); // 0..30 mm lift
    connect(explode, &QSlider::valueChanged, this, [this](int v) {
        const int pct = std::clamp(v, 0, 100);
        ConfigManager::instance().setToolProperty("PCB3DView", "ExplodePercent", pct);
        m_view->setExplodeAmount(float(pct) * 0.30f);
    });

    toolbar->addSeparator();
    toolbar->addWidget(new QLabel("Zoom"));
    QSlider* zoomSlider = new QSlider(Qt::Horizontal, this);
    zoomSlider->setRange(20, 2000);
    zoomSlider->setValue(int(m_view->zoomDistance()));
    zoomSlider->setFixedWidth(130);
    toolbar->addWidget(zoomSlider);
    connect(zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        m_view->setZoomDistance(float(v));
    });
    connect(m_view, &PCB3DView::zoomDistanceChanged, this, [zoomSlider](float distance) {
        const int value = int(std::lround(distance));
        if (zoomSlider->value() == value) return;
        zoomSlider->blockSignals(true);
        zoomSlider->setValue(value);
        zoomSlider->blockSignals(false);
    });

    toolbar->addSeparator();
    QAction* reset = toolbar->addAction("Reset Camera");
    connect(reset, &QAction::triggered, this, [this]() { m_view->resetCamera(); });

    QAction* measureToggle = toolbar->addAction("Measure");
    measureToggle->setCheckable(true);
    measureToggle->setToolTip("Measure distance on board plane (click two points)");
    connect(measureToggle, &QAction::toggled, this, [this](bool on) {
        m_view->setMeasureMode(on);
        if (m_measureLabel) {
            m_measureLabel->setText(on ? "Measure: pick first point" : "Measure: off");
        }
    });
    QAction* clearMeasure = toolbar->addAction("Clear Measure");
    connect(clearMeasure, &QAction::triggered, this, [this]() {
        m_view->clearMeasurement();
        if (m_measureLabel) m_measureLabel->setText("Measure: cleared");
    });

    QAction* shot = toolbar->addAction("Save Image");
    connect(shot, &QAction::triggered, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("Export 3D Snapshot");
        QFormLayout form(&dlg);

        QSpinBox* wSpin = new QSpinBox(&dlg);
        wSpin->setRange(320, 8192);
        wSpin->setValue(std::max(320, m_view->width()));
        QSpinBox* hSpin = new QSpinBox(&dlg);
        hSpin->setRange(240, 8192);
        hSpin->setValue(std::max(240, m_view->height()));

        QComboBox* fmtCombo = new QComboBox(&dlg);
        fmtCombo->addItem("PNG (*.png)", "PNG");
        fmtCombo->addItem("JPG (*.jpg)", "JPG");

        QCheckBox* transparentBg = new QCheckBox("Transparent background (PNG)", &dlg);
        transparentBg->setChecked(true);
        connect(fmtCombo, &QComboBox::currentTextChanged, &dlg, [fmtCombo, transparentBg]() {
            const bool png = (fmtCombo->currentData().toString() == "PNG");
            transparentBg->setEnabled(png);
            if (!png) transparentBg->setChecked(false);
        });

        form.addRow("Width:", wSpin);
        form.addRow("Height:", hSpin);
        form.addRow("Format:", fmtCombo);
        form.addRow("", transparentBg);

        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form.addRow(bb);

        if (dlg.exec() != QDialog::Accepted) return;

        const QString fmt = fmtCombo->currentData().toString();
        QString filter = (fmt == "PNG") ? "PNG (*.png)" : "JPG (*.jpg)";
        QString path = QFileDialog::getSaveFileName(this, "Save 3D Snapshot", QString(), filter);
        if (path.isEmpty()) return;

        if (fmt == "PNG" && !path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
        if (fmt == "JPG" && !path.endsWith(".jpg", Qt::CaseInsensitive) && !path.endsWith(".jpeg", Qt::CaseInsensitive)) {
            path += ".jpg";
        }

        QImage img = m_view->grabFramebuffer();
        if (img.isNull()) return;

        const QSize targetSize(wSpin->value(), hSpin->value());
        if (img.size() != targetSize) {
            img = img.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }

        if (fmt == "PNG" && transparentBg->isChecked()) {
            // Chroma-key the known GL clear color used by PCB3DView.
            const QColor keyColor = QColor::fromRgbF(0.07, 0.09, 0.12, 1.0);
            QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
            for (int y = 0; y < rgba.height(); ++y) {
                QRgb* line = reinterpret_cast<QRgb*>(rgba.scanLine(y));
                for (int x = 0; x < rgba.width(); ++x) {
                    const QColor c(line[x]);
                    const int dr = std::abs(c.red() - keyColor.red());
                    const int dg = std::abs(c.green() - keyColor.green());
                    const int db = std::abs(c.blue() - keyColor.blue());
                    if (dr <= 8 && dg <= 8 && db <= 8) {
                        line[x] = qRgba(c.red(), c.green(), c.blue(), 0);
                    }
                }
            }
            rgba.save(path, "PNG");
            return;
        }

        img.save(path, fmt.toUtf8().constData());
    });

    QAction* exportMCAD = toolbar->addAction("Export MCAD");
    connect(exportMCAD, &QAction::triggered, this, [this]() {
        QDialog dlg(this);
        dlg.setWindowTitle("Export MCAD");
        QFormLayout form(&dlg);
        QComboBox* fmt = new QComboBox(&dlg);
        fmt->addItem("STEP Wireframe (*.step)", "STEP");
        fmt->addItem("VRML Assembly (*.wrl)", "VRML");
        form.addRow("Format:", fmt);
        QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form.addRow(bb);
        if (dlg.exec() != QDialog::Accepted) return;

        const QString kind = fmt->currentData().toString();
        QString filter = (kind == "STEP") ? "STEP Files (*.step *.stp)" : "VRML Files (*.wrl)";
        QString path = QFileDialog::getSaveFileName(this, "Export MCAD", "Board", filter);
        if (path.isEmpty()) return;

        if (kind == "STEP" && !path.endsWith(".step", Qt::CaseInsensitive) && !path.endsWith(".stp", Qt::CaseInsensitive)) {
            path += ".step";
        } else if (kind == "VRML" && !path.endsWith(".wrl", Qt::CaseInsensitive)) {
            path += ".wrl";
        }

        QString err;
        bool ok = false;
        if (kind == "STEP") ok = MCADExporter::exportSTEPWireframe(m_scene, path, &err);
        else ok = MCADExporter::exportVRMLAssembly(m_scene, path, &err);

        if (!ok) {
            QDialog errDlg(this);
            errDlg.setWindowTitle("MCAD Export Failed");
            QVBoxLayout lay(&errDlg);
            lay.addWidget(new QLabel(err.isEmpty() ? "Failed to export MCAD file." : err, &errDlg));
            QDialogButtonBox* okBtn = new QDialogButtonBox(QDialogButtonBox::Ok, &errDlg);
            connect(okBtn, &QDialogButtonBox::accepted, &errDlg, &QDialog::accept);
            lay.addWidget(okBtn);
            errDlg.exec();
            return;
        }
    });

    toolbar->addSeparator();
    QAction* collisionCheck = toolbar->addAction("Check Collisions");
    connect(collisionCheck, &QAction::triggered, this, [this]() {
        const int pairs = m_view->detectComponentCollisions();
        if (m_collisionLabel) {
            if (pairs > 0) {
                m_collisionLabel->setText(QString("Collisions: %1 pair(s)").arg(pairs));
                m_collisionLabel->setStyleSheet("color: #ff6b6b; font-weight: 700;");
            } else {
                m_collisionLabel->setText("Collisions: none");
                m_collisionLabel->setStyleSheet("color: #7fe28a; font-weight: 700;");
            }
        }
    });
    m_collisionLabel = new QLabel("Collisions: -");
    toolbar->addWidget(m_collisionLabel);
    m_measureLabel = new QLabel("Measure: off");
    toolbar->addWidget(m_measureLabel);

    connect(m_view, &PCB3DView::measurementUpdated, this, [this](double mm) {
        if (!m_measureLabel) return;
        if (mm < 0.0) m_measureLabel->setText("Measure: pick second point");
        else m_measureLabel->setText(QString("Measure: %1 mm").arg(mm, 0, 'f', 3));
    });

    refreshNetList();

    if (m_scene) {
        connect(m_scene, &QGraphicsScene::changed, this, [this](const QList<QRectF>&) {
            refreshNetList();
        });
    }

    // Appearance controls sidebar
    QDockWidget* appearanceDock = new QDockWidget("Appearance", this);
    appearanceDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    QWidget* appearance = new QWidget(appearanceDock);
    QVBoxLayout* av = new QVBoxLayout(appearance);
    av->setContentsMargins(8, 8, 8, 8);
    av->setSpacing(8);

    auto mkToggle = [&](const QString& text, bool checked, auto slot) {
        QCheckBox* cb = new QCheckBox(text, appearance);
        cb->setChecked(checked);
        QObject::connect(cb, &QCheckBox::toggled, this, slot);
        av->addWidget(cb);
    };
    mkToggle("Board", true, [this](bool on) { m_view->setShowSubstrate(on); });
    mkToggle("Copper", true, [this](bool on) { m_view->setShowCopper(on); });
    mkToggle("Silkscreen", true, [this](bool on) { m_view->setShowSilkscreen(on); });
    mkToggle("Components", true, [this](bool on) { m_view->setShowComponents(on); });
    mkToggle("Grid", false, [this](bool on) { m_view->setShowGrid(on); });

    auto mkColorRow = [&](const QString& label, const QColor& initial, auto apply) {
        QWidget* row = new QWidget(appearance);
        QHBoxLayout* rh = new QHBoxLayout(row);
        rh->setContentsMargins(0, 0, 0, 0);
        QLabel* l = new QLabel(label, row);
        QPushButton* b = new QPushButton("Color", row);
        b->setMinimumWidth(72);
        auto setBtnColor = [b](const QColor& c) {
            b->setStyleSheet(QString("QPushButton { background-color: %1; color: #10151b; font-weight: 600; }")
                                 .arg(c.name()));
        };
        setBtnColor(initial);
        QObject::connect(b, &QPushButton::clicked, this, [this, b, setBtnColor, apply]() {
            QColor c = QColorDialog::getColor(Qt::white, this, "Select Color");
            if (!c.isValid()) return;
            setBtnColor(c);
            apply(c);
        });
        rh->addWidget(l);
        rh->addStretch();
        rh->addWidget(b);
        av->addWidget(row);
    };

    mkColorRow("Soldermask", QColor(38, 132, 76), [this](const QColor& c) { m_view->setSoldermaskColor(c); });
    mkColorRow("Copper Top", QColor(212, 71, 51), [this](const QColor& c) { m_view->setCopperTopColor(c); });
    mkColorRow("Copper Bottom", QColor(46, 107, 219), [this](const QColor& c) { m_view->setCopperBottomColor(c); });
    mkColorRow("Components", QColor(82, 86, 96), [this](const QColor& c) { m_view->setComponentColor(c); });
    av->addStretch();

    appearanceDock->setWidget(appearance);
    addDockWidget(Qt::RightDockWidgetArea, appearanceDock);
}

PCB3DWindow::~PCB3DWindow() = default;

void PCB3DWindow::updateView() {
    refreshNetList();
    if (m_view) m_view->updateScene();
}

void PCB3DWindow::setSubstrateAlpha(float alpha) {
    if (m_view) m_view->setSubstrateAlpha(alpha);
}

void PCB3DWindow::setComponentAlpha(float alpha) {
    if (m_view) m_view->setComponentAlpha(alpha);
}

void PCB3DWindow::setShowCopper(bool enabled) {
    if (m_view) m_view->setShowCopper(enabled);
}

void PCB3DWindow::setShowBottomCopper(bool enabled) {
    if (m_view) m_view->setShowBottomCopper(enabled);
}

void PCB3DWindow::closeEvent(QCloseEvent* event) {
    QMainWindow::closeEvent(event);
}

void PCB3DWindow::refreshNetList() {
    if (!m_scene || !m_netCombo) return;

    const QString current = m_netCombo->currentText();
    QSet<QString> nets;
    for (QGraphicsItem* item : m_scene->items()) {
        PCBItem* pcb = dynamic_cast<PCBItem*>(item);
        if (!pcb) continue;
        const QString net = pcb->netName();
        if (!net.isEmpty() && net != "No Net") nets.insert(net);
    }

    QStringList list = nets.values();
    std::sort(list.begin(), list.end(), [](const QString& a, const QString& b) {
        return a.localeAwareCompare(b) < 0;
    });

    m_netCombo->blockSignals(true);
    m_netCombo->clear();
    m_netCombo->addItem("All Nets");
    m_netCombo->addItems(list);

    int idx = m_netCombo->findText(current);
    if (idx < 0) idx = 0;
    m_netCombo->setCurrentIndex(idx);
    m_netCombo->blockSignals(false);
}
