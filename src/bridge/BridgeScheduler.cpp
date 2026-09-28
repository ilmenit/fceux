#include "bridge/BridgeScheduler.h"

#include <QCoreApplication>
#include <QEventLoop>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "bridge/BridgeDebugger.h"
#include "fceu.h"
#include "movie.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

std::mutex g_gateMutex;
std::condition_variable g_gateCondition;
bool g_gateActive = false;
int g_gateRemaining = 0;
int g_gateLastFrame = -1;

}

void SetPausedBit(bool paused)
{
	int pauseFlags = EmulationPaused;
	if (paused)
	{
		pauseFlags |= EMULATIONPAUSED_PAUSED;
	}
	else
	{
		pauseFlags &= ~EMULATIONPAUSED_PAUSED;
	}
	FCEUI_SetEmulationPaused(pauseFlags);
}

bool IsFrameGateActive()
{
	std::lock_guard<std::mutex> lock(g_gateMutex);
	return g_gateActive;
}

void CancelFrameGate()
{
	{
		std::lock_guard<std::mutex> lock(g_gateMutex);
		g_gateActive = false;
		g_gateRemaining = 0;
		g_gateLastFrame = -1;
	}
	g_gateCondition.notify_all();
}

void BeginFrameGate(int frames)
{
	{
		std::lock_guard<std::mutex> lock(g_gateMutex);
		g_gateActive = true;
		g_gateRemaining = frames;
		g_gateLastFrame = currFrameCounter;
	}
	SetPausedBit(false);
	g_gateCondition.notify_all();
}

void WaitFrameGate()
{
	std::unique_lock<std::mutex> lock(g_gateMutex);
	while (g_gateActive)
	{
		g_gateCondition.wait_for(lock, std::chrono::milliseconds(2));
		lock.unlock();
		QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
		lock.lock();
	}
}

bool WaitRunUntilBreakOrGate()
{
	while (true)
	{
		if (FCEUXBridge::HasBreakHit())
		{
			CancelFrameGate();
			return true;
		}
		if (!IsFrameGateActive())
		{
			return FCEUXBridge::HasBreakHit();
		}
		QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
}

void NotifyFrameCompleted(int frame)
{
	bool completed = false;
	{
		std::lock_guard<std::mutex> lock(g_gateMutex);
		if (!g_gateActive)
		{
			return;
		}
		if (frame == g_gateLastFrame)
		{
			return;
		}
		g_gateLastFrame = frame;
		if (g_gateRemaining > 0)
		{
			g_gateRemaining--;
		}
		if (g_gateRemaining <= 0)
		{
			g_gateActive = false;
			g_gateRemaining = 0;
			completed = true;
		}
	}

	if (completed)
	{
		SetPausedBit(true);
		g_gateCondition.notify_all();
	}
}

}
