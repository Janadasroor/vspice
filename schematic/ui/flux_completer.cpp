#include "flux_completer.h"
#include <QStandardItemModel>
#include <QIcon>
#include "../../schematic/factories/schematic_item_factory.h"
#include "../../symbols/symbol_library.h"

namespace Flux {

FluxCompleter::FluxCompleter(QObject* parent) : QCompleter(parent) {
    m_model = new QStandardItemModel(this);
    setModel(m_model);
    setCompletionMode(QCompleter::PopupCompletion);
    setCaseSensitivity(Qt::CaseInsensitive);
    setFilterMode(Qt::MatchContains);
}

void FluxCompleter::updateCompletions() {
    m_model->clear();

    // 1. Keywords
    QStringList keywords = {"import", "net", "for", "in", "range", "component"};
    for (const QString& kw : keywords) {
        addCompletionItem(kw, "Keyword", ":/icons/comp_logic.svg");
    }

    // 2. Standard Components from Factory
    auto& factory = SchematicItemFactory::instance();
    for (const QString& type : factory.registeredTypes()) {
        addCompletionItem(type, "Component (Built-in)", createIconPath(type));
    }

    // 3. Library Symbols
    for (SymbolLibrary* lib : SymbolLibraryManager::instance().libraries()) {
        for (const QString& sym : lib->symbolNames()) {
            addCompletionItem(sym, QString("Library: %1").arg(lib->name()), createIconPath(sym));
        }
    }
}

void FluxCompleter::addCompletionItem(const QString& text, const QString& type, const QString& iconPath) {
    QList<QStandardItem*> items;
    QStandardItem* item = new QStandardItem(QIcon(iconPath), text);
    item->setData(type, Qt::UserRole);
    m_model->appendRow(item);
}

QString FluxCompleter::createIconPath(const QString& name) {
    QString n = name.toLower();
    if (n.contains("resistor")) return ":/icons/comp_resistor.svg";
    if (n.contains("capacitor")) return ":/icons/comp_capacitor.svg";
    if (n.contains("inductor")) return ":/icons/comp_inductor.svg";
    if (n.contains("diode")) return ":/icons/comp_diode.svg";
    if (n.contains("transistor")) return ":/icons/comp_transistor.svg";
    if (n.contains("ic")) return ":/icons/comp_ic.svg";
    if (n.contains("gnd")) return ":/icons/comp_gnd.svg";
    if (n.contains("vcc") || n.contains("vdd")) return ":/icons/comp_vcc.svg";
    return ":/icons/component_file.svg";
}

} // namespace Flux
