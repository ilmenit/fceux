#include "bridge/BridgeMemory.h"

#include <QStringList>
#include <climits>

#include "bridge/BridgeJson.h"
#include "bridge/BridgePpu.h"
#include "bridge/BridgeProtocol.h"
#include "debug.h"
#include "driver.h"
#include "fceu.h"
#include "cart.h"
#include "x6502.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QString hexValue(uint value, int width)
{
	return QString("$%1").arg(value, width, 16, QLatin1Char('0'));
}

QString jsonByteArray(const QByteArray& bytes)
{
	QStringList parts;
	parts.reserve(bytes.size());
	for (unsigned char b : bytes)
	{
		parts.append(QString::number(static_cast<uint>(b)));
	}
	return QString("[%1]").arg(parts.join(','));
}

}

QString CpuPeekResponse(uint addr, uint len, bool singleByte)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QByteArray bytes;
	bytes.reserve(static_cast<int>(len));
	for (uint i = 0; i < len; i++)
	{
		bytes.append(static_cast<char>(GetMem(static_cast<uint16>(addr + i))));
	}
	if (singleByte)
	{
		return QString("{\"ok\":true,\"addr\":%1,\"value\":%2}")
			.arg(JsonString(hexValue(addr, 4)))
			.arg(static_cast<unsigned char>(bytes[0]));
	}
	return QString("{\"ok\":true,\"addr\":%1,\"len\":%2,\"data\":%3,\"base64\":%4}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(len)
		.arg(jsonByteArray(bytes))
		.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
}

QString CpuPeek16Response(uint addr)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	const uint lo = GetMem(static_cast<uint16>(addr));
	const uint hi = GetMem(static_cast<uint16>((addr + 1) & 0xFFFF));
	return QString("{\"ok\":true,\"addr\":%1,\"value\":%2}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(lo | (hi << 8));
}

QString BusPeekResponse(uint addr, uint len)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QByteArray bytes;
	bytes.reserve(static_cast<int>(len));
	for (uint i = 0; i < len; i++)
	{
		const uint cur = addr + i;
		bytes.append(static_cast<char>(ARead[cur](cur)));
	}
	if (len == 1)
	{
		return QString("{\"ok\":true,\"source\":\"bus\",\"addr\":%1,\"value\":%2}")
			.arg(JsonString(hexValue(addr, 4)))
			.arg(static_cast<unsigned char>(bytes[0]));
	}
	return QString("{\"ok\":true,\"source\":\"bus\",\"addr\":%1,\"len\":%2,\"data\":%3,\"base64\":%4}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(len)
		.arg(jsonByteArray(bytes))
		.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
}

QString BusPeek16Response(uint addr)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	const uint lo = ARead[addr](addr);
	const uint addr2 = (addr + 1) & 0xFFFF;
	const uint hi = ARead[addr2](addr2);
	return QString("{\"ok\":true,\"source\":\"bus\",\"addr\":%1,\"value\":%2}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(lo | (hi << 8));
}

QString CpuPokeResponse(uint addr, uint value, bool wide)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	BWrite[addr](addr, static_cast<uint8>(value & 0xFF));
	if (wide)
	{
		const uint addr2 = (addr + 1) & 0xFFFF;
		BWrite[addr2](addr2, static_cast<uint8>((value >> 8) & 0xFF));
	}
	return QStringLiteral("{\"ok\":true}");
}

QString MemLoadResponse(uint addr, const QByteArray& bytes)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	for (int i = 0; i < bytes.size(); i++)
	{
		const uint cur = addr + static_cast<uint>(i);
		BWrite[cur](cur, static_cast<uint8>(bytes[i]));
	}
	return QString("{\"ok\":true,\"addr\":%1,\"len\":%2}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(bytes.size());
}

