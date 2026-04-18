// schematic_editor_export.cpp
// Export and app-level informational handlers for SchematicEditor

#include "schematic_editor.h"

#include "config_manager.h"
#include "settings_dialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPrinter>
#include <QSvgGenerator>

void SchematicEditor::onExportPDF() {
    QString initialPath = m_currentFilePath.isEmpty() ? "schematic.pdf" : QFileInfo(m_currentFilePath).absolutePath() + "/schematic.pdf";
    QString file = QFileDialog::getSaveFileName(this, "Export PDF", initialPath, "PDF Files (*.pdf)");
    if (file.isEmpty()) return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(file);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(QPageLayout::Landscape);

    QPainter painter(&printer);
    painter.setRenderHint(QPainter::Antialiasing);

    // Prioritize the page frame if it exists, otherwise use items bounding rect
    QRectF source = m_pageFrame ? m_pageFrame->boundingRect() : m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);

    // Map to the printer's paintable area
    QRectF target = painter.viewport();
    m_scene->render(&painter, target, source, Qt::KeepAspectRatio);
    painter.end();
    statusBar()->showMessage("Exported PDF to " + file, 5000);
}

void SchematicEditor::onExportSVG() {
    QString initialPath = m_currentFilePath.isEmpty() ? "schematic.svg" : QFileInfo(m_currentFilePath).absolutePath() + "/schematic.svg";
    QString file = QFileDialog::getSaveFileName(this, "Export SVG", initialPath, "SVG Files (*.svg)");
    if (file.isEmpty()) return;

    // Determine the export area
    QRectF rect = m_pageFrame ? m_pageFrame->boundingRect() : m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);

    QSvgGenerator generator;
    generator.setFileName(file);
    generator.setSize(rect.size().toSize());
    generator.setViewBox(rect);
    generator.setTitle("viospice Schematic");

    QPainter painter(&generator);
    painter.setRenderHint(QPainter::Antialiasing);
    // Explicitly render the source rect to fill the generator's coordinate space
    m_scene->render(&painter, rect, rect);
    painter.end();
    statusBar()->showMessage("Exported SVG to " + file, 5000);
}

void SchematicEditor::onExportImage() {
    QString initialPath = m_currentFilePath.isEmpty() ? "schematic.png" : QFileInfo(m_currentFilePath).absolutePath() + "/schematic.png";
    QString file = QFileDialog::getSaveFileName(this, "Export Image", initialPath, "Images (*.png *.jpg)");
    if (file.isEmpty()) return;

    QRectF rect = m_pageFrame ? m_pageFrame->boundingRect() : m_scene->itemsBoundingRect().adjusted(-50, -50, 50, 50);

    // Create a high-resolution image (300 DPI approx, scale 4x)
    qreal scale = 4.0;
    QImage image(rect.size().toSize() * scale, QImage::Format_ARGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    // Offset painter to match the source rect origin
    painter.translate(-rect.topLeft());
    m_scene->render(&painter, rect, rect);
    painter.end();

    image.save(file);
    statusBar()->showMessage("Exported Image to " + file, 5000);
}

void SchematicEditor::onSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        auto& config = ConfigManager::instance();
        m_view->setSnapToGrid(config.snapToGrid());
        m_view->viewport()->update();
        applyTheme(); // Refresh theme if changed
    }
}

void SchematicEditor::onAbout() {
    QMessageBox::about(this, "About viospice",
        "<h3>viospice Schematic Editor</h3>"
        "<p>Version 0.1.0</p>"
        "<p>Professional Electronics Design Automation software.</p>"
        "<p>Copyright 2026 viospice Team</p>");
}
