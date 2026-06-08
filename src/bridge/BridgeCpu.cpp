#include "bridge/BridgeCpu.h"

#include <QByteArray>
#include <QStringList>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "asm.h"
#include "debug.h"
#include "fceu.h"
#include "movie.h"
#include "x6502.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QString flagsString(uint8 p)
{
	QString flags;
	flags.reserve(8);
	flags += (p & N_FLAG) ? 'N' : '-';
	flags += (p & V_FLAG) ? 'V' : '-';
	flags += (p & U_FLAG) ? 'U' : '-';
	flags += (p & B_FLAG) ? 'B' : '-';
	flags += (p & D_FLAG) ? 'D' : '-';
	flags += (p & I_FLAG) ? 'I' : '-';
	flags += (p & Z_FLAG) ? 'Z' : '-';
	flags += (p & C_FLAG) ? 'C' : '-';
	return flags;
}

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

bool parsePositiveInt(const QString& text, int* value)
{
	bool ok = false;
	const int parsed = text.toInt(&ok, 10);
	if (!ok || parsed <= 0)
	{
		return false;
	}
	*value = parsed;
	return true;
}

QString regsJson()
{
	const uint64 cycleCount = timestampbase + static_cast<uint64>(timestamp) - total_cycles_base;
	return QString("{\"ok\":true,\"PC\":%1,\"A\":%2,\"X\":%3,\"Y\":%4,\"S\":%5,\"P\":%6,\"flags\":%7,\"frame\":%8,\"cycles\":%9,\"instructions\":%10,\"paused\":%11}")
		.arg(JsonString(hexValue(X.PC, 4)))
		.arg(JsonString(hexValue(X.A, 2)))
		.arg(JsonString(hexValue(X.X, 2)))
		.arg(JsonString(hexValue(X.Y, 2)))
		.arg(JsonString(hexValue(X.S, 2)))
		.arg(JsonString(hexValue(X.P, 2)))
		.arg(JsonString(flagsString(X.P)))
		.arg(currFrameCounter)
		.arg(QString::number(static_cast<qulonglong>(cycleCount)))
		.arg(QString::number(static_cast<qulonglong>(total_instructions)))
		.arg(FCEUI_EmulationPaused() ? "true" : "false");
}

QString callStackJson(int count)
{
	QStringList frames;
	frames.reserve(count);

	const uint stackPointer = X.S;
	const uint stackStart = 0x0100 + ((stackPointer + 1) & 0xFF);
	for (uint offset = stackPointer + 1; offset < 0xFF && frames.size() < count; offset += 2)
	{
		const uint lowAddr = 0x0100 + (offset & 0xFF);
		const uint highAddr = 0x0100 + ((offset + 1) & 0xFF);
		const uint low = GetMem(static_cast<uint16>(lowAddr));
		const uint high = GetMem(static_cast<uint16>(highAddr));
		const uint returnAddress = (high << 8) | low;
		const uint resumePc = (returnAddress + 1) & 0xFFFF;
		const uint8 opcode[3] = {
			GetMem(static_cast<uint16>(resumePc)),
			GetMem(static_cast<uint16>((resumePc + 1) & 0xFFFF)),
			GetMem(static_cast<uint16>((resumePc + 2) & 0xFFFF))
		};
		frames.append(QString("{\"index\":%1,\"stack_low\":%2,\"stack_high\":%3,\"return_addr\":%4,\"resume_pc\":%5,\"resume_disasm\":%6}")
			.arg(frames.size())
			.arg(JsonString(hexValue(lowAddr, 4)))
			.arg(JsonString(hexValue(highAddr, 4)))
			.arg(JsonString(hexValue(returnAddress, 4)))
			.arg(JsonString(hexValue(resumePc, 4)))
			.arg(JsonString(QString::fromLocal8Bit(Disassemble(static_cast<int>(resumePc), const_cast<uint8*>(opcode))))));
	}

	return QString("{\"ok\":true,\"source\":\"6502-stack-scan\",\"current_pc\":%1,\"stack_pointer\":%2,\"stack_start\":%3,\"frames\":[%4]}")
		.arg(JsonString(hexValue(X.PC, 4)))
		.arg(JsonString(hexValue(stackPointer, 2)))
		.arg(JsonString(hexValue(stackStart, 4)))
		.arg(frames.join(','));
}

}

QString CpuRegsResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	return regsJson();
}

