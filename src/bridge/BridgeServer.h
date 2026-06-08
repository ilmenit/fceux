#pragma once

#include <QString>

namespace FCEUXBridge
{

struct BridgeConfig
{
	bool enabled = false;
	bool headless = false;
	QString address;
	QString initialRom;
};

void PreParseArgs(int* argc, char** argv);
const BridgeConfig& GetConfig();

bool Start();
void Stop();
void NotifyFrameCompleted(int frame);

}
