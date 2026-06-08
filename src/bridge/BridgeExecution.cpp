#include "bridge/BridgeExecution.h"

#include <algorithm>

#include "bridge/BridgeDebugger.h"
#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "bridge/BridgeScheduler.h"
#include "debug.h"
#include "fceu.h"
#include "movie.h"
#include "x6502.h"
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

QString StepResponse(int count, int timeoutFrames)
{
	bool timedOut = false;
	for (int i = 0; i < count; i++)
	{
		{
			FCEU_CRITICAL_SECTION(lock);
			if (!fceuWrapperGameLoaded())
			{
				return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
			}
			ClearBreakStatus();
			FCEUI_Debugger().step = true;
			BeginFrameGate(timeoutFrames);
		}
		if (!WaitRunUntilBreakOrGate())
		{
			timedOut = true;
			break;
		}
	}
	{
		FCEU_CRITICAL_SECTION(lock);
		const QString status = BreakStatusJson();
		return QString("{\"ok\":%1,\"target\":\"step\",\"steps_requested\":%2,\"timeout\":%3,\"break\":%4}")
			.arg(timedOut ? "false" : "true")
			.arg(count)
			.arg(timedOut ? "true" : "false")
			.arg(status);
	}
}

QString StepOutResponse(int timeoutFrames)
{
	{
		FCEU_CRITICAL_SECTION(lock);
		if (!fceuWrapperGameLoaded())
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
		}
		ClearBreakStatus();
		DebuggerState& dbgstate = FCEUI_Debugger();
		dbgstate.jsrcount = (GetMem(X.PC) == 0x20) ? 1 : 0;
		dbgstate.stepout = true;
		BeginFrameGate(timeoutFrames);
	}
	const bool hit = WaitRunUntilBreakOrGate();
	{
		FCEU_CRITICAL_SECTION(lock);
		const QString status = BreakStatusJson();
		return QString("{\"ok\":%1,\"target\":\"step_out\",\"timeout\":%2,\"break\":%3}")
			.arg(hit ? "true" : "false")
			.arg(hit ? "false" : "true")
			.arg(status);
	}
}

QString StepOverResponse(int timeoutFrames)
{
	bool usedReservedSlot = false;
	{
		FCEU_CRITICAL_SECTION(lock);
		if (!fceuWrapperGameLoaded())
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
		}
		if (watchpoint[64].flags)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"step-over already active\"}");
		}
		ClearBreakStatus();
		const uint8 opcode = GetMem(X.PC);
		if (opcode == 0x20)
		{
			watchpoint[64].address = (X.PC + 3) & 0xFFFF;
			watchpoint[64].flags = WP_E | WP_X;
			usedReservedSlot = true;
		}
		else
		{
			FCEUI_Debugger().step = true;
		}
		BeginFrameGate(timeoutFrames);
	}
	const bool hit = WaitRunUntilBreakOrGate();
	{
		FCEU_CRITICAL_SECTION(lock);
		if (!hit && usedReservedSlot)
		{
			watchpoint[64].address = 0;
			watchpoint[64].flags = 0;
		}
		const QString status = BreakStatusJson();
		return QString("{\"ok\":%1,\"target\":\"step_over\",\"timeout\":%2,\"break\":%3}")
			.arg(hit ? "true" : "false")
			.arg(hit ? "false" : "true")
			.arg(status);
	}
}

