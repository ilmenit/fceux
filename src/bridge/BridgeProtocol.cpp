#include "bridge/BridgeProtocol.h"

namespace FCEUXBridge
{

bool TokenizeCommand(const QString& line, QStringList* out, QString* error)
{
	QString current;
	bool inQuote = false;
	bool escaping = false;

	for (QChar c : line)
	{
		if (escaping)
		{
			current += c;
			escaping = false;
			continue;
		}
		if (c == '\\')
		{
			escaping = true;
			continue;
		}
		if (c == '"')
		{
			inQuote = !inQuote;
			continue;
		}
		if (c.isSpace() && !inQuote)
		{
			if (!current.isEmpty())
			{
				out->append(current);
				current.clear();
			}
			continue;
		}
		current += c;
	}

	if (escaping)
	{
		*error = QStringLiteral("dangling escape");
		return false;
	}
	if (inQuote)
	{
		*error = QStringLiteral("unterminated quote");
		return false;
	}
	if (!current.isEmpty())
	{
		out->append(current);
	}
	return true;
}

bool ParseNumber(const QString& text, uint* value)
{
	QString s = text.trimmed();
	int base = 10;
	if (s.startsWith('$'))
	{
		s = s.mid(1);
		base = 16;
	}
	else if (s.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
	{
		s = s.mid(2);
		base = 16;
	}

	bool ok = false;
	const uint parsed = s.toUInt(&ok, base);
	if (!ok)
	{
		return false;
	}
	*value = parsed;
	return true;
}

QString OptionValue(const QStringList& parts, const QString& name)
{
	const QString prefix = name + '=';
	for (int i = 1; i < parts.size(); i++)
	{
		if (parts[i].startsWith(prefix, Qt::CaseInsensitive))
		{
			return parts[i].mid(prefix.size());
		}
	}
	return QString();
}

}
