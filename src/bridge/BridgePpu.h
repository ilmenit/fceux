#pragma once

#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

uint8 PpuRead(uint address);

QString PpuStateResponse();
QString PpuPeekResponse(uint addr, uint len);
QString PpuPokeResponse(uint addr, uint value);
QString PpuDumpResponse(const QString& domain, uint start, uint len);
QString OamDumpResponse();
QString OamPokeResponse(uint addr, uint value);
QString PaletteDumpResponse();
QString PpuStateCommandResponse(const QStringList& parts);
QString PpuPeekCommandResponse(const QStringList& parts);
QString PpuPokeCommandResponse(const QStringList& parts);
QString PpuDumpCommandResponse(const QStringList& parts);
QString OamDumpCommandResponse(const QStringList& parts);
QString OamPokeCommandResponse(const QStringList& parts);
QString PaletteDumpCommandResponse(const QStringList& parts);

}
