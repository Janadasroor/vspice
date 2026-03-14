#ifndef TEXT_PROPERTIES_DIALOG_H
#define TEXT_PROPERTIES_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QColor>
#include <QPushButton>

class TextPropertiesDialog : public QDialog {
    Q_OBJECT
public:
    explicit TextPropertiesDialog(QWidget* parent = nullptr);

    QString text() const { return m_textEdit->text(); }
    int fontSize() const { return m_fontSizeSpin->value(); }
    QColor color() const { return m_color; }
    QString alignment() const { return m_alignCombo->currentText(); }
    void setColor(const QColor& c);

private slots:
    void onPickColor();

private:
    QLineEdit* m_textEdit;
    QSpinBox* m_fontSizeSpin;
    QComboBox* m_alignCombo;
    QColor m_color;
    QPushButton* m_colorBtn;
};

#endif
