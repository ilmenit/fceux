#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace FCEUXBridge
{

bool SaveCurrentState(QByteArray* bytes, QString* error);
bool LoadStateBytes(const QByteArray& bytes, QString* error);

void StoreStateSlot(const QString& slot, const QByteArray& bytes);
bool LoadStateSlot(const QString& slot, QByteArray* bytes);
QStringList StateSlotNames();
bool DropStateSlot(const QString& slot);
void ClearStateSlots();

QString StateSaveResponse(const QString& slot, const QString& path, bool includeInline);
QString StateLoadResponse(const QString& slot, const QString& path, const QString& data);
QString StateListResponse();
QString StateDropResponse(const QString& slot);
QString StateDropAllResponse();
QString StateSaveCommandResponse(const QStringList& parts);
QString StateLoadCommandResponse(const QStringList& parts);
QString StateListCommandResponse(const QStringList& parts);
QString StateDropCommandResponse(const QStringList& parts);

}
