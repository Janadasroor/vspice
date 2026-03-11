// schematic_page_item.cpp
// Professional IEEE/IEC-style schematic page frame with interactive title block.
// Supports: logo image, company, designer, project name, sheet name, revision,
//           automatic page numbering, date, description.

#include "schematic_page_item.h"
#include "theme_manager.h"
#include <QPainter>
#include <QPainterPath>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QFontMetricsF>

// ─── TitleBlockData serialization ─────────────────────────────────────────

QJsonObject TitleBlockData::toJson() const {
    QJsonObject o;
    o["projectName"]  = projectName;
    o["sheetName"]    = sheetName;
    o["sheetNumber"]  = sheetNumber;
    o["sheetTotal"]   = sheetTotal;
    o["company"]      = company;
    o["revision"]     = revision;
    o["designer"]     = designer;
    o["date"]         = date;
    o["description"]  = description;
    o["logoPath"]     = logoPath;
    return o;
}

TitleBlockData TitleBlockData::fromJson(const QJsonObject& o) {
    TitleBlockData d;
    d.projectName  = o["projectName"].toString();
    d.sheetName    = o["sheetName"].toString();
    d.sheetNumber  = o["sheetNumber"].toString("1");
    d.sheetTotal   = o["sheetTotal"].toString("1");
    d.company      = o["company"].toString();
    d.revision     = o["revision"].toString("A");
    d.designer     = o["designer"].toString();
    d.date         = o["date"].toString();
    d.description  = o["description"].toString();
    d.logoPath     = o["logoPath"].toString();
    return d;
}

// ─── SchematicPageItem ────────────────────────────────────────────────────

SchematicPageItem::SchematicPageItem(const QSize& pageSize,
                                     const QString& title,
                                     QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , m_pageSize(pageSize)
    , m_frameColor(120, 120, 120)
    , m_margin(100.0)
{
    m_tb.projectName = title;
    m_tb.date        = QDate::currentDate().toString("yyyy-MM-dd");
    setZValue(-1000);
}

QRectF SchematicPageItem::boundingRect() const {
    return QRectF(-m_pageSize.width()  / 2.0 - 4,
                  -m_pageSize.height() / 2.0 - 4,
                   m_pageSize.width()  + 8,
                   m_pageSize.height() + 8);
}

QPainterPath SchematicPageItem::shape() const {
    QPainterPath path;
    
    QRectF pageRect(-m_pageSize.width() / 2.0, -m_pageSize.height() / 2.0, 
                     m_pageSize.width(), m_pageSize.height());
    QRectF frame = pageRect.adjusted(m_margin, m_margin, -m_margin, -m_margin);

    // The shape consists of the border margin area...
    QPainterPath outer;
    outer.addRect(pageRect);
    QPainterPath inner;
    inner.addRect(frame);
    path = outer.subtracted(inner);

    // ...and the title block area at the bottom
    const qreal blockH = 310.0;
    QRectF block(frame.left(), frame.bottom() - blockH, frame.width(), blockH);
    path.addRect(block);

    return path;
}

void SchematicPageItem::setPageSize(const QSize& size) {
    prepareGeometryChange();
    m_pageSize = size;
    update();
}

void SchematicPageItem::setTitleBlock(const TitleBlockData& data) {
    m_tb = data;
    if (m_tb.date.isEmpty())
        m_tb.date = QDate::currentDate().toString("yyyy-MM-dd");
    update();
}

void SchematicPageItem::setSheetInfo(int number, int total) {
    m_tb.sheetNumber = QString::number(number);
    m_tb.sheetTotal  = QString::number(total);
    update();
}

// ─── paint ─────────────────────────────────────────────────────────────────

void SchematicPageItem::paint(QPainter* painter,
                               const QStyleOptionGraphicsItem* option,
                               QWidget* widget) {
    Q_UNUSED(option); Q_UNUSED(widget);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);

    QRectF pageRect(-m_pageSize.width()  / 2.0,
                    -m_pageSize.height() / 2.0,
                     m_pageSize.width(),
                     m_pageSize.height());

    // Paper shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 60));
    painter->drawRect(pageRect.translated(10, 10));

    // Paper background: keep translucent so scene grid remains visible through the page.
    QColor bgColor = ThemeManager::theme()->canvasBackground();
    QColor paperColor = bgColor;
    paperColor.setAlpha(bgColor.lightness() > 128 ? 96 : 48);
    painter->setBrush(paperColor);
    painter->setPen(Qt::NoPen);
    painter->drawRect(pageRect);

    bool  isLight    = (bgColor.lightness() > 128);
    QColor contentColor = isLight ? Qt::black : QColor(200, 200, 200);
    m_frameColor        = isLight ? QColor(20, 20, 20) : QColor(140, 140, 160);

    drawBorder(painter);
    drawGridReferences(painter, contentColor);
    drawTitleBlock(painter, isLight);

    painter->restore();
}

