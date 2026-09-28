#pragma once

#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

bool ParseSignedNumber(const QString& text, int* value);
QString SymbolLoadAutoResponse();
QString SymbolLoadFileResponse(const QString& path, const QString& bankText);
QString SymbolResolveResponse(const QString& name, bool hasBank, int bank);
QString SymbolLookupResponse(uint addr, bool hasBank, int bank);
QString SymbolLoadCommandResponse(const QStringList& parts);
QString SymbolResolveCommandResponse(const QStringList& parts);
QString SymbolLookupCommandResponse(const QStringList& parts);

}
