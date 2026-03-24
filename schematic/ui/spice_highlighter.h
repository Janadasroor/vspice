#ifndef SPICE_HIGHLIGHTER_H
#define SPICE_HIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class SpiceHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SpiceHighlighter(QTextDocument* parent = nullptr);
    void updateColors();

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightingRule> highlightingRules;

    QTextCharFormat componentFormat;
    QTextCharFormat commentFormat;
    QTextCharFormat commandFormat;
    QTextCharFormat valueFormat;
    QTextCharFormat subcktFormat;
    QTextCharFormat errorFormat;
    QTextCharFormat warningFormat;
};

#endif // SPICE_HIGHLIGHTER_H
