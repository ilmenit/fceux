#pragma once

#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

QString JsonEscape(const QString& value);
QString JsonString(const QString& value);
QString JsonRawBytes(const uint8* bytes, int len);
QString JsonStringArray(const QStringList& values);

}
