#ifndef SCHEMATIC_PAGE_ITEM_H
#define SCHEMATIC_PAGE_ITEM_H

#include <QGraphicsItem>
#include <QPen>
#include <QFont>
#include <QMap>
#include <QPixmap>
#include <QJsonObject>

// ─── TitleBlockData ────────────────────────────────────────────────────────
// All metadata fields for the schematic title block.
// Saved/loaded as a JSON block inside the .flxsch file.
struct TitleBlockData {
    QString projectName;   // "My Awesome PCB"
    QString sheetName;     // "Power Supply"
    QString sheetNumber;   // "3"
    QString sheetTotal;    // "5"  → shows as "Sheet 3 of 5"
    QString company;       // "viospice Inc."
    QString revision;      // "A"
    QString designer;      // "J. Smith"
    QString date;          // auto-filled if empty
    QString description;   // short doc note
    QString logoPath;      // absolute path to logo image

    TitleBlockData()
        : sheetNumber("1"), sheetTotal("1"), revision("A") {}

    QJsonObject toJson() const;
    static TitleBlockData fromJson(const QJsonObject&);
};

// ─── SchematicPageItem ─────────────────────────────────────────────────────
class SchematicPageItem : public QGraphicsItem {
public:
    explicit SchematicPageItem(const QSize& pageSize,
                               const QString& title = "Untitled",
                               QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void   paint(QPainter* painter,
                 const QStyleOptionGraphicsItem* option,
                 QWidget* widget) override;

    // Page geometry
    void setPageSize(const QSize& size);
    void setPageSizeName(const QString& name) { m_currentPageSize = name; update(); }

    // Legacy setters (kept for backward compat)
    void setTitle(const QString& title)   { m_tb.projectName = title; update(); }
    void setDate(const QString& date)     { m_tb.date        = date;  update(); }
    void setRevision(const QString& rev)  { m_tb.revision    = rev;   update(); }
    void setCompany(const QString& co)    { m_tb.company     = co;    update(); }

    // Title block data (full)
    void            setTitleBlock(const TitleBlockData& data);
    TitleBlockData  titleBlock() const { return m_tb; }

    // Auto-set the sheet number
    void setSheetInfo(int number, int total);

    QSize pageSize() const { return m_pageSize; }

    enum { Type = UserType + 100 };
    int type() const override { return Type; }

private:
    void drawBorder(QPainter* painter);
    void drawGridReferences(QPainter* painter, QColor contentColor);
    void drawTitleBlock(QPainter* painter, bool isLight);
    void drawLogoCell(QPainter* painter, const QRectF& cell, bool isLight);

    QSize          m_pageSize;
    TitleBlockData m_tb;
    QString        m_currentPageSize;
    QColor         m_frameColor;
    qreal          m_margin;

    mutable QPixmap m_logoCached;
    mutable QString m_logoCachedPath;
};

#endif // SCHEMATIC_PAGE_ITEM_H
