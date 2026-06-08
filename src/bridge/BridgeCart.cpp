#include "bridge/BridgeCart.h"

#include <QByteArray>
#include <QStringList>
#include <algorithm>
#include <cstddef>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "x6502.h"
#include "debug.h"
#include "driver.h"
#include "fceu.h"
#include "cart.h"
#include "ppu.h"
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

QString gameTypeName(EGIT type)
{
	switch (type)
	{
		case GIT_CART: return QStringLiteral("cart");
		case GIT_VSUNI: return QStringLiteral("vsuni");
		case GIT_FDS: return QStringLiteral("fds");
		case GIT_NSF: return QStringLiteral("nsf");
		default: return QStringLiteral("unknown");
	}
}

QString videoSystemName(EGIV vidsys)
{
	switch (vidsys)
	{
		case GIV_NTSC: return QStringLiteral("ntsc");
		case GIV_PAL: return QStringLiteral("pal");
		case GIV_USER: return QStringLiteral("user");
		default: return QStringLiteral("unknown");
	}
}

QString sizeArrayJson(uint32* sizes)
{
	QStringList rows;
	for (int i = 0; i < 32; i++)
	{
		if (sizes[i] != 0)
		{
			rows.append(QString("{\"chip\":%1,\"size\":%2}").arg(i).arg(sizes[i]));
		}
	}
	return QString("[%1]").arg(rows.join(','));
}

QString md5Json()
{
	if (GameInfo == nullptr)
	{
		return QStringLiteral("null");
	}
	MD5DATA md5 = GameInfo->MD5;
	return JsonString(QString::fromLatin1(md5_asciistr(md5)));
}

QString cpuMapRowJson(uint addr)
{
	const int prgOffset = GetPRGAddress(static_cast<int>(addr));
	const int nesFileOffset = GetNesFileAddress(static_cast<int>(addr));
	const int bank = getBank(static_cast<int>(addr));
	return QString("{\"start\":%1,\"end\":%2,\"bank\":%3,\"prg_offset\":%4,\"nes_file_offset\":%5}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(JsonString(hexValue(std::min<uint>(addr + 0x1FFF, 0xFFFF), 4)))
		.arg(bank)
		.arg(prgOffset >= 0 ? JsonString(hexValue(static_cast<uint>(prgOffset), 6)) : QStringLiteral("null"))
		.arg(nesFileOffset >= 0 ? JsonString(hexValue(static_cast<uint>(nesFileOffset), 6)) : QStringLiteral("null"));
}

QString ppuMapRowJson(uint addr)
{
	QString chrOffset = QStringLiteral("null");
	if (CHRptr[0] != nullptr)
	{
		uint8* ptr = &VPage[(addr & 0x1FFF) >> 10][addr & 0x1FFF];
		const ptrdiff_t offset = ptr - CHRptr[0];
		if (offset >= 0 && static_cast<uint32>(offset) < CHRsize[0])
		{
			chrOffset = JsonString(hexValue(static_cast<uint>(offset), 6));
		}
	}
	return QString("{\"start\":%1,\"end\":%2,\"chr_offset\":%3}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(JsonString(hexValue(std::min<uint>(addr + 0x03FF, 0x1FFF), 4)))
		.arg(chrOffset);
}

}

QString CartInfoResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded() || GameInfo == nullptr)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QString saveSizes = QStringLiteral("[]");
	if (currCartInfo != nullptr)
	{
		QStringList rows;
		for (size_t i = 0; i < currCartInfo->SaveGame.size(); i++)
		{
			rows.append(QString("{\"index\":%1,\"size\":%2,\"present\":%3}")
				.arg(static_cast<uint>(i))
				.arg(currCartInfo->SaveGame[i].buflen)
				.arg(currCartInfo->SaveGame[i].bufptr != nullptr ? "true" : "false"));
		}
		saveSizes = QString("[%1]").arg(rows.join(','));
	}
	return QString("{\"ok\":true,\"name\":%1,\"filename\":%2,\"archive\":%3,\"type\":%4,\"mapper\":%5,\"vidsys\":%6,\"md5\":%7,\"crc32\":%8,\"ines2\":%9,\"submapper\":%10,\"battery\":%11,\"mirror\":%12,\"wram_size\":%13,\"battery_wram_size\":%14,\"vram_size\":%15,\"battery_vram_size\":%16,\"prg\":%17,\"chr\":%18,\"save_game\":%19}")
		.arg(GameInfo->name ? JsonString(QString::fromUtf8(reinterpret_cast<const char*>(GameInfo->name))) : QStringLiteral("null"))
		.arg(GameInfo->filename ? JsonString(QString::fromLocal8Bit(GameInfo->filename)) : QStringLiteral("null"))
		.arg(GameInfo->archiveFilename ? JsonString(QString::fromLocal8Bit(GameInfo->archiveFilename)) : QStringLiteral("null"))
		.arg(JsonString(gameTypeName(GameInfo->type)))
		.arg(GameInfo->mappernum)
		.arg(JsonString(videoSystemName(GameInfo->vidsys)))
		.arg(md5Json())
		.arg(currCartInfo != nullptr ? JsonString(hexValue(currCartInfo->CRC32, 8)) : QStringLiteral("null"))
		.arg(currCartInfo != nullptr ? (currCartInfo->ines2 ? "true" : "false") : "false")
		.arg(currCartInfo != nullptr ? currCartInfo->submapper : 0)
		.arg(currCartInfo != nullptr ? (currCartInfo->battery ? "true" : "false") : "false")
		.arg(currCartInfo != nullptr ? currCartInfo->mirror : 0)
		.arg(currCartInfo != nullptr ? currCartInfo->wram_size : 0)
		.arg(currCartInfo != nullptr ? currCartInfo->battery_wram_size : 0)
		.arg(currCartInfo != nullptr ? currCartInfo->vram_size : 0)
		.arg(currCartInfo != nullptr ? currCartInfo->battery_vram_size : 0)
		.arg(sizeArrayJson(PRGsize))
		.arg(sizeArrayJson(CHRsize))
		.arg(saveSizes);
}

