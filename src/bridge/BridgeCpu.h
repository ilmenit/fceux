#pragma once

#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

QString CpuRegsResponse();
QString CpuRegSetResponse(const QString& reg, uint value);
QString CpuDisasmResponse(uint addr, int count);
QString CpuCallStackResponse(int count);
QString CpuRegSetCommandResponse(const QStringList& parts);
QString CpuDisasmCommandResponse(const QStringList& parts);
QString CpuCallStackCommandResponse(const QStringList& parts);

}
