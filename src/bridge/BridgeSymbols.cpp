#include "bridge/BridgeSymbols.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <climits>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "debug.h"
#include "debugsymboltable.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QString hexValue(uint value, int width)
{
	return QString("$%1").arg(value, width, 16, QLatin1Char('0'));
}

bool inferNlBankFromPath(const QFileInfo& fileInfo, int* bank)
{
	const QString name = fileInfo.fileName();
	if (name.endsWith(QStringLiteral(".ram.nl"), Qt::CaseInsensitive))
	{
		*bank = -1;
		return true;
	}

	static const QRegularExpression pageSuffix(QStringLiteral("\\.([0-9a-fA-F]+)\\.nl$"));
	const QRegularExpressionMatch match = pageSuffix.match(name);
	if (!match.hasMatch())
	{
		return false;
	}

	bool ok = false;
	const int parsed = match.captured(1).toInt(&ok, 16);
	if (!ok)
	{
		return false;
	}
	*bank = parsed;
	return true;
}

QString symbolJson(debugSymbol_t* sym, int bank, bool includeBank)
{
	if (sym == nullptr)
	{
		return QStringLiteral("{\"found\":false}");
	}
	return QString("{\"found\":true,\"name\":%1,\"addr\":%2,\"offset\":%3,\"bank\":%4,\"comment\":%5}")
		.arg(JsonString(QString::fromStdString(sym->name())))
		.arg(JsonString(hexValue(static_cast<uint>(sym->offset()) & 0xFFFF, 4)))
		.arg(sym->offset())
		.arg(includeBank ? QString::number(bank) : QStringLiteral("null"))
		.arg(JsonString(QString::fromStdString(sym->comment())));
}

debugSymbol_t* resolveSymbolByName(const QString& name, int* bank)
{
	const std::string symbolName = name.toStdString();
	for (int candidate : {-2, -1})
	{
		if (debugSymbol_t* sym = debugSymbolTable.getSymbol(candidate, symbolName))
		{
			*bank = candidate;
			return sym;
		}
	}

	const int pageSize = 1 << debuggerPageSize;
	const int maxBanks = std::max(1, 0x100000 / pageSize);
	for (int candidate = 0; candidate < maxBanks; candidate++)
	{
		if (debugSymbol_t* sym = debugSymbolTable.getSymbol(candidate, symbolName))
		{
			*bank = candidate;
			return sym;
		}
	}
	return nullptr;
}

}

bool ParseSignedNumber(const QString& text, int* value)
{
	QString s = text.trimmed();
	int base = 10;
	bool negative = false;
	if (s.startsWith('-'))
	{
		negative = true;
		s = s.mid(1);
	}
	if (s.startsWith('$'))
	{
		s = s.mid(1);
		base = 16;
	}
	else if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
	{
		s = s.mid(2);
		base = 16;
	}

	bool ok = false;
	const uint parsed = s.toUInt(&ok, base);
	if (!ok || parsed > static_cast<uint>(INT_MAX))
	{
		return false;
	}
	*value = negative ? -static_cast<int>(parsed) : static_cast<int>(parsed);
	return true;
}

QString SymbolLoadAutoResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	debugSymbolTable.clear();
	if (debugSymbolTable.loadGameSymbols() < 0)
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString::fromLocal8Bit(debugSymbolTable.errorMessage())));
	}
	return QString("{\"ok\":true,\"auto\":true,\"symbols\":%1}").arg(debugSymbolTable.numSymbols());
}

