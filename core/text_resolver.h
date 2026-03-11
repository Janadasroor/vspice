#ifndef TEXT_RESOLVER_H
#define TEXT_RESOLVER_H

#include <QString>
#include <QMap>
#include <QRegularExpression>

/**
 * @brief Utility class to resolve variables in text strings (e.g., ${REFERENCE} -> U1)
 */
class TextResolver {
public:
    static QString resolve(const QString& input, const QMap<QString, QString>& variables) {
        QString result = input;
        QRegularExpression re("\\$\\{([^\\}]+)\\}");
        QRegularExpressionMatchIterator it = re.globalMatch(input);
        
        // We iterate backwards to replace without affecting earlier match positions
        QList<QRegularExpressionMatch> matches;
        while (it.hasNext()) matches.prepend(it.next());
        
        for (const auto& match : matches) {
            QString varName = match.captured(1);
            if (variables.contains(varName)) {
                result.replace(match.capturedStart(), match.capturedLength(), variables[varName]);
            }
        }
        
        return result;
    }
};

#endif // TEXT_RESOLVER_H