// ─── drawBorder ────────────────────────────────────────────────────────────

void SchematicPageItem::drawBorder(QPainter* painter) {
    QRectF pageRect(-m_pageSize.width()  / 2.0, -m_pageSize.height() / 2.0,
                     m_pageSize.width(),          m_pageSize.height());
    QRectF innerRect = pageRect.adjusted(m_margin, m_margin, -m_margin, -m_margin);

    // Outer dashed edge
    QPen outerPen(QColor(80, 80, 80));
    outerPen.setWidthF(1.5);
    outerPen.setStyle(Qt::DashLine);
    painter->setPen(outerPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(pageRect);

    // Inner solid frame
    QPen framePen(m_frameColor);
    framePen.setWidthF(4.0);
    framePen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(framePen);
    painter->drawRect(innerRect);
}

// ─── drawGridReferences ────────────────────────────────────────────────────

void SchematicPageItem::drawGridReferences(QPainter* painter, QColor contentColor) {
    QRectF pageRect(-m_pageSize.width()  / 2.0, -m_pageSize.height() / 2.0,
                     m_pageSize.width(),          m_pageSize.height());
    QRectF frame = pageRect.adjusted(m_margin, m_margin, -m_margin, -m_margin);

    QPen pen(contentColor);
    pen.setWidthF(2.0);
    painter->setPen(pen);

    QFont font("Inter", 20);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setWeight(QFont::Bold);
    painter->setFont(font);

    const int hDivs = qMax(4, (int)(m_pageSize.width()  / 400));
    const int vDivs = qMax(3, (int)(m_pageSize.height() / 400));
    qreal hStep   = frame.width()  / hDivs;
    qreal vStep   = frame.height() / vDivs;
    qreal tickLen = 15;

    for (int i = 0; i < hDivs; ++i) {
        qreal x1 = frame.left() + i * hStep;
        painter->drawLine(QPointF(x1, frame.top()),    QPointF(x1, frame.top()    + tickLen));
        painter->drawLine(QPointF(x1, frame.bottom()), QPointF(x1, frame.bottom() - tickLen));
        painter->drawText(QRectF(x1, pageRect.top() + 5,    hStep, m_margin - 10), Qt::AlignCenter, QString::number(i + 1));
        painter->drawText(QRectF(x1, frame.bottom()  + 5,   hStep, m_margin - 10), Qt::AlignCenter, QString::number(i + 1));
    }
    painter->drawLine(QPointF(frame.right(), frame.top()),    QPointF(frame.right(), frame.top()    + tickLen));
    painter->drawLine(QPointF(frame.right(), frame.bottom()), QPointF(frame.right(), frame.bottom() - tickLen));

    for (int i = 0; i < vDivs; ++i) {
        qreal y1    = frame.top() + i * vStep;
        char  label = 'A' + i;
        painter->drawLine(QPointF(frame.left(),  y1), QPointF(frame.left()  + tickLen, y1));
        painter->drawLine(QPointF(frame.right(), y1), QPointF(frame.right() - tickLen, y1));
        painter->drawText(QRectF(pageRect.left() + 5, y1, m_margin - 10, vStep), Qt::AlignCenter, QString(label));
        painter->drawText(QRectF(frame.right()   + 5, y1, m_margin - 10, vStep), Qt::AlignCenter, QString(label));
    }
    painter->drawLine(QPointF(frame.left(),  frame.bottom()), QPointF(frame.left()  + tickLen, frame.bottom()));
    painter->drawLine(QPointF(frame.right(), frame.bottom()), QPointF(frame.right() - tickLen, frame.bottom()));
}

// ─── drawLogoCell ──────────────────────────────────────────────────────────

void SchematicPageItem::drawLogoCell(QPainter* painter, const QRectF& cell, bool isLight) {
    if (m_tb.logoPath.isEmpty() || !QFile::exists(m_tb.logoPath)) {
        // Draw placeholder "FLUX" wordmark
        painter->save();
        QFont f("Inter", 28, QFont::Bold);
        painter->setFont(f);
        painter->setPen(isLight ? QColor(40, 80, 200) : QColor(80, 140, 255));
        painter->drawText(cell, Qt::AlignCenter, "⚡ FLUX");
        painter->restore();
        return;
    }

    // Cache the pixmap
    if (m_logoCachedPath != m_tb.logoPath) {
        m_logoCached     = QPixmap(m_tb.logoPath);
        m_logoCachedPath = m_tb.logoPath;
    }

    if (!m_logoCached.isNull()) {
        QRectF logoRect = m_logoCached.rect();
        // Scale to fit cell with 10px margin, keep aspect
        QRectF target = cell.adjusted(10, 8, -10, -8);
        double scale = qMin(target.width()  / logoRect.width(),
                            target.height() / logoRect.height());
        double w = logoRect.width()  * scale;
        double h = logoRect.height() * scale;
        QRectF dst(target.center().x() - w / 2.0,
                   target.center().y() - h / 2.0, w, h);
        painter->drawPixmap(dst, m_logoCached, m_logoCached.rect());
    }
}

// ─── drawTitleBlock ────────────────────────────────────────────────────────

void SchematicPageItem::drawTitleBlock(QPainter* painter, bool isLight) {
    QRectF pageRect(-m_pageSize.width()  / 2.0, -m_pageSize.height() / 2.0,
                     m_pageSize.width(),          m_pageSize.height());
    QRectF frame = pageRect.adjusted(m_margin, m_margin, -m_margin, -m_margin);

    // ── Block geometry ─────────────────────────────────────────────────────
    // Full-width block at bottom, 300 units tall
    const qreal blockH = 310.0;
    QRectF block(frame.left(), frame.bottom() - blockH, frame.width(), blockH);

    // Colors
    QColor bg       = isLight ? QColor(248, 249, 252) : QColor(18, 22, 30);
    QColor altBg    = isLight ? QColor(235, 240, 255) : QColor(24, 30, 42);
    QColor labelClr = isLight ? QColor(90, 90, 110)   : QColor(110, 115, 140);
    QColor valClr   = isLight ? QColor(15, 15, 30)    : QColor(210, 215, 235);
    QColor titleClr = isLight ? QColor(10, 20, 100)   : QColor(130, 175, 255);

    // ── Background ─────────────────────────────────────────────────────────
    painter->setPen(Qt::NoPen);
    painter->setBrush(bg);
    painter->drawRect(block);

    // ── Outer border ───────────────────────────────────────────────────────
    QPen borderPen(m_frameColor, 3.0);
    borderPen.setJoinStyle(Qt::MiterJoin);
    painter->setPen(borderPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(block);

    QPen thinPen(m_frameColor, 1.5);

    // ── Column layout ──────────────────────────────────────────────────────
    // |  LOGO  |      PROJECT / SHEET INFO       | Rev | Sheet |
    // |  LOGO  | Description                     | ... | Date  |
    // |  LOGO  | Company             | Designer  | ... | Size  |
    //
    // We split the width into 4 zones:
    //   zone[0] = logo   220u
    //   zone[1] = main   auto
    //   zone[2] = right  220u  (rev / sheet / date / size)

    const qreal logoW  = 220.0;
    const qreal rightW = 400.0;
    const qreal mainW  = block.width() - logoW - rightW;

    QRectF logoZone (block.left(),              block.top(), logoW,  blockH);
    QRectF mainZone (block.left() + logoW,      block.top(), mainW,  blockH);
    QRectF rightZone(block.left() + logoW + mainW, block.top(), rightW, blockH);

    // Logo zone background (slightly tinted)
    painter->setPen(Qt::NoPen);
    painter->setBrush(altBg);
    painter->drawRect(logoZone);
    painter->setBrush(Qt::NoBrush);

    // Vertical dividers
    painter->setPen(thinPen);
    painter->drawLine(logoZone.topRight(),  logoZone.bottomRight());
    painter->drawLine(rightZone.topLeft(), rightZone.bottomLeft());

    // ── Draw logo ──────────────────────────────────────────────────────────
    drawLogoCell(painter, logoZone, isLight);

    // ── Row heights inside mainZone & rightZone ────────────────────────────
    // Row 0: Project Name (tall)  — 110u
    // Row 1: Sheet Name           — 80u
    // Row 2: Description          — 70u
    // Row 3: Company | Designer   — 50u  (mainZone split in half)
    const qreal r0H = 110.0, r1H = 80.0, r2H = 70.0, r3H = blockH - r0H - r1H - r2H;

    QFont lFont("Inter", 16);
    QFont vFont("Inter", 22, QFont::Medium);
    QFont vSmFont("Inter", 18);
    QFont projFont("Inter", 32, QFont::Bold);
    QFont sheetFont("Inter", 24, QFont::Medium);

    auto drawCell = [&](const QRectF& r, const QString& label, const QString& value,
                        const QFont& valF, const QColor& vc, bool altBackground = false) {
        if (altBackground) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(altBg);
            painter->drawRect(r);
            painter->setBrush(Qt::NoBrush);
        }
        painter->setPen(lFont.pointSize() > 0 ? QPen(labelClr) : QPen(Qt::NoPen));
        painter->setFont(lFont);
        painter->drawText(r.adjusted(10, 5, -5, 0), Qt::AlignTop | Qt::AlignLeft, label);
        painter->setPen(vc);
        painter->setFont(valF);
        painter->drawText(r.adjusted(10, 28, -8, -5), Qt::AlignBottom | Qt::AlignLeft, value);
    };

    // ── Main zone rows ─────────────────────────────────────────────────────
    qreal my = mainZone.top();

    // Row 0: Project Name
    QRectF mR0(mainZone.left(), my, mainW, r0H);
    drawCell(mR0, "PROJECT", m_tb.projectName, projFont, titleClr);
    painter->setPen(thinPen);
    painter->drawLine(mR0.bottomLeft(), mR0.bottomRight());
    my += r0H;

    // Row 1: Sheet Name
    QRectF mR1(mainZone.left(), my, mainW, r1H);
    drawCell(mR1, "SHEET", m_tb.sheetName.isEmpty() ? "—" : m_tb.sheetName, sheetFont, valClr);
    painter->setPen(thinPen);
    painter->drawLine(mR1.bottomLeft(), mR1.bottomRight());
    my += r1H;

    // Row 2: Description
    QRectF mR2(mainZone.left(), my, mainW, r2H);
    drawCell(mR2, "DESCRIPTION", m_tb.description.isEmpty() ? "—" : m_tb.description, vSmFont, valClr);
    painter->setPen(thinPen);
    painter->drawLine(mR2.bottomLeft(), mR2.bottomRight());
    my += r2H;

    // Row 3: Company | Designer (side by side)
    qreal hw = mainW / 2.0;
    QRectF mR3a(mainZone.left(),       my, hw,   r3H);
    QRectF mR3b(mainZone.left() + hw,  my, hw,   r3H);
    drawCell(mR3a, "COMPANY",  m_tb.company.isEmpty()  ? "—" : m_tb.company,  vSmFont, valClr);
    painter->setPen(thinPen);
    painter->drawLine(mR3a.topRight(), mR3a.bottomRight()); // inner divider
    drawCell(mR3b, "DESIGNED BY", m_tb.designer.isEmpty() ? "—" : m_tb.designer, vSmFont, valClr);

    // ── Right zone — 4 stacked cells ──────────────────────────────────────
    //  Rev / Sheet N of M / Date / Size
    const qreal rhH = blockH / 4.0;
    qreal ry = rightZone.top();

    auto drawRightCell = [&](const QString& label, const QString& value) {
        QRectF r(rightZone.left(), ry, rightW, rhH);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Qt::NoBrush);

        painter->setPen(thinPen);
        painter->drawLine(r.bottomLeft(), r.bottomRight());

        painter->setFont(lFont);
        painter->setPen(labelClr);
        painter->drawText(r.adjusted(12, 5, -5, 0), Qt::AlignTop | Qt::AlignLeft, label);

        painter->setFont(vFont);
        painter->setPen(valClr);
        painter->drawText(r.adjusted(10, 28, -10, -5), Qt::AlignBottom | Qt::AlignHCenter, value);

        ry += rhH;
    };

    // Rev
    drawRightCell("REVISION", m_tb.revision.isEmpty() ? "A" : m_tb.revision);
    // Sheet N of M
    QString sheetStr = QString("Sheet %1 of %2")
                           .arg(m_tb.sheetNumber.isEmpty()  ? "1" : m_tb.sheetNumber)
                           .arg(m_tb.sheetTotal.isEmpty()   ? "1" : m_tb.sheetTotal);
    drawRightCell("SHEETS", sheetStr);
    // Date
    QString dateStr = m_tb.date.isEmpty()
                       ? QDate::currentDate().toString("yyyy-MM-dd")
                       : m_tb.date;
    drawRightCell("DATE", dateStr);
    // Size
    drawRightCell("SIZE", m_currentPageSize.isEmpty() ? "A4" : m_currentPageSize);

    // Top divider lines in right zone (already drawn by drawRightCell)
    // Final horizontal rule between rows
    painter->setPen(thinPen);
    painter->drawLine(rightZone.bottomLeft(), rightZone.bottomRight());
}
