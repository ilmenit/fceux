#include "bridge/BridgeCdLog.h"

#include <QByteArray>

#include <cstdlib>
#include <cstring>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "fceu.h"
#include "cart.h"
#include "debug.h"
#include "ppu.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QString cdlogStatusJson()
{
	return QString("{\"ok\":true,\"enabled\":%1,\"cpu_size\":%2,\"ppu_size\":%3,\"code\":%4,\"data\":%5,\"undefined\":%6,\"render\":%7,\"vrom_read\":%8,\"undefined_vrom\":%9}")
		.arg(FCEUI_GetLoggingCD() ? "true" : "false")
		.arg(cdloggerdataSize)
		.arg(cdloggerVideoDataSize)
		.arg(static_cast<int>(codecount))
		.arg(static_cast<int>(datacount))
		.arg(static_cast<int>(undefinedcount))
		.arg(static_cast<int>(rendercount))
		.arg(static_cast<int>(vromreadcount))
		.arg(static_cast<int>(undefinedvromcount));
}

void resetCdLogBuffers()
{
	codecount = 0;
	datacount = 0;
	rendercount = 0;
	vromreadcount = 0;
	undefinedcount = cdloggerdataSize;
	if (cdloggerdata != nullptr && cdloggerdataSize != 0)
	{
		std::memset(cdloggerdata, 0, cdloggerdataSize);
	}
	if (cdloggervdata != nullptr)
	{
		const unsigned int ppuSize = cdloggerVideoDataSize != 0 ? cdloggerVideoDataSize : 8192;
		undefinedvromcount = ppuSize;
		std::memset(cdloggervdata, 0, ppuSize);
	}
	else
	{
		undefinedvromcount = 0;
	}
}

bool ensureCdLogBuffers(QString* error)
{
	if (PRGptr[0] == nullptr || PRGsize[0] == 0)
	{
		*error = QStringLiteral("no PRG ROM available");
		return false;
	}
	if (cdloggerdata == nullptr || cdloggerdataSize != PRGsize[0])
	{
		std::free(cdloggerdata);
		cdloggerdataSize = PRGsize[0];
		cdloggerdata = static_cast<unsigned char*>(std::malloc(cdloggerdataSize));
		if (cdloggerdata == nullptr)
		{
			cdloggerdataSize = 0;
			*error = QStringLiteral("failed to allocate CPU CD log");
			return false;
		}
	}

	const unsigned int ppuSize = CHRsize[0] != 0 ? CHRsize[0] : 8192;
	if (cdloggervdata == nullptr || (cdloggerVideoDataSize != 0 ? cdloggerVideoDataSize : 8192) != ppuSize)
	{
		std::free(cdloggervdata);
		cdloggerVideoDataSize = CHRsize[0];
		cdloggervdata = static_cast<unsigned char*>(std::malloc(ppuSize));
		if (cdloggervdata == nullptr)
		{
			cdloggerVideoDataSize = 0;
			*error = QStringLiteral("failed to allocate PPU CD log");
			return false;
		}
	}
	resetCdLogBuffers();
	return true;
}

}

QString CdLogStartResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded() || GameInfo == nullptr)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QString error;
	if (!ensureCdLogBuffers(&error))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(error));
	}
	FCEUI_SetLoggingCD(1);
	return cdlogStatusJson();
}

QString CdLogStopResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	FCEUI_SetLoggingCD(0);
	return cdlogStatusJson();
}

QString CdLogDumpResponse(const QString& domain)
{
	FCEU_CRITICAL_SECTION(lock);
	QString cpuJson = QStringLiteral("null");
	QString ppuJson = QStringLiteral("null");
	if ((domain == QStringLiteral("cpu") || domain == QStringLiteral("all")) && cdloggerdata != nullptr && cdloggerdataSize != 0)
	{
		const QByteArray bytes(reinterpret_cast<const char*>(cdloggerdata), static_cast<int>(cdloggerdataSize));
		cpuJson = QString("{\"size\":%1,\"base64\":%2}")
			.arg(cdloggerdataSize)
			.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
	}
	if ((domain == QStringLiteral("ppu") || domain == QStringLiteral("all")) && cdloggervdata != nullptr)
	{
		const unsigned int ppuSize = cdloggerVideoDataSize != 0 ? cdloggerVideoDataSize : 8192;
		const QByteArray bytes(reinterpret_cast<const char*>(cdloggervdata), static_cast<int>(ppuSize));
		ppuJson = QString("{\"size\":%1,\"base64\":%2}")
			.arg(ppuSize)
			.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
	}
	return QString("{\"ok\":true,\"domain\":%1,\"status\":%2,\"cpu\":%3,\"ppu\":%4}")
		.arg(JsonString(domain))
		.arg(cdlogStatusJson().replace(QStringLiteral("{\"ok\":true,"), QStringLiteral("{")))
		.arg(cpuJson)
		.arg(ppuJson);
}

QString CdLogStartCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CDLOG_START takes no arguments\"}");
	}
	return CdLogStartResponse();
}

QString CdLogStopCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CDLOG_STOP takes no arguments\"}");
	}
	return CdLogStopResponse();
}

QString CdLogDumpCommandResponse(const QStringList& parts)
{
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CDLOG_DUMP accepts optional domain=cpu|ppu|all\"}");
	}
	QString domain = OptionValue(parts, QStringLiteral("domain"));
	if (domain.isEmpty())
	{
		domain = parts.size() == 2 ? parts[1] : QStringLiteral("all");
	}
	domain = domain.toLower();
	if (domain != QStringLiteral("cpu") && domain != QStringLiteral("ppu") && domain != QStringLiteral("all"))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CDLOG_DUMP domain must be cpu, ppu, or all\"}");
	}
	return CdLogDumpResponse(domain);
}

}