QString RunUntilResponse(const QString& target, int timeoutFrames)
{
	if (target.startsWith(QStringLiteral("frame="), Qt::CaseInsensitive))
	{
		uint targetFrame = 0;
		if (!ParseNumber(target.mid(6), &targetFrame))
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid frame target\"}");
		}
		{
			FCEU_CRITICAL_SECTION(lock);
			if (!fceuWrapperGameLoaded())
			{
				return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
			}
			if (static_cast<uint>(currFrameCounter) >= targetFrame)
			{
				return QString("{\"ok\":true,\"target\":\"frame\",\"frame\":%1,\"timeout\":false}").arg(currFrameCounter);
			}
			const uint delta = targetFrame - static_cast<uint>(currFrameCounter);
			BeginFrameGate(static_cast<int>(std::min<uint>(delta, static_cast<uint>(timeoutFrames))));
		}
		WaitFrameGate();
		{
			FCEU_CRITICAL_SECTION(lock);
			const bool timeout = static_cast<uint>(currFrameCounter) < targetFrame;
			return QString("{\"ok\":%1,\"target\":\"frame\",\"frame\":%2,\"timeout\":%3%4}")
				.arg(timeout ? "false" : "true")
				.arg(currFrameCounter)
				.arg(timeout ? "true" : "false")
				.arg(timeout ? QStringLiteral(",\"error\":\"timeout\"") : QString());
		}
	}

	int tempBreakpointId = 0;
	const bool pcTarget = target.startsWith(QStringLiteral("pc="), Qt::CaseInsensitive);
	if (pcTarget)
	{
		uint pc = 0;
		if (!ParseNumber(target.mid(3), &pc) || pc > 0xFFFF)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid pc target\"}");
		}
		QString response;
		QString error;
		FCEU_CRITICAL_SECTION(lock);
		if (!AddBreakpoint(QStringLiteral("cpu"), QStringLiteral("x"), pc, -1, QString(), QStringLiteral("run-until-pc"), true, &response, &error))
		{
			return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(error));
		}
		const int idPos = response.indexOf(QStringLiteral("\"id\":"));
		if (idPos >= 0)
		{
			bool ok = false;
			tempBreakpointId = response.mid(idPos + 5).section(',', 0, 0).toInt(&ok);
			if (!ok)
			{
				tempBreakpointId = 0;
			}
		}
	}
	else if (target != QStringLiteral("break"))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"RUN_UNTIL target must be break, pc=ADDR, or frame=N\"}");
	}

	{
		FCEU_CRITICAL_SECTION(lock);
		if (!fceuWrapperGameLoaded())
		{
			if (tempBreakpointId > 0)
			{
				QString ignored;
				ClearBreakpoint(tempBreakpointId, &ignored);
			}
			return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
		}
		ClearBreakStatus();
		BeginFrameGate(timeoutFrames);
	}
	const bool hit = WaitRunUntilBreakOrGate();
	{
		FCEU_CRITICAL_SECTION(lock);
		const QString status = BreakStatusJson();
		if (tempBreakpointId > 0)
		{
			QString ignored;
			ClearBreakpoint(tempBreakpointId, &ignored);
		}
		const QString targetJson = JsonString(pcTarget ? QStringLiteral("pc") : QStringLiteral("break"));
		if (hit)
		{
			return QString("{\"ok\":true,\"target\":%1,\"timeout\":false,\"break\":%2}")
				.arg(targetJson)
				.arg(status);
		}
		return QString("{\"ok\":false,\"error\":\"timeout\",\"target\":%1,\"timeout\":true,\"frame\":%2}")
			.arg(targetJson)
			.arg(currFrameCounter);
	}
}

QString StepCommandResponse(const QStringList& parts)
{
	int count = 1;
	if (parts.size() > 3)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STEP accepts optional count and timeout_frames=N\"}");
	}
	if (parts.size() >= 2 && !parts[1].startsWith(QStringLiteral("timeout_frames="), Qt::CaseInsensitive) && !parsePositiveInt(parts[1], &count))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STEP count must be a positive integer\"}");
	}
	if (count > 10000)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STEP count too large\"}");
	}
	int timeoutFrames = 60;
	const QString timeoutText = OptionValue(parts, QStringLiteral("timeout_frames"));
	if (!timeoutText.isEmpty() && !parsePositiveInt(timeoutText, &timeoutFrames))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid timeout_frames\"}");
	}
	return StepResponse(count, timeoutFrames);
}

QString StepOutCommandResponse(const QStringList& parts)
{
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STEP_OUT accepts optional timeout_frames=N\"}");
	}
	int timeoutFrames = 600;
	const QString timeoutText = OptionValue(parts, QStringLiteral("timeout_frames"));
	if (!timeoutText.isEmpty() && !parsePositiveInt(timeoutText, &timeoutFrames))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid timeout_frames\"}");
	}
	return StepOutResponse(timeoutFrames);
}

QString StepOverCommandResponse(const QStringList& parts)
{
	if (parts.size() > 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STEP_OVER accepts optional timeout_frames=N\"}");
	}
	int timeoutFrames = 600;
	const QString timeoutText = OptionValue(parts, QStringLiteral("timeout_frames"));
	if (!timeoutText.isEmpty() && !parsePositiveInt(timeoutText, &timeoutFrames))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid timeout_frames\"}");
	}
	return StepOverResponse(timeoutFrames);
}

QString RunUntilCommandResponse(const QStringList& parts)
{
	if (parts.size() < 2)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"RUN_UNTIL requires break, pc=ADDR, or frame=N\"}");
	}
	int timeoutFrames = 600;
	const QString timeoutText = OptionValue(parts, QStringLiteral("timeout_frames"));
	if (!timeoutText.isEmpty() && !parsePositiveInt(timeoutText, &timeoutFrames))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid timeout_frames\"}");
	}

	return RunUntilResponse(parts[1], timeoutFrames);
}

}
