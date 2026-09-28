#include "bridge/BridgePpu.h"

#include <QByteArray>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "driver.h"
#include "debug.h"
#include "fceu.h"
#include "movie.h"
#include "ppu.h"
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

uint8 ppuDirectRead(uint address)
{
	const uint addr = address & 0x3FFF;
	if (addr < 0x2000)
	{
		return VPage[addr >> 10][addr];
	}
	if (GameInfo != nullptr && GameInfo->type == GIT_NSF)
	{
		return 0;
	}
	if (addr < 0x3F00)
	{
		return vnapage[(addr >> 10) & 0x03][addr & 0x03FF];
	}
	return READPAL_MOTHEROFALL(addr & 0x1F);
}

void ppuWrite(uint address, uint8 value)
{
	if (FFCEUX_PPUWrite != nullptr)
	{
		FFCEUX_PPUWrite(address & 0x3FFF, value);
		return;
	}
	FFCEUX_PPUWrite_Default(address & 0x3FFF, value);
}

}

uint8 PpuRead(uint address)
{
	if (FFCEUX_PPURead != nullptr)
	{
		return FFCEUX_PPURead(address & 0x3FFF);
	}
	return FFCEUX_PPURead_Default(address & 0x3FFF);
}

QString PpuStateResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	int scrollX = 0;
	int scrollY = 0;
	ppu_getScroll(scrollX, scrollY);
	return QString("{\"ok\":true,\"frame\":%1,\"newppu\":%2,\"scanline\":%3,\"dot\":%4,\"legacy_scanline\":%5,\"rasterpos\":%6,\"ppuaddr\":%7,\"refresh_addr\":%8,\"temp_addr\":%9,\"x_offset\":%10,\"vtoggle\":%11,\"vram_buffer\":%12,\"gen_latch\":%13,\"oam_addr\":%14,\"oam_latch\":%15,\"sprite_dma\":%16,\"ctrl\":%17,\"mask\":%18,\"status\":%19,\"scroll_x\":%20,\"scroll_y\":%21,\"rendering\":%22,\"sprite_size_16\":%23,\"nmi_enabled\":%24}")
		.arg(currFrameCounter)
		.arg(newppu ? "true" : "false")
		.arg(newppu ? newppu_get_scanline() : scanline)
		.arg(newppu ? newppu_get_dot() : -1)
		.arg(scanline)
		.arg(g_rasterpos)
		.arg(JsonString(hexValue(FCEUPPU_PeekAddress(), 4)))
		.arg(JsonString(hexValue(RefreshAddr & 0x3FFF, 4)))
		.arg(JsonString(hexValue(TempAddr & 0x3FFF, 4)))
		.arg(static_cast<uint>(XOffset))
		.arg(static_cast<uint>(vtoggle))
		.arg(JsonString(hexValue(VRAMBuffer, 2)))
		.arg(JsonString(hexValue(PPUGenLatch, 2)))
		.arg(JsonString(hexValue(PPU[3], 2)))
		.arg(JsonString(hexValue(PPUSPL, 2)))
		.arg(JsonString(hexValue(SpriteDMA, 2)))
		.arg(JsonString(hexValue(PPU[0], 2)))
		.arg(JsonString(hexValue(PPU[1], 2)))
		.arg(JsonString(hexValue(PPU[2], 2)))
		.arg(scrollX)
		.arg(scrollY)
		.arg((PPU[1] & 0x18) ? "true" : "false")
		.arg((PPU[0] & 0x20) ? "true" : "false")
		.arg((PPU[0] & 0x80) ? "true" : "false");
}

QString PpuPeekResponse(uint addr, uint len)
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
		bytes.append(static_cast<char>(PpuRead((addr + i) & 0x3FFF)));
	}
	if (len == 1)
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

QString PpuPokeResponse(uint addr, uint value)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	ppuWrite(addr, static_cast<uint8>(value));
	return QStringLiteral("{\"ok\":true}");
}

QString PpuDumpResponse(const QString& domain, uint start, uint len)
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
		bytes.append(static_cast<char>(ppuDirectRead(start + i)));
	}
	return QString("{\"ok\":true,\"domain\":%1,\"source\":\"direct\",\"addr\":%2,\"len\":%3,\"data\":%4,\"base64\":%5}")
		.arg(JsonString(domain))
		.arg(JsonString(hexValue(start, 4)))
		.arg(len)
		.arg(jsonByteArray(bytes))
		.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
}

QString OamDumpResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	const QByteArray bytes(reinterpret_cast<const char*>(SPRAM), 0x100);
	return QString("{\"ok\":true,\"len\":256,\"data\":%1,\"base64\":%2}")
		.arg(JsonRawBytes(SPRAM, 0x100))
		.arg(JsonString(QString::fromLatin1(bytes.toBase64())));
}

