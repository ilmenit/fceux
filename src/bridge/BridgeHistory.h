#pragma once

#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

void StartHistory();
void StopHistory();
void ClearHistory();
bool HistoryEnabled();
bool ConfigureHistory(int capacity, bool enabled, QString* error);
QString HistoryStatusJson();
QString HistoryJson(int count, bool includeDisasm = true);
QString HistoryResponse(const QStringList& parts);
QString HistoryClearResponse(const QStringList& parts);
QString HistoryConfigResponse(const QStringList& parts);
QString TraceStartResponse(const QStringList& parts);
QString TraceStopResponse(const QStringList& parts);
QString TraceStatusResponse(const QStringList& parts);

}
