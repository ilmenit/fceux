#include "bridge/BridgeDebugger.h"

#include <cstdlib>
#include <cstring>
#include <mutex>

#include <QStringList>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "types.h"
#include "debug.h"
#include "fceu.h"
#include "x6502.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

std::mutex g_debuggerMutex;
int g_slotBridgeIds[64] = {0};
int g_nextBridgeId = 1;
int g_lastHitSlot = -1;
int g_lastHitBridgeId = 0;
int g_lastHitType = 0;
bool g_hasLastHit = false;
uint64 g_lastHitInstruction = 0;
uint64 g_lastHitCycle = 0;
uint16 g_lastHitPc = 0;

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

QString domainFromFlags(uint16 flags)
{
	if (flags & BT_P) { return QStringLiteral("ppu"); }
	if (flags & BT_S) { return QStringLiteral("oam"); }
	if (flags & BT_R) { return QStringLiteral("rom"); }
	return QStringLiteral("cpu");
}

QString modeFromFlags(uint16 flags)
{
	QString mode;
	if (flags & WP_R) { mode += 'r'; }
	if (flags & WP_W) { mode += 'w'; }
	if (flags & WP_X) { mode += 'x'; }
	if (mode.isEmpty() && (flags & WP_F)) { mode = QStringLiteral("forbid"); }
	return mode;
}

bool typeFromDomainMode(const QString& domain, const QString& mode, uint* type, QString* error)
{
	const QString d = domain.isEmpty() ? QStringLiteral("cpu") : domain.toLower();
	const QString m = mode.isEmpty() ? QStringLiteral("x") : mode.toLower();

	uint result = 0;
	if (d == QStringLiteral("cpu"))
	{
		result |= BT_C;
	}
	else if (d == QStringLiteral("ppu"))
	{
		result |= BT_P;
	}
	else if (d == QStringLiteral("oam") || d == QStringLiteral("sprite"))
	{
		result |= BT_S;
	}
	else if (d == QStringLiteral("rom"))
	{
		result |= BT_R;
	}
	else
	{
		*error = QStringLiteral("invalid breakpoint domain");
		return false;
	}

	if (m == QStringLiteral("r"))
	{
		result |= WP_R;
	}
	else if (m == QStringLiteral("w"))
	{
		result |= WP_W;
	}
	else if (m == QStringLiteral("x"))
	{
		result |= WP_X;
	}
	else if (m == QStringLiteral("rw") || m == QStringLiteral("wr"))
	{
		result |= WP_R | WP_W;
	}
	else if (m == QStringLiteral("rwx") || m == QStringLiteral("rxw") || m == QStringLiteral("wrx") || m == QStringLiteral("wxr") || m == QStringLiteral("xrw") || m == QStringLiteral("xwr"))
	{
		result |= WP_R | WP_W | WP_X;
	}
	else
	{
		*error = QStringLiteral("invalid breakpoint mode");
		return false;
	}

	if ((result & (BT_P | BT_S)) && (result & WP_X))
	{
		*error = QStringLiteral("PPU and OAM breakpoints do not support execute mode");
		return false;
	}

	*type = result;
	return true;
}

