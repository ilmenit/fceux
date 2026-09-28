#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

QString LuaEvalResponse(const QString& code);
QString LuaLoadResponse(const QString& path);
QString LuaResetResponse();
QString LuaStatusResponse();
QString LuaEvalCommandResponse(const QStringList& parts);
QString LuaLoadCommandResponse(const QStringList& parts);
QString LuaResetCommandResponse(const QStringList& parts);
QString LuaStatusCommandResponse(const QStringList& parts);

}
