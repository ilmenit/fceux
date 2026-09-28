#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

QString CpuPeekResponse(uint addr, uint len, bool singleByte);
QString CpuPeek16Response(uint addr);
QString BusPeekResponse(uint addr, uint len);
QString BusPeek16Response(uint addr);
QString CpuPokeResponse(uint addr, uint value, bool wide);
QString MemLoadResponse(uint addr, const QByteArray& bytes);
bool ParseBytePattern(const QString& text, QByteArray* pattern, QByteArray* mask, QString* error);
QString MemSearchResponse(const QString& domain, const QByteArray& pattern, const QByteArray& patternMask, uint start, uint end, bool endProvided, uint limit);
QString MemSearchCommandResponse(const QStringList& parts);
QString CpuPeekCommandResponse(const QString& cmd, const QStringList& parts);
QString CpuPeek16CommandResponse(const QStringList& parts);
QString BusPeekCommandResponse(const QStringList& parts);
QString BusPeek16CommandResponse(const QStringList& parts);
QString CpuPokeCommandResponse(const QString& cmd, const QStringList& parts);
QString MemLoadCommandResponse(const QStringList& parts);

}
