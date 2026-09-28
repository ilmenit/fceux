#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

bool TokenizeCommand(const QString& line, QStringList* out, QString* error);
bool ParseNumber(const QString& text, uint* value);
QString OptionValue(const QStringList& parts, const QString& name);

}
