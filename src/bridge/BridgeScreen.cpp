#include "bridge/BridgeScreen.h"

#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QIODevice>

#include "bridge/BridgeJson.h"
#include "bridge/BridgeProtocol.h"
#include "driver.h"
#include "video.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

QByteArray rawScreenRgba(bool overlay)
{
	QByteArray out;
	out.resize(256 * 240 * 4);
	uint8* src = overlay ? XBuf : XBackBuf;
	if (src == nullptr)
	{
		out.fill(0);
		return out;
	}

	unsigned char* dst = reinterpret_cast<unsigned char*>(out.data());
	for (int y = 0; y < 240; y++)
	{
		for (int x = 0; x < 256; x++)
		{
			uint8 r = 0, g = 0, b = 0;
			FCEUD_GetPalette(src[(y * 256) + x], &r, &g, &b);
			*dst++ = r;
			*dst++ = g;
			*dst++ = b;
			*dst++ = 255;
		}
	}
	return out;
}

bool writeRawScreenFile(const QString& path, const QByteArray& rgba, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		*error = file.errorString();
		return false;
	}
	if (file.write(rgba) != rgba.size())
	{
		*error = file.errorString().isEmpty() ? QStringLiteral("short write") : file.errorString();
		return false;
	}
	return true;
}

bool writeScreenshotPng(const QString& path, const QByteArray& rgba, QString* error)
{
	QImage image(reinterpret_cast<const uchar*>(rgba.constData()), 256, 240, 256 * 4, QImage::Format_RGBA8888);
	if (!image.save(path, "PNG"))
	{
		*error = QStringLiteral("failed to save PNG");
		return false;
	}
	return true;
}

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

QString ScreenResponse(bool screenshot, bool overlay, bool includeInline, const QString& path)
{
	FCEU_CRITICAL_SECTION(lock);
	if (!fceuWrapperGameLoaded())
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"no game loaded\"}");
	}

	const QByteArray rgba = rawScreenRgba(overlay);
	if (!path.isEmpty())
	{
		QString error;
		const bool saved = screenshot
			? writeScreenshotPng(path, rgba, &error)
			: writeRawScreenFile(path, rgba, &error);
		if (!saved)
		{
			return QString("{\"ok\":false,\"error\":%1,\"path\":%2}")
				.arg(JsonString(error))
				.arg(JsonString(path));
		}
	}

	return QString("{\"ok\":true,\"width\":256,\"height\":240,\"stride\":1024,\"format\":\"rgba8888\",\"overlay\":%1,\"path\":%2,\"base64\":%3}")
		.arg(overlay ? "true" : "false")
		.arg(path.isEmpty() ? QStringLiteral("null") : JsonString(path))
		.arg(includeInline ? JsonString(QString::fromLatin1(rgba.toBase64())) : QStringLiteral("null"));
}

QString ScreenCommandResponse(const QString& cmd, const QStringList& parts)
{
	if (parts.size() > 4)
	{
		return QString("{\"ok\":false,\"error\":%1}")
			.arg(JsonString(QString("%1 accepts overlay=true|false, inline=true|false, and path=FILE").arg(cmd)));
	}
	bool overlay = false;
	const QString overlayText = OptionValue(parts, QStringLiteral("overlay"));
	if (!overlayText.isEmpty() && !parseBoolText(overlayText, &overlay))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid overlay value\"}");
	}
	const QString path = OptionValue(parts, QStringLiteral("path"));
	bool includeInline = path.isEmpty();
	const QString inlineText = OptionValue(parts, QStringLiteral("inline"));
	if (!inlineText.isEmpty() && !parseBoolText(inlineText, &includeInline))
	{
		return QStringLiteral("{\"ok\":false,\"error\":\"invalid inline value\"}");
	}
	return ScreenResponse(cmd == QStringLiteral("SCREENSHOT"), overlay, includeInline, path);
}

}
