#include "bridge/BridgeLua.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>

#include "bridge/BridgeJson.h"
#include "fceulua.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QString evalLuaBytes(const QByteArray& code, const QString& path, int byteCount)
{
	char result[32768] = {0};
	char output[16384] = {0};
	char error[4096] = {0};
	char traceback[8192] = {0};

	FCEU_CRITICAL_SECTION(lock);
	if (!FCEU_LuaBridgeEvalJson(code.constData(), static_cast<unsigned int>(code.size()), result, sizeof(result), output, sizeof(output), error, sizeof(error), traceback, sizeof(traceback)))
	{
		QString response = QStringLiteral("{\"ok\":false,");
		if (!path.isEmpty())
		{
			response += QString("\"path\":%1,").arg(JsonString(path));
		}
		response += QString("\"error\":%1,\"traceback\":%2,\"output\":%3}")
			.arg(JsonString(QString::fromLocal8Bit(error)))
			.arg(JsonString(QString::fromLocal8Bit(traceback)))
			.arg(JsonString(QString::fromLocal8Bit(output)));
		return response;
	}

	if (!path.isEmpty())
	{
		return QString("{\"ok\":true,\"path\":%1,\"bytes\":%2,\"result\":%3,\"output\":%4,\"running\":%5}")
			.arg(JsonString(path))
			.arg(byteCount)
			.arg(QString::fromLocal8Bit(result))
			.arg(JsonString(QString::fromLocal8Bit(output)))
			.arg(FCEU_LuaRunning() ? "true" : "false");
	}

	return QString("{\"ok\":true,\"result\":%1,\"output\":%2,\"running\":%3}")
		.arg(QString::fromLocal8Bit(result))
		.arg(JsonString(QString::fromLocal8Bit(output)))
		.arg(FCEU_LuaRunning() ? "true" : "false");
}

}

QString LuaEvalResponse(const QString& code)
{
	return evalLuaBytes(code.toUtf8(), QString(), 0);
}

QString LuaLoadResponse(const QString& path)
{
	const QFileInfo fileInfo(path);
	if (!fileInfo.exists() || !fileInfo.isFile())
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString("Lua file not found: %1").arg(path)));
	}

	QFile file(fileInfo.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString("failed to read Lua file: %1").arg(file.errorString())));
	}

	const QByteArray code = file.readAll();
	return evalLuaBytes(code, fileInfo.absoluteFilePath(), code.size());
}

QString LuaResetResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	FCEU_LuaStop();
	return QStringLiteral("{\"ok\":true,\"running\":false}");
}

QString LuaStatusResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	return QString("{\"ok\":true,\"running\":%1,\"script\":%2}")
		.arg(FCEU_LuaRunning() ? "true" : "false")
		.arg(FCEU_GetLuaScriptName() ? JsonString(QString::fromLocal8Bit(FCEU_GetLuaScriptName())) : QStringLiteral("null"));
}

QString LuaEvalCommandResponse(const QStringList& parts)
{
	if (parts.size() != 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"LUA_EVAL requires quoted code\"}");
	}
	return LuaEvalResponse(parts[1]);
}

QString LuaLoadCommandResponse(const QStringList& parts)
{
	if (parts.size() != 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"LUA_LOAD requires path\"}");
	}
	return LuaLoadResponse(parts[1]);
}

QString LuaResetCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"LUA_RESET takes no arguments\"}");
	}
	return LuaResetResponse();
}

QString LuaStatusCommandResponse(const QStringList& parts)
{
	Q_UNUSED(parts);
	return LuaStatusResponse();
}

}
