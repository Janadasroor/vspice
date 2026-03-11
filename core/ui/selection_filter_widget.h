#ifndef SELECTION_FILTER_WIDGET_H
#define SELECTION_FILTER_WIDGET_H

#include <QWidget>
#include <QCheckBox>
#include <QMap>
#include <QVBoxLayout>

class SelectionFilterWidget : public QWidget {
    Q_OBJECT
public:
    explicit SelectionFilterWidget(QWidget *parent = nullptr);

    bool isFilterEnabled(const QString& type) const;
    void setFilterEnabled(const QString& type, bool enabled);

signals:
    void filterChanged();

private:
    void setupUi();
    QMap<QString, QCheckBox*> m_filters;
};

#endif // SELECTION_FILTER_WIDGET_H