bool ParseBytePattern(const QString& text, QByteArray* pattern, QByteArray* mask, QString* error)
{
	QString payload = text.trimmed();
	if (payload.startsWith(QStringLiteral("hex:"), Qt::CaseInsensitive))
	{
		payload = payload.mid(4);
	}
	else if (payload.startsWith(QStringLiteral("base64:"), Qt::CaseInsensitive))
	{
		const QByteArray decoded = QByteArray::fromBase64(payload.mid(7).toLatin1());
		if (decoded.isEmpty())
		{
			*error = QStringLiteral("empty or invalid base64 pattern");
			return false;
		}
		*pattern = decoded;
		*mask = QByteArray(decoded.size(), 1);
		return true;
	}

	payload.remove(' ');
	payload.remove('\t');
	if (payload.isEmpty() || (payload.size() % 2) != 0)
	{
		*error = QStringLiteral("hex pattern must contain an even number of digits");
		return false;
	}
	QByteArray values;
	QByteArray exactMask;
	values.reserve(payload.size() / 2);
	exactMask.reserve(payload.size() / 2);
	for (int i = 0; i < payload.size(); i += 2)
	{
		const QChar high = payload[i];
		const QChar low = payload[i + 1];
		if (high == '?' && low == '?')
		{
			values.append(static_cast<char>(0));
			exactMask.append(static_cast<char>(0));
			continue;
		}
		for (QChar c : {high, low})
		{
			if (!c.isDigit() && (c.toLower() < 'a' || c.toLower() > 'f'))
			{
				*error = QStringLiteral("hex pattern contains a non-hex digit or partial wildcard");
				return false;
			}
		}
		values.append(QByteArray::fromHex(QString(payload.mid(i, 2)).toLatin1()));
		exactMask.append(static_cast<char>(1));
	}
	*pattern = values;
	*mask = exactMask;
	if (pattern->isEmpty())
	{
		*error = QStringLiteral("empty pattern");
		return false;
	}
	return true;
}

QString MemSearchResponse(const QString& domain, const QByteArray& pattern, const QByteArray& patternMask, uint start, uint end, bool endProvided, uint limit)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	if (domain == QStringLiteral("rom"))
	{
		if (PRGptr[0] == nullptr || PRGsize[0] == 0)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"no PRG ROM available\"}");
		}
		if (!endProvided)
		{
			end = PRGsize[0] - 1;
		}
		if (start >= PRGsize[0] || end >= PRGsize[0] || start > end)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid ROM search range\"}");
		}
	}
	else if (domain == QStringLiteral("ppu"))
	{
		if (start > 0x3FFF || end > 0x3FFF || start > end)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid PPU search range\"}");
		}
	}
	else if (start > 0xFFFF || end > 0xFFFF || start > end)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU search range\"}");
	}
	if (static_cast<uint>(pattern.size()) > (end - start + 1))
	{
		return QString("{\"ok\":true,\"domain\":%1,\"pattern_len\":%2,\"matches\":[],\"truncated\":false}")
			.arg(JsonString(domain))
			.arg(pattern.size());
	}

	QStringList matches;
	bool truncated = false;
	const uint last = end - static_cast<uint>(pattern.size()) + 1;
	for (uint addr = start; addr <= last; addr++)
	{
		bool hit = true;
		for (int i = 0; i < pattern.size(); i++)
		{
			if (!patternMask.at(i))
			{
				continue;
			}
			const uint8 expected = static_cast<uint8>(pattern.at(i));
			uint8 actual = 0;
			if (domain == QStringLiteral("cpu"))
			{
				actual = GetMem(static_cast<uint16>((addr + static_cast<uint>(i)) & 0xFFFF));
			}
			else if (domain == QStringLiteral("ppu"))
			{
				actual = PpuRead((addr + static_cast<uint>(i)) & 0x3FFF);
			}
			else
			{
				actual = PRGptr[0][addr + static_cast<uint>(i)];
			}
			if (actual != expected)
			{
				hit = false;
				break;
			}
		}
		if (hit)
		{
			matches.append(JsonString(hexValue(addr, domain == QStringLiteral("rom") ? 6 : 4)));
			if (static_cast<uint>(matches.size()) >= limit)
			{
				truncated = addr < last;
				break;
			}
		}
		if (addr == UINT_MAX)
		{
			break;
		}
	}
	return QString("{\"ok\":true,\"domain\":%1,\"start\":%2,\"end\":%3,\"pattern_len\":%4,\"matches\":[%5],\"truncated\":%6}")
		.arg(JsonString(domain))
		.arg(JsonString(hexValue(start, domain == QStringLiteral("rom") ? 6 : 4)))
		.arg(JsonString(hexValue(end, domain == QStringLiteral("rom") ? 6 : 4)))
		.arg(pattern.size())
		.arg(matches.join(','))
		.arg(truncated ? "true" : "false");
}

