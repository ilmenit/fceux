#include "bridge/BridgeHistory.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include <QStringList>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "types.h"
#include "asm.h"
#include "debug.h"
#include "fceu.h"
#include "movie.h"
#include "ppu.h"
#include "x6502.h"

namespace FCEUXBridge
{

namespace
{

constexpr size_t kDefaultHistoryCapacity = 4096;
constexpr size_t kMaxHistoryCapacity = 131072;

struct HistoryEntry
{
	uint64 sequence = 0;
	uint64 instruction = 0;
	uint64 cycle = 0;
	int frame = 0;
	int scanline = 0;
	int dot = -1;
	uint16 pc = 0;
	int bank = 0;
	int prgOffset = -1;
	int effectiveAddress = -1;
	uint8 accessFlags = 0;
	int preWriteValue = -1;
	uint8 a = 0;
	uint8 x = 0;
	uint8 y = 0;
	uint8 s = 0;
	uint8 p = 0;
	uint8 size = 0;
	std::array<uint8, 3> opcode = {0, 0, 0};
	char disasm[64] = {0};
};

std::mutex g_historyMutex;
std::vector<HistoryEntry> g_history(kDefaultHistoryCapacity);
size_t g_historyStart = 0;
size_t g_historyCount = 0;
uint64 g_historySequence = 0;
void* g_traceHandle = nullptr;
std::atomic_bool g_historyEnabled(true);

QString jsonEscape(const QString& value)
{
	QString out;
	out.reserve(value.size() + 8);
	for (QChar c : value)
	{
		switch (c.unicode())
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c.unicode() < 0x20)
				{
					out += QString("\\u%1").arg(c.unicode(), 4, 16, QLatin1Char('0'));
				}
				else
				{
					out += c;
				}
				break;
		}
	}
	return out;
}

QString jsonString(const QString& value)
{
	return QString("\"%1\"").arg(jsonEscape(value));
}

QString hexValue(uint value, int width)
{
	return QString("$%1").arg(value, width, 16, QLatin1Char('0'));
}

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

QString opcodeJson(const HistoryEntry& entry)
{
	QStringList bytes;
	bytes.reserve(entry.size);
	for (uint8 i = 0; i < entry.size && i < entry.opcode.size(); i++)
	{
		bytes.append(QString::number(static_cast<uint>(entry.opcode[i])));
	}
	return QString("[%1]").arg(bytes.join(','));
}

int effectiveAddressForOpcode(const HistoryEntry& entry)
{
	const uint8 op = entry.opcode[0];
	const uint8 operand = entry.opcode[1];
	const uint16 absolute = static_cast<uint16>(entry.opcode[1] | (entry.opcode[2] << 8));
	switch (optype[op])
	{
		case 1:
		{
			const uint zp = (operand + entry.x) & 0xFF;
			return GetMem(static_cast<uint16>(zp)) | (GetMem(static_cast<uint16>((zp + 1) & 0xFF)) << 8);
		}
		case 2:
			return operand;
		case 3:
			return absolute;
		case 4:
			return (GetMem(operand) | (GetMem(static_cast<uint16>((operand + 1) & 0xFF)) << 8)) + entry.y;
		case 5:
			return (operand + entry.x) & 0xFF;
		case 6:
			return (absolute + entry.y) & 0xFFFF;
		case 7:
			return (absolute + entry.x) & 0xFFFF;
		case 8:
			return (operand + entry.y) & 0xFF;
		default:
			return -1;
	}
}

