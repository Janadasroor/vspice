#pragma once

#include <QString>
#include <QStringList>

class IDocumentIO {
public:
    virtual ~IDocumentIO() = default;

    virtual QString formatId() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList supportedExtensions() const = 0;

    virtual QString lastError() const = 0;
};