QString MemSearchCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 6)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"MEMSEARCH requires pattern [domain=cpu|ppu|rom] [start=ADDR] [end=ADDR] [limit=N]\"}");
	}
	QByteArray pattern;
	QByteArray patternMask;
	QString patternError;
	if (!ParseBytePattern(parts[1], &pattern, &patternMask, &patternError))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(patternError));
	}
	QString domain = OptionValue(parts, QStringLiteral("domain"));
	if (domain.isEmpty())
	{
		domain = QStringLiteral("cpu");
	}
	domain = domain.toLower();
	if (domain != QStringLiteral("cpu") && domain != QStringLiteral("ppu") && domain != QStringLiteral("rom"))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"MEMSEARCH domain must be cpu, ppu, or rom\"}");
	}
	uint start = 0;
	uint end = (domain == QStringLiteral("ppu")) ? 0x3FFF : (domain == QStringLiteral("rom") ? 0 : 0xFFFF);
	uint limit = 256;
	const QString startText = OptionValue(parts, QStringLiteral("start"));
	if (!startText.isEmpty() && !ParseNumber(startText, &start))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid MEMSEARCH start\"}");
	}
	const QString endText = OptionValue(parts, QStringLiteral("end"));
	if (!endText.isEmpty() && !ParseNumber(endText, &end))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid MEMSEARCH end\"}");
	}
	const QString limitText = OptionValue(parts, QStringLiteral("limit"));
	if (!limitText.isEmpty() && (!ParseNumber(limitText, &limit) || limit == 0 || limit > 4096))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid MEMSEARCH limit\"}");
	}
	return MemSearchResponse(domain, pattern, patternMask, start, end, !endText.isEmpty(), limit);
}

QString CpuPeekCommandResponse(const QString& cmd, const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("%1 requires addr [len]").arg(cmd)));
	}
	uint addr = 0;
	uint len = (cmd == QStringLiteral("PEEK")) ? 1 : 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	if (parts.size() == 3 && (!ParseNumber(parts[2], &len) || len == 0 || len > 65536))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid length\"}");
	}
	if (cmd == QStringLiteral("MEMDUMP") && len == 0)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"MEMDUMP requires length\"}");
	}
	if (addr + len > 0x10000)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"range exceeds CPU address space\"}");
	}
	return CpuPeekResponse(addr, len, cmd == QStringLiteral("PEEK") && len == 1);
}

QString CpuPeek16CommandResponse(const QStringList& parts)
{
	if (parts.size() != 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PEEK16 requires addr\"}");
	}
	uint addr = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	return CpuPeek16Response(addr);
}

QString BusPeekCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"BUSPEEK requires addr [len]\"}");
	}
	uint addr = 0;
	uint len = 1;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	if (parts.size() == 3 && (!ParseNumber(parts[2], &len) || len == 0 || len > 65536))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid BUSPEEK length\"}");
	}
	if (addr + len > 0x10000)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"range exceeds CPU address space\"}");
	}
	return BusPeekResponse(addr, len);
}

QString BusPeek16CommandResponse(const QStringList& parts)
{
	if (parts.size() != 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"BUSPEEK16 requires addr\"}");
	}
	uint addr = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	return BusPeek16Response(addr);
}

QString CpuPokeCommandResponse(const QString& cmd, const QStringList& parts)
{
	if (parts.size() != 3)
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("%1 requires addr value").arg(cmd)));
	}
	uint addr = 0;
	uint value = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF || !ParseNumber(parts[2], &value))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid address or value\"}");
	}
	const bool wide = (cmd == QStringLiteral("POKE16"));
	if ((!wide && value > 0xFF) || (wide && value > 0xFFFF))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"value out of range\"}");
	}
	return CpuPokeResponse(addr, value, wide);
}

QString MemLoadCommandResponse(const QStringList& parts)
{
	if (parts.size() != 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"MEMLOAD requires addr base64\"}");
	}
	uint addr = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	QString payload = parts[2];
	if (payload.startsWith(QStringLiteral("base64:"), Qt::CaseInsensitive))
	{
		payload = payload.mid(7);
	}
	const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(payload.toLatin1());
	if (!decoded)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid base64 payload\"}");
	}
	const QByteArray bytes = decoded.decoded;
	if (addr + static_cast<uint>(bytes.size()) > 0x10000)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"payload range exceeds CPU address space\"}");
	}
	return MemLoadResponse(addr, bytes);
}

}
