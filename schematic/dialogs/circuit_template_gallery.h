#ifndef CIRCUIT_TEMPLATE_GALLERY_H
#define CIRCUIT_TEMPLATE_GALLERY_H

#include <QDialog>
#include <QJsonObject>
#include <QMap>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>

class QLineEdit;
class QPushButton;
class QListWidgetItem;
class QTableWidget;

class CircuitTemplateGallery : public QDialog {
    Q_OBJECT

public:
    struct Parameter {
        QString name;
        QString defaultValue;
        QString displayName;
        QString description;
        QString unit;
    };

    struct Template {
        QString id;
        QString name;
        QString category;
        QString subcategory;
        QString description;
        QStringList tags;
        QString filePath;
        QString thumbnailPath;
        QStringList libraryDependencies;
        QVector<Parameter> parameters;
        bool hasSimulation;
        QString simulationType;
    };

    explicit CircuitTemplateGallery(const QString& projectDir, QWidget* parent = nullptr);
    ~CircuitTemplateGallery();

    Template selectedTemplate() const { return m_selectedTemplate; }
    QMap<QString, QString> customParameters() const { return m_customParameters; }

private slots:
    void onCategorySelected(QListWidgetItem* item);
    void onTemplateSelected(QListWidgetItem* item);
    void onSearchTextChanged(const QString& text);
    void onInsertClicked();
    void onSaveAsTemplateClicked();
    void onRefreshTemplates();

private:
    void setupUi();
    void loadTemplates();
    void loadTemplateMetadata();
    void populateCategories();
    void populateTemplatesForCategory(const QString& category);
    void populateSearchResults(const QString& query);
    QString templatesDirectory() const;
    QString userTemplatesDirectory() const;
    void saveUserTemplate(const QString& sourceSchematic);
    void renderThumbnail(const QString& schematicPath, const QString& thumbPath);
    Template parseTemplateFromFile(const QString& filePath) const;

    QString m_projectDir;
    Template m_selectedTemplate;
    QMap<QString, QString> m_customParameters;
    QJsonObject m_metadata;

    QListWidget* m_categoryList;
    QListWidget* m_templateList;
    QStackedWidget* m_templateStack;
    QLabel* m_thumbnailLabel;
    QLabel* m_nameLabel;
    QLabel* m_descriptionLabel;
    QTableWidget* m_paramsTable;
    QLineEdit* m_searchEdit;
    QPushButton* m_insertButton;
    QPushButton* m_saveAsButton;

    QMap<QString, QList<Template>> m_templatesByCategory;
    QList<Template> m_allTemplates;
};

#endif // CIRCUIT_TEMPLATE_GALLERY_H