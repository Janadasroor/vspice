#include "spice_highlighter.h"

#include "theme_manager.h"

SpiceHighlighter::SpiceHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    updateColors();
}

void SpiceHighlighter::updateColors() {
    highlightingRules.clear();
    
    PCBTheme* theme = ThemeManager::theme();
    bool isLight = theme && theme->type() == PCBTheme::Light;

    // Components (R, C, L, V, I, Q, M, D, X, etc.)
    componentFormat.setForeground(isLight ? QColor("#0550ae") : QColor("#569cd6")); 
    componentFormat.setFontWeight(QFont::Bold);
    
    HighlightingRule rule;
    rule.pattern = QRegularExpression("^[RrCcLlVvIiQqMmDdXx][A-Za-z0-9_]*");
    rule.format = componentFormat;
    highlightingRules.append(rule);

    // Commands (.tran, .ac, .dc, .op, .model, .subckt, etc.)
    commandFormat.setForeground(isLight ? QColor("#8250df") : QColor("#c586c0"));
    commandFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression("^\\.[A-Za-z]+");
    rule.format = commandFormat;
    highlightingRules.append(rule);

    // Subcircuits
    subcktFormat.setForeground(isLight ? QColor("#0e707e") : QColor("#4ec9b0"));
    rule.pattern = QRegularExpression("\\b(subckt|ends)\\b", QRegularExpression::CaseInsensitiveOption);
    rule.format = subcktFormat;
    highlightingRules.append(rule);

    // Values (Numbers with suffixes k, m, u, n, p, f, meg, etc.)
    valueFormat.setForeground(isLight ? QColor("#067a40") : QColor("#b5cea8"));
    rule.pattern = QRegularExpression("\\b[0-9]*\\.?[0-9]+([eE][-+]?[0-9]+|[tgkMmUnPfF]|meg)?\\b");
    rule.format = valueFormat;
    highlightingRules.append(rule);

    // Comments
    commentFormat.setForeground(isLight ? QColor("#6e7781") : QColor("#6a9955"));
    rule.pattern = QRegularExpression("^\\*.*");
    rule.format = commentFormat;
    highlightingRules.append(rule);
    
    rule.pattern = QRegularExpression(";.*");
    highlightingRules.append(rule);

    // Errors (e.g. "Error: ...", "Fatal error: ...")
    errorFormat.setForeground(isLight ? QColor("#d32f2f") : QColor("#f44336"));
    errorFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression("(?i).*(error|fatal error):.*");
    rule.format = errorFormat;
    highlightingRules.append(rule);

    // Warnings (e.g. "Warning: ...")
    warningFormat.setForeground(isLight ? QColor("#ed6c02") : QColor("#ffb300")); 
    warningFormat.setFontWeight(QFont::Bold);
    rule.pattern = QRegularExpression("(?i).*warning:.*");
    rule.format = warningFormat;
    highlightingRules.append(rule);
    
    rehighlight();
}

void SpiceHighlighter::highlightBlock(const QString& text) {
    for (const HighlightingRule& rule : highlightingRules) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