QString SymbolLoadFileResponse(const QString& path, const QString& bankText)
{
	const QFileInfo fileInfo(path);
	if (!fileInfo.exists() || !fileInfo.isFile())
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString("symbol file not found: %1").arg(path)));
	}
	FCEU_CRITICAL_SECTION(lock);
	if (fileInfo.suffix().compare(QStringLiteral("dbg"), Qt::CaseInsensitive) == 0)
	{
		debugSymbolTable.clear();
		if (debugSymbolTable.ld65LoadDebugFile(fileInfo.absoluteFilePath().toLocal8Bit().constData()) < 0)
		{
			return QString("{\"ok\":false,\"error\":%1}")
				.arg(JsonString(QString::fromLocal8Bit(debugSymbolTable.errorMessage())));
		}
		return QString("{\"ok\":true,\"path\":%1,\"format\":\"ld65-dbg\",\"symbols\":%2}")
			.arg(JsonString(fileInfo.absoluteFilePath()))
			.arg(debugSymbolTable.numSymbols());
	}
	if (fileInfo.suffix().compare(QStringLiteral("nl"), Qt::CaseInsensitive) != 0)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"SYM_LOAD supports ld65 .dbg files and FCEUX .nl files\"}");
	}
	int bank = 0;
	if (!bankText.isEmpty())
	{
		if (!ParseSignedNumber(bankText, &bank))
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid bank\"}");
		}
	}
	else if (!inferNlBankFromPath(fileInfo, &bank))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"SYM_LOAD .nl requires bank=N unless the filename ends with .ram.nl or .<hex>.nl\"}");
	}
	debugSymbolTable.clear();
	if (debugSymbolTable.loadFileNL(bank, fileInfo.absoluteFilePath().toLocal8Bit().constData()) < 0)
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString::fromLocal8Bit(debugSymbolTable.errorMessage())));
	}
	return QString("{\"ok\":true,\"path\":%1,\"format\":\"fceux-nl\",\"bank\":%2,\"symbols\":%3}")
		.arg(JsonString(fileInfo.absoluteFilePath()))
		.arg(bank)
		.arg(debugSymbolTable.numSymbols());
}

QString SymbolResolveResponse(const QString& name, bool hasBank, int bank)
{
	FCEU_CRITICAL_SECTION(lock);
	debugSymbol_t* sym = hasBank
		? debugSymbolTable.getSymbol(bank, name.toStdString())
		: resolveSymbolByName(name, &bank);
	return QString("{\"ok\":true,\"query\":%1,\"symbol\":%2}")
		.arg(JsonString(name))
		.arg(symbolJson(sym, bank, sym != nullptr || hasBank));
}

QString SymbolLookupResponse(uint addr, bool hasBank, int bank)
{
	if (!hasBank)
	{
		bank = (addr >= 0x8000) ? getBank(static_cast<int>(addr)) : -1;
	}
	FCEU_CRITICAL_SECTION(lock);
	debugSymbol_t* sym = debugSymbolTable.getSymbolAtBankOffset(bank, static_cast<int>(addr));
	if (sym == nullptr && bank == -1)
	{
		sym = debugSymbolTable.getSymbolAtBankOffset(-2, static_cast<int>(addr));
		if (sym != nullptr)
		{
			bank = -2;
		}
	}
	return QString("{\"ok\":true,\"addr\":%1,\"symbol\":%2}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(symbolJson(sym, bank, true));
}

QString SymbolLoadCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"SYM_LOAD requires path or auto=true\"}");
	}
	const QString autoText = OptionValue(parts, QStringLiteral("auto"));
	const bool autoLoad = parts[1].compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0 || autoText == QStringLiteral("true");
	if (autoLoad)
	{
		return SymbolLoadAutoResponse();
	}

	return SymbolLoadFileResponse(parts[1], OptionValue(parts, QStringLiteral("bank")));
}

QString SymbolResolveCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"SYM_RESOLVE requires name [bank=N]\"}");
	}
	int bank = 0;
	const QString bankText = OptionValue(parts, QStringLiteral("bank"));
	const bool hasBank = !bankText.isEmpty();
	if (hasBank && !ParseSignedNumber(bankText, &bank))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid symbol bank\"}");
	}
	return SymbolResolveResponse(parts[1], hasBank, bank);
}

QString SymbolLookupCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"SYM_LOOKUP requires addr [bank=N]\"}");
	}
	uint addr = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	int bank = 0;
	const QString bankText = OptionValue(parts, QStringLiteral("bank"));
	const bool hasBank = !bankText.isEmpty();
	if (hasBank && !ParseSignedNumber(bankText, &bank))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid symbol bank\"}");
	}
	return SymbolLookupResponse(addr, hasBank, bank);
}

}
