#ifndef FLUX_COMPLETER_H
#define FLUX_COMPLETER_H

#include <QCompleter>

class QStandardItemModel;

namespace Flux {

class FluxCompleter : public QCompleter {
    Q_OBJECT
public:
    explicit FluxCompleter(QObject* parent = nullptr);
    void updateCompletions();

private:
    void addCompletionItem(const QString& text, const QString& type, const QString& iconPath);
    QString createIconPath(const QString& name);

    QStandardItemModel* m_model;
};

} // namespace Flux

#endif // FLUX_COMPLETER_H
