#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

QString StepResponse(int count, int timeoutFrames);
QString StepOutResponse(int timeoutFrames);
QString StepOverResponse(int timeoutFrames);
QString RunUntilResponse(const QString& target, int timeoutFrames);
QString StepCommandResponse(const QStringList& parts);
QString StepOutCommandResponse(const QStringList& parts);
QString StepOverCommandResponse(const QStringList& parts);
QString RunUntilCommandResponse(const QStringList& parts);

}
