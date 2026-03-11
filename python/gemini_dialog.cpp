#include "gemini_dialog.h"
#include "gemini_panel.h"
#include <QVBoxLayout>

GeminiDialog::GeminiDialog(QGraphicsScene* scene, QWidget* parent) 
    : QDialog(parent) {
    qDebug() << "Initializing GeminiDialog... scene:" << scene;
    setWindowTitle("Gemini AI Assistant");
    resize(700, 500);
    setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    m_panel = new GeminiPanel(scene, this);
    layout->addWidget(m_panel);
}
