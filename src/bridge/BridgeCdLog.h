#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

QString CdLogStartResponse();
QString CdLogStopResponse();
QString CdLogDumpResponse(const QString& domain);
QString CdLogStartCommandResponse(const QStringList& parts);
QString CdLogStopCommandResponse(const QStringList& parts);
QString CdLogDumpCommandResponse(const QStringList& parts);

}