bool addressValidForDomain(uint addr, int end, uint type)
{
	uint limit = 0x10000;
	if (type & BT_P) { limit = 0x4000; }
	else if (type & BT_S) { limit = 0x100; }
	if (addr >= limit) { return false; }
	if (end >= 0 && static_cast<uint>(end) >= limit) { return false; }
	if (end >= 0 && static_cast<uint>(end) < addr) { return false; }
	return true;
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

void freeWatchpoint(int slot)
{
	if (slot < 0 || slot >= 64)
	{
		return;
	}
	if (watchpoint[slot].cond)
	{
		delete watchpoint[slot].cond;
	}
	if (watchpoint[slot].condText)
	{
		free(watchpoint[slot].condText);
	}
	if (watchpoint[slot].desc)
	{
		free(watchpoint[slot].desc);
	}
	watchpoint[slot].address = 0;
	watchpoint[slot].endaddress = 0;
	watchpoint[slot].flags = 0;
	watchpoint[slot].cond = nullptr;
	watchpoint[slot].condText = nullptr;
	watchpoint[slot].desc = nullptr;
	g_slotBridgeIds[slot] = 0;
}

int slotForBridgeId(int bridgeId)
{
	for (int i = 0; i < numWPs && i < 64; i++)
	{
		if (g_slotBridgeIds[i] == bridgeId)
		{
			return i;
		}
	}
	return -1;
}

QString breakpointJson(int slot)
{
	const watchpointinfo& wp = watchpoint[slot];
	return QString("{\"id\":%1,\"slot\":%2,\"enabled\":%3,\"domain\":%4,\"mode\":%5,\"addr\":%6,\"end\":%7,\"condition\":%8,\"name\":%9}")
		.arg(g_slotBridgeIds[slot])
		.arg(slot)
		.arg((wp.flags & WP_E) ? "true" : "false")
		.arg(jsonString(domainFromFlags(wp.flags)))
		.arg(jsonString(modeFromFlags(wp.flags)))
		.arg(jsonString(hexValue(wp.address, (wp.flags & BT_S) ? 2 : 4)))
		.arg(wp.endaddress ? jsonString(hexValue(wp.endaddress, (wp.flags & BT_S) ? 2 : 4)) : QStringLiteral("null"))
		.arg(wp.condText ? jsonString(QString::fromLocal8Bit(wp.condText)) : QStringLiteral("null"))
		.arg(wp.desc ? jsonString(QString::fromLocal8Bit(wp.desc)) : QStringLiteral("null"));
}

}

void NotifyBreakHit(int slot)
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	g_hasLastHit = true;
	g_lastHitType = slot;
	g_lastHitSlot = (slot >= 0 && slot < 64) ? slot : -1;
	g_lastHitBridgeId = (g_lastHitSlot >= 0) ? g_slotBridgeIds[g_lastHitSlot] : 0;
	g_lastHitInstruction = total_instructions;
	g_lastHitCycle = timestampbase + static_cast<uint64>(timestamp) - total_cycles_base;
	g_lastHitPc = X.PC;
}

void ClearBreakStatus()
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	g_lastHitSlot = -1;
	g_lastHitBridgeId = 0;
	g_lastHitType = 0;
	g_hasLastHit = false;
	g_lastHitInstruction = 0;
	g_lastHitCycle = 0;
	g_lastHitPc = 0;
}

bool HasBreakHit()
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	return g_hasLastHit;
}

bool AddBreakpoint(const QString& domain, const QString& mode, uint addr, int end, const QString& condition, const QString& name, bool enabled, QString* response, QString* error)
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	if (numWPs >= 64)
	{
		*error = QStringLiteral("breakpoint table full");
		return false;
	}

	uint type = 0;
	if (!typeFromDomainMode(domain, mode, &type, error))
	{
		return false;
	}
	if (!addressValidForDomain(addr, end, type))
	{
		*error = QStringLiteral("breakpoint address out of range");
		return false;
	}

	const int slot = numWPs;
	const int bridgeId = g_nextBridgeId++;
	const QString bpName = name.isEmpty() ? QString("bridge-%1").arg(bridgeId) : name;
	const unsigned int result = NewBreak(
		bpName.toLocal8Bit().constData(),
		static_cast<int>(addr),
		end,
		type,
		condition.toLocal8Bit().constData(),
		static_cast<unsigned int>(slot),
		enabled);
	if (result == 1 || result == 2)
	{
		freeWatchpoint(slot);
		*error = QStringLiteral("invalid breakpoint condition");
		return false;
	}

	g_slotBridgeIds[slot] = bridgeId;
	numWPs++;
	*response = QString("{\"ok\":true,\"id\":%1,\"breakpoint\":%2}").arg(bridgeId).arg(breakpointJson(slot));
	return true;
}

QString BreakpointListJson()
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	QStringList items;
	items.reserve(numWPs);
	for (int i = 0; i < numWPs && i < 64; i++)
	{
		items.append(breakpointJson(i));
	}
	return QString("{\"ok\":true,\"breakpoints\":[%1]}").arg(items.join(','));
}

