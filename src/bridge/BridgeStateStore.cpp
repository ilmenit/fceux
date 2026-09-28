#include "bridge/BridgeStateStore.h"

#include <QFile>
#include <QHash>
#include <QIODevice>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "emufile.h"
#include "state.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QHash<QString, QByteArray> g_stateSlots;

bool parseBoolText(const QString& text, bool* value)
{
	const QString normalized = text.trimmed().toLower();
	if (normalized == QStringLiteral("true") || normalized == QStringLiteral("1") || normalized == QStringLiteral("yes") || normalized == QStringLiteral("on"))
	{
		*value = true;
		return true;
	}
	if (normalized == QStringLiteral("false") || normalized == QStringLiteral("0") || normalized == QStringLiteral("no") || normalized == QStringLiteral("off"))
	{
		*value = false;
		return true;
	}
	return false;
}

}

bool SaveCurrentState(QByteArray* bytes, QString* error)
{
	if (bytes == nullptr)
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("missing state output buffer");
		}
		return false;
	}

	EMUFILE_MEMORY em;
	if (!FCEUSS_SaveMS(&em, 0))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("failed to save state");
		}
		return false;
	}

	*bytes = QByteArray(reinterpret_cast<const char*>(em.buf()), static_cast<int>(em.size()));
	return true;
}

bool LoadStateBytes(const QByteArray& bytes, QString* error)
{
	if (bytes.isEmpty())
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("empty state payload");
		}
		return false;
	}

	QByteArray mutableBytes = bytes;
	EMUFILE_MEMORY em(mutableBytes.data(), static_cast<size_t>(mutableBytes.size()));
	if (!FCEUSS_LoadFP(&em, SSLOADPARAM_NOBACKUP))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("failed to load state");
		}
		return false;
	}

	return true;
}

void StoreStateSlot(const QString& slot, const QByteArray& bytes)
{
	g_stateSlots[slot] = bytes;
}

bool LoadStateSlot(const QString& slot, QByteArray* bytes)
{
	if (bytes == nullptr || !g_stateSlots.contains(slot))
	{
		return false;
	}
	*bytes = g_stateSlots[slot];
	return true;
}

QStringList StateSlotNames()
{
	return g_stateSlots.keys();
}

bool DropStateSlot(const QString& slot)
{
	return g_stateSlots.remove(slot) > 0;
}

void ClearStateSlots()
{
	g_stateSlots.clear();
}

QString StateSaveResponse(const QString& slot, const QString& path, bool includeInline)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QByteArray bytes;
	QString stateError;
	if (!SaveCurrentState(&bytes, &stateError))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(stateError));
	}
	if (!slot.isEmpty())
	{
		StoreStateSlot(slot, bytes);
	}
	if (!path.isEmpty())
	{
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
		{
			return QString("{\"ok\":false,\"error\":%1,\"path\":%2}")
				.arg(JsonString(file.errorString().isEmpty() ? QStringLiteral("failed to write state file") : file.errorString()))
				.arg(JsonString(path));
		}
	}
	return QString("{\"ok\":true,\"slot\":%1,\"path\":%2,\"bytes\":%3,\"base64\":%4}")
		.arg(slot.isEmpty() ? QStringLiteral("null") : JsonString(slot))
		.arg(path.isEmpty() ? QStringLiteral("null") : JsonString(path))
		.arg(bytes.size())
		.arg(includeInline ? JsonString(QString::fromLatin1(bytes.toBase64())) : QStringLiteral("null"));
}

QString StateLoadResponse(const QString& slot, const QString& path, const QString& data)
{
	QByteArray bytes;
	if (!slot.isEmpty())
	{
		if (!LoadStateSlot(slot, &bytes))
		{
			return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("unknown state slot: %1").arg(slot)));
		}
	}
	else if (!path.isEmpty())
	{
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			return QString("{\"ok\":false,\"error\":%1,\"path\":%2}")
				.arg(JsonString(file.errorString()))
				.arg(JsonString(path));
		}
		bytes = file.readAll();
	}
	else
	{
		QString payload = data;
		if (payload.startsWith(QStringLiteral("base64:"), Qt::CaseInsensitive))
		{
			payload = payload.mid(7);
		}
		const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(payload.toLatin1());
		if (!decoded)
		{
			return QStringLiteral("{\"ok\":false,\"error\":\"invalid state base64 payload\"}");
		}
		bytes = decoded.decoded;
	}
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}
	QString stateError;
	if (!LoadStateBytes(bytes, &stateError))
	{
		return QString("{\"ok\":false,\"error\":%1}").arg(JsonString(stateError));
	}
	return QString("{\"ok\":true,\"slot\":%1,\"path\":%2,\"bytes\":%3}")
		.arg(slot.isEmpty() ? QStringLiteral("null") : JsonString(slot))
		.arg(path.isEmpty() ? QStringLiteral("null") : JsonString(path))
		.arg(bytes.size());
}

QString StateListResponse()
{
	const QStringList slotNames = StateSlotNames();
	return QString("{\"ok\":true,\"slots\":%1}").arg(JsonStringArray(slotNames));
}

QString StateDropResponse(const QString& slot)
{
	const bool removed = DropStateSlot(slot);
	return QString("{\"ok\":true,\"slot\":%1,\"removed\":%2}")
		.arg(JsonString(slot))
		.arg(removed ? "true" : "false");
}

QString StateDropAllResponse()
{
	ClearStateSlots();
	return QStringLiteral("{\"ok\":true,\"dropped\":\"all\"}");
}

QString StateSaveCommandResponse(const QStringList& parts)
{
	const QString slot = OptionValue(parts, QStringLiteral("slot"));
	const QString path = OptionValue(parts, QStringLiteral("path"));
	bool includeInline = false;
	const QString inlineText = OptionValue(parts, QStringLiteral("inline"));
	if (!inlineText.isEmpty() && !parseBoolText(inlineText, &includeInline))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid inline value\"}");
	}
	if (slot.isEmpty() && path.isEmpty() && !includeInline)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STATE_SAVE requires slot=NAME, path=FILE, or inline=true\"}");
	}
	return StateSaveResponse(slot, path, includeInline);
}

QString StateLoadCommandResponse(const QStringList& parts)
{
	const QString slot = OptionValue(parts, QStringLiteral("slot"));
	const QString path = OptionValue(parts, QStringLiteral("path"));
	const QString data = OptionValue(parts, QStringLiteral("data"));
	const int sourceCount = (slot.isEmpty() ? 0 : 1) + (path.isEmpty() ? 0 : 1) + (data.isEmpty() ? 0 : 1);
	if (sourceCount != 1)
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STATE_LOAD requires exactly one of slot=NAME, path=FILE, or data=base64\"}");
	}
	return StateLoadResponse(slot, path, data);
}

QString StateListCommandResponse(const QStringList& parts)
{
	Q_UNUSED(parts);
	return StateListResponse();
}

QString StateDropCommandResponse(const QStringList& parts)
{
	if (parts.size() == 2 && parts[1] == QStringLiteral("all=true"))
	{
		return StateDropAllResponse();
	}
	const QString slot = OptionValue(parts, QStringLiteral("slot"));
	if (slot.isEmpty())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"STATE_DROP requires slot=NAME or all=true\"}");
	}
	return StateDropResponse(slot);
}

}
