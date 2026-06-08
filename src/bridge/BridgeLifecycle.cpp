#include "bridge/BridgeLifecycle.h"

#include <QFileInfo>
#include <QTimer>

#include "bridge/BridgeDebugger.h"
#include "bridge/BridgeHistory.h"
#include "bridge/BridgeJson.h"
#include "bridge/BridgeScheduler.h"
#include "bridge/BridgeStateStore.h"
#include "fceu.h"
#include "cart.h"
#include "movie.h"
#include "Qt/dface.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

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

}

QString StatusResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	return QString("{\"ok\":true,\"paused\":%1,\"game_loaded\":%2,\"frame\":%3,\"emulator_cycle\":%4,\"gate_active\":%5}")
		.arg(FCEUI_EmulationPaused() ? "true" : "false")
		.arg(fceuWrapperGameLoaded() ? "true" : "false")
		.arg(currFrameCounter)
		.arg(emulatorCycleCount)
		.arg(IsFrameGateActive() ? "true" : "false");
}

QString PauseResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	CancelFrameGate();
	SetPausedBit(true);
	return QStringLiteral("{\"ok\":true,\"paused\":true}");
}

QString ResumeResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	CancelFrameGate();
	SetPausedBit(false);
	return QStringLiteral("{\"ok\":true,\"paused\":false}");
}

QString FrameResponse(int frames)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	BeginFrameGate(frames);
	return QString("{\"ok\":true,\"frames\":%1,\"gate_active\":true}").arg(frames);
}

QString WaitResponse()
{
	WaitFrameGate();
	FCEU_CRITICAL_SECTION(lock);
	return QString("{\"ok\":true,\"frame\":%1,\"paused\":%2,\"gate_active\":false}")
		.arg(currFrameCounter)
		.arg(FCEUI_EmulationPaused() ? "true" : "false");
}

QString LoadRomResponse(const QString& requestedPath)
{
	const QFileInfo rom(requestedPath);
	if (!rom.exists())
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("ROM not found: %1").arg(requestedPath)));
	}
	const QString path = rom.canonicalFilePath();
	FCEU_CRITICAL_SECTION(lock);
	CancelFrameGate();
	ClearStateSlots();
	ClearHistory();
	ClearAllBreakpoints();
	const int result = LoadGame(path.toLocal8Bit().constData(), true);
	if (result != 1)
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("failed to load ROM: %1").arg(path)));
	}
	return QString("{\"ok\":true,\"rom\":%1}").arg(JsonString(path));
}

QString ResetResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	CancelFrameGate();
	ClearHistory();
	fceuWrapperSoftReset();
	return QStringLiteral("{\"ok\":true}");
}

QString PowerResponse()
{
	FCEU_CRITICAL_SECTION(lock);
	CancelFrameGate();
	ClearHistory();
	if (GameInfo != nullptr)
	{
		PowerNES();
	}
	return QStringLiteral("{\"ok\":true}");
}

QString AppExitResponse()
{
	QTimer::singleShot(0, []() {
		fceuWrapperRequestAppExit();
	});
	return QStringLiteral("{\"ok\":true,\"exiting\":true}");
}

QString FrameCommandResponse(const QStringList& parts)
{
	int frames = 1;
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"FRAME accepts at most one count\"}");
	}
	if (parts.size() == 2 && !parsePositiveInt(parts[1], &frames))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"FRAME count must be a positive integer\"}");
	}
	return FrameResponse(frames);
}

QString LoadRomCommandResponse(const QStringList& parts)
{
	if (parts.size() != 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"LOAD_ROM requires path\"}");
	}
	return LoadRomResponse(parts[1]);
}

}
