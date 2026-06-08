#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

QString ApuStateResponse();
QString ApuStateCommandResponse(const QStringList& parts);

}
