#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

void NotifyBreakHit(int slot);
void ClearBreakStatus();
bool HasBreakHit();

bool AddBreakpoint(const QString& domain, const QString& mode, uint addr, int end, const QString& condition, const QString& name, bool enabled, QString* response, QString* error);
QString BreakpointListJson();
bool ClearBreakpoint(int bridgeId, QString* error);
void ClearAllBreakpoints();
QString BreakStatusJson();
QString BreakpointSetResponse(const QString& cmd, const QStringList& parts);
QString BreakpointListResponse();
QString BreakpointClearResponse(const QStringList& parts);
QString BreakpointClearAllResponse();
QString BreakStatusResponse();

}