QString BankInfoResponse(bool singleAddress, uint addr)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded() || GameInfo == nullptr)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	if (singleAddress)
	{
		return QString("{\"ok\":true,\"address\":%1,\"mapping\":%2}")
			.arg(JsonString(hexValue(addr, 4)))
			.arg(cpuMapRowJson(addr));
	}
	QStringList rows;
	for (uint rowAddr = 0x6000; rowAddr <= 0xE000; rowAddr += 0x2000)
	{
		rows.append(cpuMapRowJson(rowAddr));
	}
	return QString("{\"ok\":true,\"cpu\":[%1],\"debugger_page_size\":%2}")
		.arg(rows.join(','))
		.arg(debuggerPageSize);
}

QString MemMapResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded() || GameInfo == nullptr)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QStringList cpuRows;
	for (uint addr = 0; addr <= 0xE000; addr += 0x2000)
	{
		cpuRows.append(cpuMapRowJson(addr));
	}
	QStringList ppuRows;
	for (uint addr = 0; addr <= 0x1C00; addr += 0x0400)
	{
		ppuRows.append(ppuMapRowJson(addr));
	}
	return QString("{\"ok\":true,\"cpu\":[%1],\"ppu\":[%2]}")
		.arg(cpuRows.join(','))
		.arg(ppuRows.join(','));
}

QString RomBytesResponse(const QString& domain, uint offset, uint len)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded() || GameInfo == nullptr)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	uint8* base = (domain == QStringLiteral("prg")) ? PRGptr[0] : CHRptr[0];
	const uint size = (domain == QStringLiteral("prg")) ? PRGsize[0] : CHRsize[0];
	if (base == nullptr || size == 0)
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString("no %1 ROM available").arg(domain.toUpper())));
	}
	if (len == 0)
	{
		len = size;
	}
	if (offset >= size || len > size || offset + len > size)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"ROM range out of bounds\"}");
	}
	if (len > 1048576)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"ROM response too large; use ROM_PEEK with a smaller range\"}");
	}
	const QByteArray bytes(reinterpret_cast<const char*>(base + offset), static_cast<int>(len));
	return QString("{\"ok\":true,\"domain\":%1,\"offset\":%2,\"len\":%3,\"size\":%4,\"bytes\":%5,\"base64\":%6}")
		.arg(JsonString(domain))
		.arg(JsonString(hexValue(offset, 6)))
		.arg(len)
		.arg(size)
		.arg(jsonByteArray(bytes))
		.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
}

QString CartInfoCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CART_INFO takes no arguments\"}");
	}
	return CartInfoResponse();
}

QString BankInfoCommandResponse(const QStringList& parts)
{
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"BANK_INFO accepts optional CPU address\"}");
	}
	if (parts.size() == 2)
	{
		uint addr = 0;
		if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
		}
		return BankInfoResponse(true, addr);
	}
	return BankInfoResponse(false, 0);
}

QString MemMapCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"MEMMAP takes no arguments\"}");
	}
	return MemMapResponse();
}

QString RomBytesCommandResponse(const QString& cmd, const QStringList& parts)
{
	const bool peek = cmd == QStringLiteral("ROM_PEEK");
	if ((peek && (parts.size() < 3 || parts.size() > 4)) || (!peek && parts.size() > 2))
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(peek ? QStringLiteral("ROM_PEEK requires offset len [domain=prg|chr]") : QStringLiteral("ROM_DUMP accepts optional domain=prg|chr")));
	}
	QString domain = OptionValue(parts, QStringLiteral("domain"));
	if (domain.isEmpty())
	{
		domain = (!peek && parts.size() == 2) ? parts[1] : QStringLiteral("prg");
	}
	domain = domain.toLower();
	if (domain != QStringLiteral("prg") && domain != QStringLiteral("chr"))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"ROM domain must be prg or chr\"}");
	}
	uint offset = 0;
	uint len = 0;
	if (peek)
	{
		if (!ParseNumber(parts[1], &offset) || !ParseNumber(parts[2], &len) || len == 0)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid ROM_PEEK offset or length\"}");
		}
	}
	return RomBytesResponse(domain, offset, len);
}

}
