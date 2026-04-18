#include "passive_model_picker_dialog.h"

#include "theme_manager.h"
#include "../../simulator/bridge/model_library_manager.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

QString decodeSpiceText(const QByteArray& raw) {
    if (raw.isEmpty()) return QString();
    if (raw.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(raw[0]);
        const unsigned char b1 = static_cast<unsigned char>(raw[1]);
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
            return QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData() + 2), (raw.size() - 2) / 2);
        }
    }
    return QString::fromUtf8(raw);
}

QStringList loadLogicalLines(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QString content = decodeSpiceText(f.readAll());
    f.close();

    QStringList logical;
    for (const QString& raw : content.split('\n')) {
        const QString t = raw.trimmed();
        if (t.isEmpty()) continue;
        if (!logical.isEmpty() && t.startsWith('+')) {
            logical.last().append(' ' + t.mid(1).trimmed());
        } else {
            logical.append(raw);
        }
    }
    return logical;
}

QString kindName(PassiveModelPickerDialog::Kind kind) {
    if (kind == PassiveModelPickerDialog::Kind::Resistor) return "Resistor";
    if (kind == PassiveModelPickerDialog::Kind::Capacitor) return "Capacitor";
    return "Inductor";
}

QStringList acceptedTypeTokens(PassiveModelPickerDialog::Kind kind) {
    if (kind == PassiveModelPickerDialog::Kind::Resistor) {
        return {"r", "res", "resistor"};
    }
    if (kind == PassiveModelPickerDialog::Kind::Capacitor) {
        return {"c", "cap", "capacitor"};
    }
    return {"l", "ind", "inductor"};
}

QStringList preferredFiles(PassiveModelPickerDialog::Kind kind) {
    const QString home = QDir::homePath();
    if (kind == PassiveModelPickerDialog::Kind::Resistor) {
        return {
            home + "/ViospiceLib/lib/resistors_standard.lib",
            home + "/Documents/ltspice/cmp/standard.res"
        };
    }
    if (kind == PassiveModelPickerDialog::Kind::Capacitor) {
        return {
            home + "/ViospiceLib/lib/capacitors_standard.lib",
            home + "/Documents/ltspice/cmp/standard.cap"
        };
    }
    return {
        home + "/ViospiceLib/lib/inductors_standard.lib",
        home + "/Documents/ltspice/cmp/standard.ind"
    };
}

QString catalogPathForKind(PassiveModelPickerDialog::Kind kind) {
    const QString home = QDir::homePath();
    if (kind == PassiveModelPickerDialog::Kind::Resistor) {
        return home + "/ViospiceLib/lib/passive_catalog_resistor.json";
    }
    if (kind == PassiveModelPickerDialog::Kind::Capacitor) {
        return home + "/ViospiceLib/lib/passive_catalog_capacitor.json";
    }
    return home + "/ViospiceLib/lib/passive_catalog_inductor.json";
}

} // namespace

PassiveModelPickerDialog::PassiveModelPickerDialog(Kind kind, QWidget* parent)
    : QDialog(parent), m_kind(kind) {
    setWindowTitle(QString("Pick %1 Model").arg(kindName(kind)));
    setModal(true);
    setMinimumSize(520, 400);

    auto* layout = new QVBoxLayout(this);
    auto* searchLayout = new QHBoxLayout();
    searchLayout->addWidget(new QLabel("Search:"));
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("Filter models by name...");
    searchLayout->addWidget(m_searchEdit);
    layout->addLayout(searchLayout);

    m_modelList = new QListWidget();
    layout->addWidget(m_modelList);

    m_detailLabel = new QLabel("Select a model");
    {
        QFont detailFont = m_detailLabel->font();
        detailFont.setPointSizeF(qMax(8.0, detailFont.pointSizeF() - 1.0));
        m_detailLabel->setFont(detailFont);
        QPalette pal = m_detailLabel->palette();
        pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
        m_detailLabel->setPalette(pal);
    }
    m_detailLabel->setWordWrap(true);
    layout->addWidget(m_detailLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &PassiveModelPickerDialog::applySelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_searchEdit, &QLineEdit::textChanged, this, &PassiveModelPickerDialog::filterModels);
    connect(m_modelList, &QListWidget::itemDoubleClicked, this, &PassiveModelPickerDialog::onModelSelected);
    connect(m_modelList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current) {
        if (current) {
            m_detailLabel->setText(current->data(Qt::UserRole + 1).toString());
        }
    });

    if (ThemeManager::theme()) {
        setStyleSheet(ThemeManager::theme()->widgetStylesheet());
    }

    loadModels();
}

