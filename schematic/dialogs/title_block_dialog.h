#ifndef TITLE_BLOCK_DIALOG_H
#define TITLE_BLOCK_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include "schematic_page_item.h"

class QPushButton;
class QSpinBox;

// ─── TitleBlockDialog ──────────────────────────────────────────────────────
// Interactive editor for all schematic title block fields.
// Double-click the title block corner (or use Edit > Title Block) to open.
class TitleBlockDialog : public QDialog {
    Q_OBJECT
public:
    explicit TitleBlockDialog(const TitleBlockData& current,
                              int totalSheets = 1,
                              QWidget* parent = nullptr);

    TitleBlockData result() const;

private slots:
    void browseLogo();
    void clearLogo();

private:
    void setupUi(const TitleBlockData& d, int totalSheets);
    QWidget* makeRow(const QString& label, QLineEdit*& field, const QString& value);

    QLineEdit* m_projectName;
    QLineEdit* m_sheetName;
    QLineEdit* m_company;
    QLineEdit* m_designer;
    QLineEdit* m_revision;
    QLineEdit* m_description;
    QLineEdit* m_date;
    QSpinBox*  m_sheetNumber;
    QSpinBox*  m_sheetTotal;
    QLineEdit* m_logoPath;
    QLabel*    m_logoPreview;
};

#endif // TITLE_BLOCK_DIALOG_H
