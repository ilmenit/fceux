#include "bridge/BridgeApu.h"

#include "bridge/BridgeJson.h"
#include "debug.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QString hexValue(uint value, int width)
{
	return QString("$%1").arg(value, width, 16, QLatin1Char('0'));
}

}

QString ApuStateResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	return QString("{\"ok\":true,\"psg\":%1,\"dmc_format\":%2,\"raw_da_latch\":%3,\"dmc_address_latch\":%4,\"dmc_size_latch\":%5,\"enabled_channels\":%6,\"channels\":{\"square1\":%7,\"square2\":%8,\"triangle\":%9,\"noise\":%10,\"dmc\":%11}}")
		.arg(JsonRawBytes(PSG, 0x10))
		.arg(JsonString(hexValue(DMCFormat, 2)))
		.arg(JsonString(hexValue(RawDALatch, 2)))
		.arg(JsonString(hexValue(DMCAddressLatch, 2)))
		.arg(JsonString(hexValue(DMCSizeLatch, 2)))
		.arg(JsonString(hexValue(EnabledChannels, 2)))
		.arg((EnabledChannels & 0x01) ? "true" : "false")
		.arg((EnabledChannels & 0x02) ? "true" : "false")
		.arg((EnabledChannels & 0x04) ? "true" : "false")
		.arg((EnabledChannels & 0x08) ? "true" : "false")
		.arg((EnabledChannels & 0x10) ? "true" : "false");
}

QString ApuStateCommandResponse(const QStringList& parts)
{
	if (parts.size() != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"APU_STATE takes no arguments\"}");
	}
	return ApuStateResponse();
}

}
