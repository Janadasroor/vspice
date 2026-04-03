#ifndef PCB_PROPERTY_EDITOR_H
#define PCB_PROPERTY_EDITOR_H

#include <QWidget>
#include <QList>
#include <QMap>
#include <QVariant>

class PCBItem;
class QVBoxLayout;
class QScrollArea;
class QLineEdit;
class QPushButton;

namespace Flux {

class PropertySection;

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setItem(PCBItem* item);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    PCBItem* m_item = nullptr;
};

/**
 * @brief A modern, smart property editor for PCB items.
 * Features collapsible sections, live previews, and advanced searching.
 */
class PCBPropertyEditor : public QWidget {
    Q_OBJECT
public:
    explicit PCBPropertyEditor(QWidget* parent = nullptr);

    void setPCBItems(const QList<PCBItem*>& items);
    void setPCBTool(class PCBTool* tool) { Q_UNUSED(tool); }
    void clear();

signals:
    void propertyChanged(const QString& name, const QVariant& value);

private slots:
    void onSearchChanged(const QString& text);

private:
    void setupUi();
    void populateProperties();
    
    // UI Components
    QLineEdit* m_searchEdit;
    PreviewWidget* m_previewWidget;
    QScrollArea* m_scrollArea;
    QWidget* m_contentWidget;
    QVBoxLayout* m_contentLayout;
    
    QList<PCBItem*> m_items;
    QList<PropertySection*> m_sections;
    
    bool m_blockSignals = false;
};

/**
 * @brief Helper widget for a single property row.
 */
class PropertyRow : public QWidget {
    Q_OBJECT
public:
    PropertyRow(const QString& label, QWidget* editor, QWidget* parent = nullptr);
    QString label() const { return m_label; }
    void setVisible(bool visible) override;

private:
    QString m_label;
};

/**
 * @brief A collapsible section in the property editor.
 */
class PropertySection : public QWidget {
    Q_OBJECT
public:
    explicit PropertySection(const QString& title, QWidget* parent = nullptr);
    void addRow(PropertyRow* row);
    void clear();
    void setTitle(const QString& title);
    void setExpanded(bool expanded);
    void filterRows(const QString& filter);

private:
    QString m_title;
    QVBoxLayout* m_rowsLayout;
    QWidget* m_container;
    QPushButton* m_headerBtn;
};

} // namespace Flux

#endif // PCB_PROPERTY_EDITOR_H
