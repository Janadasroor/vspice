#include "visual_pin_mapper.h"
#include <QPainter>
#include <QMouseEvent>
#include <QInputDialog>
#include <algorithm>

VisualPinMapper::VisualPinMapper(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 300);
    setMouseTracking(true);
}

void VisualPinMapper::setPins(const QStringList& inputs, const QStringList& outputs) {
    m_inputs = inputs;
    m_outputs = outputs;
    updateRects();
    update();
}

void VisualPinMapper::updateRects() {
    m_pinRects.clear();
    int w = width();
    int h = height();
    int maxPins = std::max(m_inputs.size(), m_outputs.size());
    int blockH = std::max(100, maxPins * 30 + 40);
    int blockW = 150;
    
    m_blockRect = QRectF((w - blockW) / 2, (h - blockH) / 2, blockW, blockH);
    
    // Input Pins (Left)
    int startY = m_blockRect.top() + 20;
    for (int i = 0; i < m_inputs.size(); ++i) {
        PinRect pr;
        pr.rect = QRectF(m_blockRect.left() - 20, startY + i * 30 - 10, 40, 20);
        pr.index = i;
        pr.isInput = true;
        pr.name = m_inputs[i];
        m_pinRects.append(pr);
    }
    
    // Output Pins (Right)
    for (int i = 0; i < m_outputs.size(); ++i) {
        PinRect pr;
        pr.rect = QRectF(m_blockRect.right() - 20, startY + i * 30 - 10, 40, 20);
        pr.index = i;
        pr.isInput = false;
        pr.name = m_outputs[i];
        m_pinRects.append(pr);
    }
}

void VisualPinMapper::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    painter.fillRect(rect(), QColor(30, 30, 30));
    
    // Draw Block
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(QColor(45, 45, 48));
    painter.drawRoundedRect(m_blockRect, 5, 5);
    
    painter.setPen(QColor(139, 92, 246));
    painter.setFont(QFont("Inter", 8, QFont::Bold));
    painter.drawText(m_blockRect.adjusted(0, 5, 0, -m_blockRect.height() + 25), Qt::AlignCenter, "PIN MAPPER");

    // Draw Pins
    for (const auto& pr : m_pinRects) {
        bool isDragging = (m_dragIdx == pr.index && m_dragIsInput == pr.isInput);
        
        painter.setPen(QPen(isDragging ? QColor(99, 102, 241) : Qt::white, 1.5));
        painter.setBrush(isDragging ? QColor(99, 102, 241, 100) : QColor(60, 60, 60));
        
        // Draw pin line
        if (pr.isInput) {
            painter.drawLine(pr.rect.left(), pr.rect.center().y(), pr.rect.right() - 20, pr.rect.center().y());
            painter.drawEllipse(pr.rect.left() - 5, pr.rect.center().y() - 3, 6, 6);
            painter.drawText(pr.rect.adjusted(25, 0, 100, 0), Qt::AlignLeft | Qt::AlignVCenter, pr.name);
        } else {
            painter.drawLine(pr.rect.left() + 20, pr.rect.center().y(), pr.rect.right(), pr.rect.center().y());
            painter.drawEllipse(pr.rect.right() - 1, pr.rect.center().y() - 3, 6, 6);
            painter.drawText(pr.rect.adjusted(-100, 0, -25, 0), Qt::AlignRight | Qt::AlignVCenter, pr.name);
        }
    }
    
    painter.setPen(QColor(100, 100, 100));
    painter.setFont(QFont("Inter", 7));
    painter.drawText(rect().adjusted(10, 0, -10, -10), Qt::AlignBottom | Qt::AlignCenter, 
                     "Drag to reorder pins • Double-click to rename");
}

void VisualPinMapper::mousePressEvent(QMouseEvent* event) {
    for (const auto& pr : m_pinRects) {
        if (pr.rect.contains(event->pos())) {
            m_dragIdx = pr.index;
            m_dragIsInput = pr.isInput;
            m_dragPos = event->pos();
            update();
            return;
        }
    }
}

void VisualPinMapper::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragIdx != -1) {
        update();
    }
}

void VisualPinMapper::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragIdx != -1) {
        // Find new index based on Y position
        int newIdx = (event->pos().y() - (m_blockRect.top() + 20)) / 30;
        qsizetype count = m_dragIsInput ? m_inputs.size() : m_outputs.size();
        newIdx = (int)std::max((qsizetype)0, std::min((qsizetype)newIdx, count - 1));
        
        if (newIdx != m_dragIdx) {
            if (m_dragIsInput) {
                QString name = m_inputs.takeAt(m_dragIdx);
                m_inputs.insert(newIdx, name);
            } else {
                QString name = m_outputs.takeAt(m_dragIdx);
                m_outputs.insert(newIdx, name);
            }
            emit pinsChanged();
        }
        
        m_dragIdx = -1;
        updateRects();
        update();
    }
}

void VisualPinMapper::mouseDoubleClickEvent(QMouseEvent* event) {
    for (const auto& pr : m_pinRects) {
        if (pr.rect.contains(event->pos())) {
            bool ok;
            QString newName = QInputDialog::getText(this, "Rename Pin", "Enter new name:", QLineEdit::Normal, pr.name, &ok);
            if (ok && !newName.isEmpty()) {
                if (pr.isInput) m_inputs[pr.index] = newName;
                else m_outputs[pr.index] = newName;
                emit pinsChanged();
                updateRects();
                update();
            }
            return;
        }
    }
}
