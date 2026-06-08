#include "bridge/BridgeJson.h"

namespace FCEUXBridge
{

QString JsonEscape(const QString& value)
{
	QString out;
	out.reserve(value.size() + 8);
	for (QChar c : value)
	{
		switch (c.unicode())
		{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c.unicode() < 0x20)
				{
					out += QString("\\u%1").arg(c.unicode(), 4, 16, QLatin1Char('0'));
				}
				else
				{
					out += c;
				}
				break;
		}
	}
	return out;
}

QString JsonString(const QString& value)
{
	return QString("\"%1\"").arg(JsonEscape(value));
}

QString JsonRawBytes(const uint8* bytes, int len)
{
	QStringList parts;
	parts.reserve(len);
	for (int i = 0; i < len; i++)
	{
		parts.append(QString::number(static_cast<uint>(bytes[i])));
	}
	return QString("[%1]").arg(parts.join(','));
}

QString JsonStringArray(const QStringList& values)
{
	QStringList parts;
	parts.reserve(values.size());
	for (const QString& value : values)
	{
		parts.append(JsonString(value));
	}
	return QString("[%1]").arg(parts.join(','));
}

}