bool ClearBreakpoint(int bridgeId, QString* error)
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	const int slot = slotForBridgeId(bridgeId);
	if (slot < 0)
	{
		*error = QStringLiteral("unknown breakpoint id");
		return false;
	}

	freeWatchpoint(slot);
	for (int i = slot; i < numWPs - 1; i++)
	{
		watchpoint[i] = watchpoint[i + 1];
		g_slotBridgeIds[i] = g_slotBridgeIds[i + 1];
	}
	watchpoint[numWPs - 1].address = 0;
	watchpoint[numWPs - 1].endaddress = 0;
	watchpoint[numWPs - 1].flags = 0;
	watchpoint[numWPs - 1].cond = nullptr;
	watchpoint[numWPs - 1].condText = nullptr;
	watchpoint[numWPs - 1].desc = nullptr;
	g_slotBridgeIds[numWPs - 1] = 0;
	numWPs--;
	return true;
}

void ClearAllBreakpoints()
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	for (int i = 0; i < numWPs && i < 64; i++)
	{
		freeWatchpoint(i);
	}
	numWPs = 0;
	g_lastHitSlot = -1;
	g_lastHitBridgeId = 0;
	g_lastHitType = 0;
	g_hasLastHit = false;
}

QString BreakStatusJson()
{
	std::lock_guard<std::mutex> lock(g_debuggerMutex);
	return QString("{\"ok\":true,\"hit\":%1,\"type\":%2,\"slot\":%3,\"id\":%4,\"PC\":%5,\"instruction\":%6,\"cycle\":%7}")
		.arg(g_hasLastHit ? "true" : "false")
		.arg(g_lastHitType)
		.arg(g_lastHitSlot >= 0 ? QString::number(g_lastHitSlot) : QStringLiteral("null"))
		.arg(g_lastHitBridgeId ? QString::number(g_lastHitBridgeId) : QStringLiteral("null"))
		.arg(jsonString(hexValue(g_lastHitPc, 4)))
		.arg(QString::number(static_cast<qulonglong>(g_lastHitInstruction)))
		.arg(QString::number(static_cast<qulonglong>(g_lastHitCycle)));
}

QString BreakpointSetResponse(const QString& cmd, const QStringList& parts)
{
	if (parts.size() < 2)
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("%1 requires addr").arg(cmd)));
	}
	uint addr = 0;
	if (!ParseNumber(parts[1], &addr))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid breakpoint address\"}");
	}
	int end = -1;
	const QString endText = OptionValue(parts, QStringLiteral("end"));
	if (!endText.isEmpty())
	{
		uint parsedEnd = 0;
		if (!ParseNumber(endText, &parsedEnd))
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid breakpoint end address\"}");
		}
		end = static_cast<int>(parsedEnd);
	}
	const QString domain = OptionValue(parts, QStringLiteral("domain"));
	QString mode = OptionValue(parts, QStringLiteral("mode"));
	if (mode.isEmpty() && cmd == QStringLiteral("WATCH_SET"))
	{
		mode = QStringLiteral("rw");
	}
	const QString condition = OptionValue(parts, QStringLiteral("condition"));
	const QString name = OptionValue(parts, QStringLiteral("name"));
	const QString enabledText = OptionValue(parts, QStringLiteral("enabled"));
	const bool enabled = enabledText.isEmpty() || enabledText != QStringLiteral("false");
	QString response;
	QString error;
	FCEU_CRITICAL_SECTION(lock);
	if (!AddBreakpoint(domain, mode, addr, end, condition, name, enabled, &response, &error))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(error));
	}
	return response;
}

QString BreakpointListResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	return BreakpointListJson();
}

QString BreakpointClearResponse(const QStringList& parts)
{
	if (parts.size() != 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"BP_CLEAR requires id\"}");
	}
	int id = 0;
	if (!parsePositiveInt(parts[1], &id))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid breakpoint id\"}");
	}
	QString error;
	FCEU_CRITICAL_SECTION(lock);
	if (!ClearBreakpoint(id, &error))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(error));
	}
	return QString("{\"ok\":true,\"id\":%1,\"cleared\":true}").arg(id);
}

QString BreakpointClearAllResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	ClearAllBreakpoints();
	return QStringLiteral("{\"ok\":true,\"cleared\":\"all\"}");
}

QString BreakStatusResponse()
{
	return BreakStatusJson();
}

}