QString OamPokeResponse(uint addr, uint value)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	SPRAM[addr] = static_cast<uint8>(value);
	return QStringLiteral("{\"ok\":true}");
}

QString PaletteDumpResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	const QByteArray palBytes(reinterpret_cast<const char*>(PALRAM), 0x20);
	const QByteArray upalBytes(reinterpret_cast<const char*>(UPALRAM), 0x03);
	return QString("{\"ok\":true,\"palram\":%1,\"upalram\":%2,\"palram_base64\":%3,\"upalram_base64\":%4}")
		.arg(JsonRawBytes(PALRAM, 0x20))
		.arg(JsonRawBytes(UPALRAM, 0x03))
		.arg(JsonString(QString::fromLatin1(palBytes.toBase64())))
		.arg(JsonString(QString::fromLatin1(upalBytes.toBase64())));
}

QString PpuStateCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PPU_STATE takes no arguments\"}");
	}
	return PpuStateResponse();
}

QString PpuPeekCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PPU_PEEK requires addr [len]\"}");
	}
	uint addr = 0;
	uint len = 1;
	if (!ParseNumber(parts[1], &addr) || addr > 0x3FFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid PPU address\"}");
	}
	if (parts.size() == 3 && (!ParseNumber(parts[2], &len) || len == 0 || len > 0x4000))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid PPU length\"}");
	}
	return PpuPeekResponse(addr, len);
}

QString PpuPokeCommandResponse(const QStringList& parts)
{
	if (parts.size() != 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PPU_POKE requires addr value\"}");
	}
	uint addr = 0;
	uint value = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0x3FFF || !ParseNumber(parts[2], &value) || value > 0xFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid PPU address or value\"}");
	}
	return PpuPokeResponse(addr, value);
}

QString PpuDumpCommandResponse(const QStringList& parts)
{
	if (parts.size() > 4)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PPU_DUMP accepts domain=pattern|nametable|palette|all [start=ADDR] [len=N]\"}");
	}
	QString domain = OptionValue(parts, QStringLiteral("domain"));
	if (domain.isEmpty())
	{
		domain = parts.size() >= 2 && !parts[1].contains('=') ? parts[1] : QStringLiteral("all");
	}
	domain = domain.toLower();
	if (domain != QStringLiteral("pattern") && domain != QStringLiteral("nametable") && domain != QStringLiteral("palette") && domain != QStringLiteral("all"))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PPU_DUMP domain must be pattern, nametable, palette, or all\"}");
	}
	uint start = 0;
	uint len = 0;
	uint base = 0;
	uint maxLen = 0x4000;
	if (domain == QStringLiteral("pattern"))
	{
		base = 0x0000;
		maxLen = 0x2000;
	}
	else if (domain == QStringLiteral("nametable"))
	{
		base = 0x2000;
		maxLen = 0x1000;
	}
	else if (domain == QStringLiteral("palette"))
	{
		base = 0x3F00;
		maxLen = 0x20;
	}
	const QString startText = OptionValue(parts, QStringLiteral("start"));
	if (!startText.isEmpty() && !ParseNumber(startText, &start))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid PPU_DUMP start\"}");
	}
	const QString lenText = OptionValue(parts, QStringLiteral("len"));
	if (!lenText.isEmpty() && (!ParseNumber(lenText, &len) || len == 0))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid PPU_DUMP length\"}");
	}
	if (domain != QStringLiteral("all"))
	{
		if (startText.isEmpty())
		{
			start = base;
		}
		if (lenText.isEmpty())
		{
			len = maxLen;
		}
		if (start < base || start >= base + maxLen || len > maxLen || start + len > base + maxLen)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"PPU_DUMP range outside selected domain\"}");
		}
	}
	else
	{
		if (startText.isEmpty())
		{
			start = 0;
		}
		if (lenText.isEmpty())
		{
			len = 0x4000;
		}
		if (start > 0x3FFF || len > 0x4000 || start + len > 0x4000)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"PPU_DUMP range outside PPU address space\"}");
		}
	}
	return PpuDumpResponse(domain, start, len);
}

QString OamDumpCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"OAM_DUMP takes no arguments\"}");
	}
	return OamDumpResponse();
}

QString OamPokeCommandResponse(const QStringList& parts)
{
	if (parts.size() != 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"OAM_POKE requires addr value\"}");
	}
	uint addr = 0;
	uint value = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFF || !ParseNumber(parts[2], &value) || value > 0xFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid OAM address or value\"}");
	}
	return OamPokeResponse(addr, value);
}

QString PaletteDumpCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"PALETTE_DUMP takes no arguments\"}");
	}
	return PaletteDumpResponse();
}

}
