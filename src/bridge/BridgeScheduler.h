#pragma once

namespace FCEUXBridge
{

void SetPausedBit(bool paused);
bool IsFrameGateActive();
void CancelFrameGate();
void BeginFrameGate(int frames);
void WaitFrameGate();
bool WaitRunUntilBreakOrGate();
void NotifyFrameCompleted(int frame);

}
