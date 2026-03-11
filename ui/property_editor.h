#ifndef PROPERTY_EDITOR_H
#define PROPERTY_EDITOR_H

#include <QWidget>
#include <QTableWidget>
#include <QVariant>
#include <QMap>

class SchematicItem;

class PropertyEditor : public QWidget {
    Q_OBJECT

public:
    explicit PropertyEditor(QWidget *parent = nullptr);
    ~PropertyEditor();

    // Setup for SchematicItem
    void setSchematicItems(const QList<SchematicItem*>& items);
    
    // Compatibility helpers
    void setSchematicItem(SchematicItem* item) { setSchematicItems(item ? QList<SchematicItem*>{item} : QList<SchematicItem*>()); }

    // Clear the editor
    void clear();

    // specific helper 
    void addProperty(const QString& name, const QVariant& value, const QString& type = "string");
    void addSectionHeader(const QString& title);

signals:
    // Emitted when a property changes. 
    // For SchematicItems, the editor might apply changes directly or emit this 
    // for the parent editor to create an UndoCommand.
    void propertyChanged(const QString& name, const QVariant& value);

private slots:
    void onCellChanged(int row, int column);
    void onBoolToggled(bool checked); 

private:
    void setupUi();
    
    // Helpers to create specific editors in the table
    void addStringProperty(int row, const QString& value);
    void addBoolProperty(int row, bool value);
    void addDoubleProperty(int row, double value);
    void addColorProperty(int row, const QString& colorName);
    void addEnumProperty(int row, const QStringList& options, const QString& current);
    void addFileProperty(int row, const QString& value, const QString& filter);

    QTableWidget* m_table;
    QList<SchematicItem*> m_schematicItems;
    bool m_blockSignals = false;
};

#endif // PROPERTY_EDITOR_H
