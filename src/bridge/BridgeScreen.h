#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

QString ScreenResponse(bool screenshot, bool overlay, bool includeInline, const QString& path);
QString ScreenCommandResponse(const QString& cmd, const QStringList& parts);

}
