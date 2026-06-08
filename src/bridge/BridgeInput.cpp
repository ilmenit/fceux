#include "bridge/BridgeInput.h"

#include <mutex>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"

namespace FCEUXBridge
{

namespace
{
std::mutex g_inputMutex;
uint8 g_joypadMask[4] = {0, 0, 0, 0};
}

uint8 ApplyJoypad(int which, uint8 physical)
{
	if (which < 0 || which >= 4)
	{
		return physical;
	}
	std::lock_guard<std::mutex> lock(g_inputMutex);
	return physical | g_joypadMask[which];
}

void SetJoypad(int which, uint8 mask)
{
	if (which < 0 || which >= 4)
	{
		return;
	}
	std::lock_guard<std::mutex> lock(g_inputMutex);
	g_joypadMask[which] = mask;
}

void ClearJoypad(int which)
{
	if (which < 0 || which >= 4)
	{
		return;
	}
	std::lock_guard<std::mutex> lock(g_inputMutex);
	g_joypadMask[which] = 0;
}

void ClearAllJoypads()
{
	std::lock_guard<std::mutex> lock(g_inputMutex);
	for (uint8& mask : g_joypadMask)
	{
		mask = 0;
	}
}

uint8 GetJoypad(int which)
{
	if (which < 0 || which >= 4)
	{
		return 0;
	}
	std::lock_guard<std::mutex> lock(g_inputMutex);
	return g_joypadMask[which];
}

bool ButtonMaskFromName(const QString& name, uint8* bit)
{
	const QString n = name.toLower();
	if (n == QStringLiteral("a")) { *bit = 1 << 0; return true; }
	if (n == QStringLiteral("b")) { *bit = 1 << 1; return true; }
	if (n == QStringLiteral("select")) { *bit = 1 << 2; return true; }
	if (n == QStringLiteral("start")) { *bit = 1 << 3; return true; }
	if (n == QStringLiteral("up")) { *bit = 1 << 4; return true; }
	if (n == QStringLiteral("down")) { *bit = 1 << 5; return true; }
	if (n == QStringLiteral("left")) { *bit = 1 << 6; return true; }
	if (n == QStringLiteral("right")) { *bit = 1 << 7; return true; }
	return false;
}

QString JoypadJson(uint8 mask)
{
	return QString("{\"a\":%1,\"b\":%2,\"select\":%3,\"start\":%4,\"up\":%5,\"down\":%6,\"left\":%7,\"right\":%8}")
		.arg((mask & (1 << 0)) ? "true" : "false")
		.arg((mask & (1 << 1)) ? "true" : "false")
		.arg((mask & (1 << 2)) ? "true" : "false")
		.arg((mask & (1 << 3)) ? "true" : "false")
		.arg((mask & (1 << 4)) ? "true" : "false")
		.arg((mask & (1 << 5)) ? "true" : "false")
		.arg((mask & (1 << 6)) ? "true" : "false")
		.arg((mask & (1 << 7)) ? "true" : "false");
}

QString JoySetResponse(int port, const QStringList& buttons)
{
	uint8 mask = 0;
	for (const QString& button : buttons)
	{
		uint8 bit = 0;
		if (!ButtonMaskFromName(button, &bit))
		{
			return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("unknown button: %1").arg(button)));
		}
		mask |= bit;
	}
	SetJoypad(port, mask);
	return QString("{\"ok\":true,\"port\":%1,\"mask\":%2,\"buttons\":%3}")
		.arg(port)
		.arg(mask)
		.arg(JoypadJson(mask));
}

QString JoyClearResponse(int port)
{
	ClearJoypad(port);
	return QString("{\"ok\":true,\"port\":%1,\"mask\":0}").arg(port);
}

QString JoyClearAllResponse()
{
	ClearAllJoypads();
	return QStringLiteral("{\"ok\":true,\"cleared\":\"all\"}");
}

QString InputStateResponse()
{
	return QString("{\"ok\":true,\"joypads\":[%1,%2,%3,%4]}")
		.arg(JoypadJson(GetJoypad(0)))
		.arg(JoypadJson(GetJoypad(1)))
		.arg(JoypadJson(GetJoypad(2)))
		.arg(JoypadJson(GetJoypad(3)));
}

QString JoySetCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"JOY requires port [buttons...]\"}");
	}
	uint port = 0;
	if (!ParseNumber(parts[1], &port) || port > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid joypad port\"}");
	}
	return JoySetResponse(static_cast<int>(port), parts.mid(2));
}

QString JoyClearCommandResponse(const QStringList& parts)
{
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"JOY_CLEAR accepts optional port\"}");
	}
	if (parts.size() == 1)
	{
		return JoyClearAllResponse();
	}
	uint port = 0;
	if (!ParseNumber(parts[1], &port) || port > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid joypad port\"}");
	}
	return JoyClearResponse(static_cast<int>(port));
}

QString InputStateCommandResponse(const QStringList& parts)
{
	Q_UNUSED(parts);
	return InputStateResponse();
}

}
