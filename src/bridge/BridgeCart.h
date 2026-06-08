#pragma once

#include <QString>
#include <QStringList>

#include "types.h"

namespace FCEUXBridge
{

QString CartInfoResponse();
QString BankInfoResponse(bool singleAddress, uint addr);
QString MemMapResponse();
QString RomBytesResponse(const QString& domain, uint offset, uint len);
QString CartInfoCommandResponse(const QStringList& parts);
QString BankInfoCommandResponse(const QStringList& parts);
QString MemMapCommandResponse(const QStringList& parts);
QString RomBytesCommandResponse(const QString& cmd, const QStringList& parts);

}