void PassiveModelPickerDialog::loadModels() {
    m_modelList->clear();

    const QStringList accepted = acceptedTypeTokens(m_kind);
    const QRegularExpression headRe(R"(^\s*\.model\s+(\S+)\s+([^\s(]+))", QRegularExpression::CaseInsensitiveOption);

    QStringList seen;
    for (const QString& filePath : preferredFiles(m_kind)) {
        if (!QFileInfo::exists(filePath)) continue;
        const QStringList lines = loadLogicalLines(filePath);
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            const auto m = headRe.match(trimmed);
            if (!m.hasMatch()) continue;
            const QString modelName = m.captured(1).trimmed();
            const QString typeTok = m.captured(2).trimmed().toLower();
            if (!accepted.contains(typeTok)) continue;
            if (modelName.isEmpty()) continue;
            const QString modelLower = modelName.toLower();
            if (seen.contains(modelLower)) continue;
            seen.append(modelLower);

            auto* item = new QListWidgetItem(modelName);
            item->setData(Qt::UserRole, modelName);
            item->setData(Qt::UserRole + 1, QString("%1\nLibrary: %2\n%3")
                                           .arg(modelName,
                                                QFileInfo(filePath).fileName(),
                                                trimmed));
            m_modelList->addItem(item);
        }
    }

    if (m_modelList->count() == 0) {
        const QString catalogPath = catalogPathForKind(m_kind);
        QFile cf(catalogPath);
        if (cf.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(cf.readAll());
            cf.close();
            const QJsonArray entries = doc.object().value("entries").toArray();
            for (const QJsonValue& v : entries) {
                const QJsonObject o = v.toObject();
                const QString value = o.value("value").toString().trimmed();
                const QString mpn = o.value("mpn").toString().trimmed();
                const QString mfg = o.value("manufacturer").toString().trimmed();
                const QString desc = o.value("description").toString().trimmed();
                QString label = value;
                if (!mpn.isEmpty()) {
                    if (label.isEmpty()) label = mpn;
                    else label += "  [" + mpn + "]";
                }
                if (label.isEmpty()) continue;
                auto* item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, mpn);
                item->setData(Qt::UserRole + 2, value);
                item->setData(Qt::UserRole + 3, mfg);
                item->setData(Qt::UserRole + 4, mpn);
                item->setData(Qt::UserRole + 1,
                              QString("%1\nMFG: %2\nMPN: %3\n%4")
                                  .arg(label, mfg, mpn, desc));
                m_modelList->addItem(item);
            }
        }
    }

    if (m_modelList->count() == 0) {
        const QVector<SpiceModelInfo> allModels = ModelLibraryManager::instance().allModels();
        QString hint;
        if (m_kind == Kind::Resistor) hint = "res";
        else if (m_kind == Kind::Capacitor) hint = "cap";
        else hint = "ind";
        for (const auto& mi : allModels) {
            const QString lib = QFileInfo(mi.libraryPath).fileName().toLower();
            if (!lib.contains(hint)) continue;
            const QString modelLower = mi.name.toLower();
            if (seen.contains(modelLower)) continue;
            seen.append(modelLower);

            auto* item = new QListWidgetItem(mi.name);
            item->setData(Qt::UserRole, mi.name);
            item->setData(Qt::UserRole + 1,
                          QString("%1\nLibrary: %2\nType: %3\nParams: %4")
                              .arg(mi.name,
                                   QFileInfo(mi.libraryPath).fileName(),
                                   mi.type,
                                   mi.params.join(", ")));
            m_modelList->addItem(item);
        }
    }

    if (m_modelList->count() > 0) {
        m_modelList->setCurrentRow(0);
    } else {
        const QString ext = (m_kind == Kind::Resistor) ? "res" : (m_kind == Kind::Capacitor ? "cap" : "ind");
        m_detailLabel->setText(QString("No %1 models found. Import standard.%2 first.")
                                   .arg(kindName(m_kind).toLower(),
                                        ext));
    }
}

void PassiveModelPickerDialog::filterModels(const QString& text) {
    for (int i = 0; i < m_modelList->count(); ++i) {
        QListWidgetItem* item = m_modelList->item(i);
        const bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void PassiveModelPickerDialog::onModelSelected(QListWidgetItem* item) {
    if (!item) return;
    m_selectedModel = item->data(Qt::UserRole).toString();
    m_selectedValue = item->data(Qt::UserRole + 2).toString();
    m_selectedManufacturer = item->data(Qt::UserRole + 3).toString();
    m_selectedMpn = item->data(Qt::UserRole + 4).toString();
    accept();
}

void PassiveModelPickerDialog::applySelected() {
    QListWidgetItem* item = m_modelList->currentItem();
    if (item) {
        m_selectedModel = item->data(Qt::UserRole).toString();
        m_selectedValue = item->data(Qt::UserRole + 2).toString();
        m_selectedManufacturer = item->data(Qt::UserRole + 3).toString();
        m_selectedMpn = item->data(Qt::UserRole + 4).toString();
    }
    accept();
}

QString PassiveModelPickerDialog::selectedModel() const {
    return m_selectedModel;
}

QString PassiveModelPickerDialog::selectedValue() const {
    return m_selectedValue;
}

QString PassiveModelPickerDialog::selectedManufacturer() const {
    return m_selectedManufacturer;
}

QString PassiveModelPickerDialog::selectedMpn() const {
    return m_selectedMpn;
}