QString accessModeJson(uint8 flags)
{
	QString mode;
	if (flags & WP_R)
	{
		mode += 'r';
	}
	if (flags & WP_W)
	{
		mode += 'w';
	}
	return mode.isEmpty() ? QStringLiteral("null") : jsonString(mode);
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

bool parseBoolText(const QString& text, bool* value)
{
	if (text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || text == QStringLiteral("1") || text.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0)
	{
		*value = true;
		return true;
	}
	if (text.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 || text == QStringLiteral("0") || text.compare(QStringLiteral("no"), Qt::CaseInsensitive) == 0)
	{
		*value = false;
		return true;
	}
	return false;
}

void traceInstruction(uint8* opcode, int size)
{
	if (!g_historyEnabled.load(std::memory_order_relaxed))
	{
		return;
	}

	HistoryEntry entry;
	entry.instruction = total_instructions;
	entry.cycle = timestampbase + static_cast<uint64>(timestamp) - total_cycles_base;
	entry.frame = currFrameCounter;
	entry.scanline = newppu ? newppu_get_scanline() : scanline;
	entry.dot = newppu ? newppu_get_dot() : -1;
	entry.pc = X.PC;
	entry.bank = getBank(static_cast<int>(entry.pc));
	entry.prgOffset = GetPRGAddress(static_cast<int>(entry.pc));
	entry.a = X.A;
	entry.x = X.X;
	entry.y = X.Y;
	entry.s = X.S;
	entry.p = X.P;
	entry.size = static_cast<uint8>(std::max(0, std::min(size, 3)));
	for (uint8 i = 0; i < entry.size; i++)
	{
		entry.opcode[i] = opcode[i];
	}
	entry.accessFlags = opbrktype[entry.opcode[0]] & (WP_R | WP_W);
	if (entry.accessFlags)
	{
		entry.effectiveAddress = effectiveAddressForOpcode(entry);
		if ((entry.accessFlags & WP_W) && entry.effectiveAddress >= 0)
		{
			entry.preWriteValue = GetMem(static_cast<uint16>(entry.effectiveAddress));
		}
	}

	uint8 disasmOpcode[3] = {entry.opcode[0], entry.opcode[1], entry.opcode[2]};
	const char* text = Disassemble(entry.pc, disasmOpcode);
	std::strncpy(entry.disasm, text != nullptr ? text : "", sizeof(entry.disasm) - 1);
	entry.disasm[sizeof(entry.disasm) - 1] = '\0';

	std::lock_guard<std::mutex> lock(g_historyMutex);
	if (!g_historyEnabled.load(std::memory_order_relaxed) || g_history.empty())
	{
		return;
	}
	entry.sequence = ++g_historySequence;
	const size_t capacity = g_history.size();
	const size_t index = (g_historyStart + g_historyCount) % capacity;
	g_history[index] = entry;
	if (g_historyCount < capacity)
	{
		g_historyCount++;
	}
	else
	{
		g_historyStart = (g_historyStart + 1) % capacity;
	}
}

}

void StartHistory()
{
	g_historyEnabled.store(true, std::memory_order_relaxed);
	if (g_traceHandle == nullptr)
	{
		g_traceHandle = FCEUI_TraceInstructionRegister(traceInstruction);
	}
}

void StopHistory()
{
	if (g_traceHandle != nullptr)
	{
		FCEUI_TraceInstructionUnregisterHandle(g_traceHandle);
		g_traceHandle = nullptr;
	}
	std::lock_guard<std::mutex> lock(g_historyMutex);
	g_historyEnabled.store(false, std::memory_order_relaxed);
	g_historyStart = 0;
	g_historyCount = 0;
	g_historySequence = 0;
}

void ClearHistory()
{
	std::lock_guard<std::mutex> lock(g_historyMutex);
	g_historyStart = 0;
	g_historyCount = 0;
	g_historySequence = 0;
}

bool HistoryEnabled()
{
	return g_historyEnabled.load(std::memory_order_relaxed);
}

bool ConfigureHistory(int capacity, bool enabled, QString* error)
{
	if (capacity != 0 && (capacity < 1 || static_cast<size_t>(capacity) > kMaxHistoryCapacity))
	{
		if (error != nullptr)
		{
			*error = QString("history size must be between 1 and %1").arg(static_cast<uint>(kMaxHistoryCapacity));
		}
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_historyMutex);
		if (capacity != 0 && static_cast<size_t>(capacity) != g_history.size())
		{
			g_history.clear();
			g_history.resize(static_cast<size_t>(capacity));
			g_historyStart = 0;
			g_historyCount = 0;
			g_historySequence = 0;
		}
		else if (!enabled)
		{
			g_historyStart = 0;
			g_historyCount = 0;
			g_historySequence = 0;
		}
		g_historyEnabled.store(enabled, std::memory_order_relaxed);
	}

	if (enabled)
	{
		if (g_traceHandle == nullptr)
		{
			g_traceHandle = FCEUI_TraceInstructionRegister(traceInstruction);
		}
	}
	else if (g_traceHandle != nullptr)
	{
		FCEUI_TraceInstructionUnregisterHandle(g_traceHandle);
		g_traceHandle = nullptr;
	}
	return true;
}

QString HistoryStatusJson()
{
	std::lock_guard<std::mutex> lock(g_historyMutex);
	return QString("{\"ok\":true,\"enabled\":%1,\"capacity\":%2,\"count\":%3,\"sequence\":%4,\"registered\":%5}")
		.arg(g_historyEnabled.load(std::memory_order_relaxed) ? "true" : "false")
		.arg(static_cast<uint>(g_history.size()))
		.arg(static_cast<uint>(g_historyCount))
		.arg(QString::number(static_cast<qulonglong>(g_historySequence)))
		.arg(g_traceHandle != nullptr ? "true" : "false");
}

