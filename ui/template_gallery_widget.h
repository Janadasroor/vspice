#ifndef TEMPLATE_GALLERY_WIDGET_H
#define TEMPLATE_GALLERY_WIDGET_H

#include <QWidget>
#include <QMap>
#include <QList>
#include <QString>
#include <QJsonObject>

class QLineEdit;
class QTabWidget;
class QGridLayout;
class QScrollArea;
class QPushButton;
class QLabel;
class QVBoxLayout;

class TemplateGalleryWidget : public QWidget {
    Q_OBJECT

public:
    struct Template {
        QString id;
        QString name;
        QString category;
        QString description;
        QStringList tags;
        QString schematicPath;
        QString netlistPath;
        QString thumbnailPath;
        QVector<QJsonObject> parameters;
        bool hasSimulation;
        QString simulationType;
    };

    explicit TemplateGalleryWidget(QWidget* parent = nullptr);
    ~TemplateGalleryWidget();

    void loadTemplates();
    void setProjectDirectory(const QString& dir);

signals:
    void templateSelected(const QString& schematicPath, const QString& netlistPath);
    void openSchematicRequested(const QString& filePath);

private slots:
    void onCategoryChanged(int index);
    void onSearchChanged(const QString& text);
    void onTryItClicked(const QString& templateId);

private:
    void setupUi();
    void loadTemplateMetadata();
    void populateGallery();
    void filterByCategory(const QString& category);
    void filterBySearch(const QString& query);
    QString templatesDirectory() const;
    void renderThumbnailIfNeeded(const Template& tpl);

    QString m_projectDir;
    QJsonObject m_metadata;
    QMap<QString, Template> m_templatesById;
    QList<Template> m_filteredTemplates;

    QLineEdit* m_searchEdit;
    QTabWidget* m_categoryTabs;
    QScrollArea* m_scrollArea;
    QWidget* m_cardContainer;
    QGridLayout* m_cardGrid;
    QMap<QString, QPushButton*> m_tryItButtons;
};

#endif // TEMPLATE_GALLERY_WIDGET_H