QString CpuRegSetResponse(const QString& reg, uint value)
{
	if (reg == QStringLiteral("PC"))
	{
		if (value > 0xFFFF)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"PC value out of range\"}");
		}
		FCEU_CRITICAL_SECTION(lock);
		X.PC = static_cast<uint16>(value);
	}
	else if (reg == QStringLiteral("A") || reg == QStringLiteral("X") || reg == QStringLiteral("Y") || reg == QStringLiteral("S") || reg == QStringLiteral("P"))
	{
		if (value > 0xFF)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"8-bit register value out of range\"}");
		}
		FCEU_CRITICAL_SECTION(lock);
		if (reg == QStringLiteral("A"))
		{
			X.A = static_cast<uint8>(value);
		}
		else if (reg == QStringLiteral("X"))
		{
			X.X = static_cast<uint8>(value);
		}
		else if (reg == QStringLiteral("Y"))
		{
			X.Y = static_cast<uint8>(value);
		}
		else if (reg == QStringLiteral("S"))
		{
			X.S = static_cast<uint8>(value);
		}
		else
		{
			X.P = static_cast<uint8>(value);
		}
	}
	else
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"unsupported register\"}");
	}

	FCEU_CRITICAL_SECTION(lock);
	const uint64 cycleCount = timestampbase + static_cast<uint64>(timestamp) - total_cycles_base;
	return QString("{\"ok\":true,\"register\":%1,\"value\":%2,\"PC\":%3,\"A\":%4,\"X\":%5,\"Y\":%6,\"S\":%7,\"P\":%8,\"flags\":%9,\"frame\":%10,\"cycles\":%11,\"instructions\":%12,\"paused\":%13}")
		.arg(JsonString(reg))
		.arg(JsonString(reg == QStringLiteral("PC") ? hexValue(value, 4) : hexValue(value, 2)))
		.arg(JsonString(hexValue(X.PC, 4)))
		.arg(JsonString(hexValue(X.A, 2)))
		.arg(JsonString(hexValue(X.X, 2)))
		.arg(JsonString(hexValue(X.Y, 2)))
		.arg(JsonString(hexValue(X.S, 2)))
		.arg(JsonString(hexValue(X.P, 2)))
		.arg(JsonString(flagsString(X.P)))
		.arg(currFrameCounter)
		.arg(QString::number(static_cast<qulonglong>(cycleCount)))
		.arg(QString::number(static_cast<qulonglong>(total_instructions)))
		.arg(FCEUI_EmulationPaused() ? "true" : "false");
}

QString CpuDisasmResponse(uint addr, int count)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QStringList rows;
	rows.reserve(count);
	uint cur = addr;
	for (int i = 0; i < count; i++)
	{
		uint8 opcode[3] = {
			GetMem(static_cast<uint16>(cur)),
			GetMem(static_cast<uint16>((cur + 1) & 0xFFFF)),
			GetMem(static_cast<uint16>((cur + 2) & 0xFFFF))
		};
		int size = opsize[opcode[0]];
		if (size <= 0)
		{
			size = 1;
		}
		if (size > 3)
		{
			size = 3;
		}
		QByteArray bytes;
		for (int j = 0; j < size; j++)
		{
			bytes.append(static_cast<char>(opcode[j]));
		}
		rows.append(QString("{\"addr\":%1,\"size\":%2,\"bytes\":%3,\"text\":%4}")
			.arg(JsonString(hexValue(cur, 4)))
			.arg(size)
			.arg(jsonByteArray(bytes))
			.arg(JsonString(QString::fromLocal8Bit(Disassemble(static_cast<int>(cur), opcode)))));
		cur = (cur + static_cast<uint>(size)) & 0xFFFF;
	}
	return QString("{\"ok\":true,\"addr\":%1,\"count\":%2,\"instructions\":[%3]}")
		.arg(JsonString(hexValue(addr, 4)))
		.arg(count)
		.arg(rows.join(','));
}

QString CpuCallStackResponse(int count)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	return callStackJson(count);
}

QString CpuRegSetCommandResponse(const QStringList& parts)
{
	if (parts.size() != 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"REG_SET requires register and value\"}");
	}
	const QString reg = parts[1].toUpper();
	uint value = 0;
	if (!ParseNumber(parts[2], &value))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid register value\"}");
	}
	return CpuRegSetResponse(reg, value);
}

QString CpuDisasmCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2 || parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"DISASM requires addr [count]\"}");
	}
	uint addr = 0;
	if (!ParseNumber(parts[1], &addr) || addr > 0xFFFF)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid CPU address\"}");
	}
	int count = 16;
	if (parts.size() == 3 && !parsePositiveInt(parts[2], &count))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"DISASM count must be a positive integer\"}");
	}
	if (count > 1024)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"DISASM count too large\"}");
	}
	return CpuDisasmResponse(addr, count);
}

QString CpuCallStackCommandResponse(const QStringList& parts)
{
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CALLSTACK accepts optional count\"}");
	}
	int count = 16;
	if (parts.size() == 2 && !parsePositiveInt(parts[1], &count))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"CALLSTACK count must be a positive integer\"}");
	}
	if (count > 64)
	{
		count = 64;
	}
	return CpuCallStackResponse(count);
}

}
