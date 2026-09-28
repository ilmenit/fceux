#pragma once

#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

uint8 ApplyJoypad(int which, uint8 physical);
void SetJoypad(int which, uint8 mask);
void ClearJoypad(int which);
void ClearAllJoypads();
uint8 GetJoypad(int which);
bool ButtonMaskFromName(const QString& name, uint8* bit);
QString JoypadJson(uint8 mask);
QString JoySetResponse(int port, const QStringList& buttons);
QString JoyClearResponse(int port);
QString JoyClearAllResponse();
QString InputStateResponse();
QString JoySetCommandResponse(const QStringList& parts);
QString JoyClearCommandResponse(const QStringList& parts);
QString InputStateCommandResponse(const QStringList& parts);

}
