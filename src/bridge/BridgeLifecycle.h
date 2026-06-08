#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

QString StatusResponse();
QString PauseResponse();
QString ResumeResponse();
QString FrameResponse(int frames);
QString WaitResponse();
QString LoadRomResponse(const QString& path);
QString ResetResponse();
QString PowerResponse();
QString AppExitResponse();
QString FrameCommandResponse(const QStringList& parts);
QString LoadRomCommandResponse(const QStringList& parts);

}