QString HistoryJson(int count, bool includeDisasm)
{
	std::vector<HistoryEntry> entries;
	size_t capacity = 0;
	bool enabled = false;
	{
		std::lock_guard<std::mutex> lock(g_historyMutex);
		capacity = g_history.size();
		enabled = g_historyEnabled.load(std::memory_order_relaxed);
		const size_t take = std::min(static_cast<size_t>(std::max(count, 0)), g_historyCount);
		entries.reserve(take);
		const size_t first = g_historyStart + (g_historyCount - take);
		for (size_t i = 0; i < take; i++)
		{
			entries.push_back(g_history[(first + i) % capacity]);
		}
	}

	QStringList items;
	items.reserve(static_cast<int>(entries.size()));
	for (const HistoryEntry& entry : entries)
	{
		QString item = QString("{\"seq\":%1,\"frame\":%2,\"scanline\":%3,\"dot\":%4,\"instruction\":%5,\"cycle\":%6,\"PC\":%7,\"bank\":%8,\"prg_offset\":%9,\"effective_addr\":%10,\"access\":%11,\"pre_write\":%12,\"A\":%13,\"X\":%14,\"Y\":%15,\"S\":%16,\"P\":%17,\"flags\":%18,\"size\":%19,\"opcode\":%20")
			.arg(QString::number(static_cast<qulonglong>(entry.sequence)))
			.arg(entry.frame)
			.arg(entry.scanline)
			.arg(entry.dot)
			.arg(QString::number(static_cast<qulonglong>(entry.instruction)))
			.arg(QString::number(static_cast<qulonglong>(entry.cycle)))
			.arg(jsonString(hexValue(entry.pc, 4)))
			.arg(entry.bank)
			.arg(entry.prgOffset >= 0 ? jsonString(hexValue(static_cast<uint>(entry.prgOffset), 6)) : QStringLiteral("null"))
			.arg(entry.effectiveAddress >= 0 ? jsonString(hexValue(static_cast<uint>(entry.effectiveAddress), 4)) : QStringLiteral("null"))
			.arg(accessModeJson(entry.accessFlags))
			.arg(entry.preWriteValue >= 0 ? jsonString(hexValue(static_cast<uint>(entry.preWriteValue), 2)) : QStringLiteral("null"))
			.arg(jsonString(hexValue(entry.a, 2)))
			.arg(jsonString(hexValue(entry.x, 2)))
			.arg(jsonString(hexValue(entry.y, 2)))
			.arg(jsonString(hexValue(entry.s, 2)))
			.arg(jsonString(hexValue(entry.p, 2)))
			.arg(jsonString(flagsString(entry.p)))
			.arg(static_cast<uint>(entry.size))
			.arg(opcodeJson(entry));
		if (includeDisasm)
		{
			item += QString(",\"disasm\":%1").arg(jsonString(QString::fromLocal8Bit(entry.disasm)));
		}
		item += '}';
		items.append(item);
	}

	return QString("{\"ok\":true,\"enabled\":%1,\"capacity\":%2,\"count\":%3,\"include_disasm\":%4,\"entries\":%5}")
		.arg(enabled ? "true" : "false")
		.arg(static_cast<uint>(capacity))
		.arg(entries.size())
		.arg(includeDisasm ? "true" : "false")
		.arg(QString("[%1]").arg(items.join(',')));
}

QString HistoryResponse(const QStringList& parts)
{
	int count = 64;
	if (parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"HISTORY accepts optional count and include_disasm option\"}");
	}
	bool includeDisasm = true;
	if (parts.size() >= 2 && !parts[1].startsWith(QStringLiteral("include_disasm="), Qt::CaseInsensitive) && !parsePositiveInt(parts[1], &count))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"HISTORY count must be a positive integer\"}");
	}
	const QString includeDisasmText = OptionValue(parts, QStringLiteral("include_disasm"));
	if (!includeDisasmText.isEmpty() && !parseBoolText(includeDisasmText, &includeDisasm))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid include_disasm value\"}");
	}
	if (count > 4096)
	{
		count = 4096;
	}
	return HistoryJson(count, includeDisasm);
}

QString HistoryClearResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"HISTORY_CLEAR takes no arguments\"}");
	}
	ClearHistory();
	return QStringLiteral("{\"ok\":true,\"cleared\":true}");
}

QString HistoryConfigResponse(const QStringList& parts)
{
	if (parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"HISTORY_CONFIG accepts size=N and enabled=true|false\"}");
	}
	int capacity = 0;
	bool enabled = HistoryEnabled();
	const QString sizeText = OptionValue(parts, QStringLiteral("size"));
	if (!sizeText.isEmpty() && (!parsePositiveInt(sizeText, &capacity) || capacity > 131072))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid history size\"}");
	}
	const QString enabledText = OptionValue(parts, QStringLiteral("enabled"));
	if (!enabledText.isEmpty() && !parseBoolText(enabledText, &enabled))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid history enabled value\"}");
	}
	QString error;
	if (!ConfigureHistory(capacity, enabled, &error))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(error));
	}
	return HistoryStatusJson();
}

QString TraceStartResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"TRACE_START takes no arguments\"}");
	}
	StartHistory();
	return HistoryStatusJson();
}

QString TraceStopResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"TRACE_STOP takes no arguments\"}");
	}
	StopHistory();
	return HistoryStatusJson();
}

QString TraceStatusResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"TRACE_STATUS takes no arguments\"}");
	}
	return HistoryStatusJson();
}

}
