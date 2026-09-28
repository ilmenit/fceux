#include "bridge/BridgeServer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QSet>
#include <QSocketNotifier>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>

#include <cstdio>
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __linux__
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include "driver.h"
#include "bridge/BridgeApu.h"
#include "bridge/BridgeCart.h"
#include "bridge/BridgeCdLog.h"
#include "bridge/BridgeCpu.h"
#include "bridge/BridgeDebugger.h"
#include "bridge/BridgeExecution.h"
#include "bridge/BridgeHistory.h"
#include "bridge/BridgeInput.h"
#include "bridge/BridgeJson.h"
#include "bridge/BridgeLifecycle.h"
#include "bridge/BridgeLua.h"
#include "bridge/BridgeMemory.h"
#include "bridge/BridgePpu.h"
#include "bridge/BridgeProtocol.h"
#include "bridge/BridgeScheduler.h"
#include "bridge/BridgeScreen.h"
#include "bridge/BridgeStateStore.h"
#include "bridge/BridgeSymbols.h"
#include "asm.h"
#include "fceu.h"
#include "cart.h"
#include "debug.h"
#include "movie.h"
#include "ppu.h"
#include "version.h"
#include "x6502.h"
#include "Qt/dface.h"
#include "Qt/fceuWrapper.h"

namespace FCEUXBridge
{

namespace
{

BridgeConfig g_config;

QString makeToken()
{
	QString token;
	token.reserve(32);
	for (int i = 0; i < 4; i++)
	{
		token += QString("%1").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
	}
	return token;
}

QString defaultAddress()
{
	return QStringLiteral("tcp:127.0.0.1:0");
}

bool parseTcpAddress(const QString& address, QHostAddress* host, quint16* port)
{
	QString spec = address;
	if (spec.isEmpty() || spec == QStringLiteral("tcp"))
	{
		spec = defaultAddress();
	}
	if (!spec.startsWith(QStringLiteral("tcp:")))
	{
		return false;
	}

	const QString rest = spec.mid(4);
	const int lastColon = rest.lastIndexOf(':');
	if (lastColon <= 0)
	{
		return false;
	}

	bool ok = false;
	const uint parsedPort = rest.mid(lastColon + 1).toUInt(&ok);
	if (!ok || parsedPort > 65535)
	{
		return false;
	}

	const QString hostText = rest.left(lastColon);
	QHostAddress parsedHost(hostText);
	if (parsedHost.isNull())
	{
		return false;
	}
	if (parsedHost != QHostAddress::LocalHost && parsedHost != QHostAddress::LocalHostIPv6)
	{
		return false;
	}

	*host = parsedHost;
	*port = static_cast<quint16>(parsedPort);
	return true;
}

bool parseUnixAddress(const QString& address, QString* path)
{
	QString spec = address;
	if (spec == QStringLiteral("unix"))
	{
		spec = QStringLiteral("unix:");
	}
	if (!spec.startsWith(QStringLiteral("unix:")))
	{
		return false;
	}
	QString parsed = spec.mid(5);
	if (parsed.isEmpty())
	{
		parsed = QDir::tempPath() + QString("/fceux-bridge-%1.sock").arg(QCoreApplication::applicationPid());
	}
	if (!parsed.startsWith('/'))
	{
		return false;
	}
	*path = parsed;
	return true;
}

bool parseUnixAbstractAddress(const QString& address, QString* name)
{
	if (!address.startsWith(QStringLiteral("unix-abstract:"), Qt::CaseInsensitive))
	{
		return false;
	}
	const QString parsed = address.mid(QStringLiteral("unix-abstract:").size());
	if (parsed.isEmpty() || parsed.contains(QChar('\0')))
	{
		return false;
	}
	*name = parsed;
	return true;
}

#ifndef _WIN32
class StdioDevice : public QIODevice
{
	public:
		explicit StdioDevice(QObject* parent = nullptr)
			: QIODevice(parent)
		{
			writeFd = ::dup(STDOUT_FILENO);
			if (writeFd >= 0)
			{
				::dup2(STDERR_FILENO, STDOUT_FILENO);
			}
			const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
			if (flags >= 0)
			{
				::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
			}
			notifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
			QObject::connect(notifier, &QSocketNotifier::activated, this, [this]() {
				readFromStdin();
			});
			open(QIODevice::ReadWrite | QIODevice::Unbuffered);
		}

		~StdioDevice() override
		{
			if (writeFd >= 0)
			{
				::close(writeFd);
				writeFd = -1;
			}
		}

		bool isSequential() const override
		{
			return true;
		}

		qint64 bytesAvailable() const override
		{
			return pending.size() + QIODevice::bytesAvailable();
		}

	protected:
		qint64 readData(char* data, qint64 maxSize) override
		{
			const qint64 count = std::min<qint64>(maxSize, pending.size());
			if (count <= 0)
			{
				return 0;
			}
			std::memcpy(data, pending.constData(), static_cast<size_t>(count));
			pending.remove(0, static_cast<int>(count));
			return count;
		}

		qint64 writeData(const char* data, qint64 maxSize) override
		{
			qint64 written = 0;
			while (written < maxSize)
			{
				const ssize_t count = ::write(writeFd, data + written, static_cast<size_t>(maxSize - written));
				if (count < 0)
				{
					if (errno == EINTR)
					{
						continue;
					}
					return written > 0 ? written : -1;
				}
				written += static_cast<qint64>(count);
			}
			return written;
		}

	private:
		void readFromStdin()
		{
			char buffer[4096];
			bool gotData = false;
			while (true)
			{
				const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
				if (count > 0)
				{
					pending.append(buffer, static_cast<int>(count));
					gotData = true;
					continue;
				}
				if (count == 0)
				{
					close();
				}
				else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
				{
					close();
				}
				break;
			}
			if (gotData)
			{
				emit readyRead();
			}
		}

		QByteArray pending;
		int writeFd = -1;
		QSocketNotifier* notifier = nullptr;
};
#endif

class BridgeServer
{
	public:
		explicit BridgeServer(const BridgeConfig& config)
			: config(config)
		{
		}

		~BridgeServer()
		{
			stop();
		}

		bool start()
		{
			token = makeToken();
			if (config.address.compare(QStringLiteral("stdio"), Qt::CaseInsensitive) == 0)
			{
#ifndef _WIN32
				StdioDevice* stdio = new StdioDevice();
				if (!stdio->isOpen())
				{
					fprintf(stderr, "[bridge] stdio transport failed\n");
					delete stdio;
					return false;
				}
				registerSocket(stdio);
				if (!writeTokenFile(QStringLiteral("stdio")))
				{
					stop();
					return false;
				}
				fprintf(stderr, "[bridge] listening on stdio\n");
				fprintf(stderr, "[bridge] token-file: %s\n", tokenFilePath.toLocal8Bit().constData());
				return true;
#else
				fprintf(stderr, "[bridge] stdio transport is not supported on Windows\n");
				return false;
#endif
			}
			if (config.address.startsWith(QStringLiteral("unix-abstract:"), Qt::CaseInsensitive))
			{
#ifdef __linux__
				QString socketName;
				if (!parseUnixAbstractAddress(config.address, &socketName))
				{
					fprintf(stderr, "[bridge] unsupported abstract unix address: %s\n", config.address.toLocal8Bit().constData());
					return false;
				}
				if (!startAbstractLocalServer(socketName))
				{
					return false;
				}
				const QString endpoint = QString("unix-abstract:%1").arg(socketName);
				if (!writeTokenFile(endpoint))
				{
					stop();
					return false;
				}
				fprintf(stderr, "[bridge] listening on %s\n", endpoint.toLocal8Bit().constData());
				fprintf(stderr, "[bridge] token-file: %s\n", tokenFilePath.toLocal8Bit().constData());
				return true;
#else
				fprintf(stderr, "[bridge] unix-abstract endpoints are only supported on Linux\n");
				return false;
#endif
			}
			if (config.address.startsWith(QStringLiteral("unix:"), Qt::CaseInsensitive) || config.address == QStringLiteral("unix"))
			{
				QString socketPath;
				if (!parseUnixAddress(config.address, &socketPath))
				{
					fprintf(stderr, "[bridge] unsupported unix address: %s\n", config.address.toLocal8Bit().constData());
					return false;
				}
				QLocalServer::removeServer(socketPath);
				localServer = new QLocalServer();
				if (!localServer->listen(socketPath))
				{
					fprintf(stderr, "[bridge] listen failed: %s\n", localServer->errorString().toLocal8Bit().constData());
					delete localServer;
					localServer = nullptr;
					return false;
				}

				QObject::connect(localServer, &QLocalServer::newConnection, localServer, [this]() {
					acceptLocalConnections();
				});

				unixSocketPath = socketPath;
				QFile::setPermissions(socketPath, QFile::ReadOwner | QFile::WriteOwner);
				const QString endpoint = QString("unix:%1").arg(socketPath);
				if (!writeTokenFile(endpoint))
				{
					stop();
					return false;
				}

				fprintf(stderr, "[bridge] listening on %s\n", endpoint.toLocal8Bit().constData());
				fprintf(stderr, "[bridge] token-file: %s\n", tokenFilePath.toLocal8Bit().constData());
				return true;
			}

			QHostAddress host;
			quint16 port = 0;
			if (!parseTcpAddress(config.address, &host, &port))
			{
				fprintf(stderr, "[bridge] unsupported address: %s\n", config.address.toLocal8Bit().constData());
				return false;
			}

			tcpServer = new QTcpServer();
			if (!tcpServer->listen(host, port))
			{
				fprintf(stderr, "[bridge] listen failed: %s (error=%d)\n",
					tcpServer->errorString().toLocal8Bit().constData(),
					static_cast<int>(tcpServer->serverError()));
				delete tcpServer;
				tcpServer = nullptr;
				return false;
			}

			QObject::connect(tcpServer, &QTcpServer::newConnection, tcpServer, [this]() {
				acceptTcpConnections();
			});

			const QString endpoint = QString("tcp:%1:%2").arg(tcpServer->serverAddress().toString()).arg(tcpServer->serverPort());
			if (!writeTokenFile(endpoint))
			{
				stop();
				return false;
			}

			fprintf(stderr, "[bridge] listening on %s\n", endpoint.toLocal8Bit().constData());
			fprintf(stderr, "[bridge] token-file: %s\n", tokenFilePath.toLocal8Bit().constData());
			return true;
		}

		void stop()
		{
			for (QIODevice* socket : sockets)
			{
				disconnectSocket(socket);
				socket->deleteLater();
			}
			sockets.clear();
			authenticated.clear();
			buffers.clear();
			FCEUXBridge::ClearAllJoypads();

			if (tcpServer != nullptr)
			{
				tcpServer->close();
				tcpServer->deleteLater();
				tcpServer = nullptr;
			}
			if (localServer != nullptr)
			{
				localServer->close();
				localServer->deleteLater();
				localServer = nullptr;
			}
			if (!unixSocketPath.isEmpty())
			{
				QLocalServer::removeServer(unixSocketPath);
				unixSocketPath.clear();
			}
#ifdef __linux__
			if (abstractNotifier != nullptr)
			{
				abstractNotifier->setEnabled(false);
				abstractNotifier->deleteLater();
				abstractNotifier = nullptr;
			}
			if (abstractListenFd >= 0)
			{
				::close(abstractListenFd);
				abstractListenFd = -1;
			}
#endif

			if (!tokenFilePath.isEmpty())
			{
				QFile::remove(tokenFilePath);
				tokenFilePath.clear();
			}
		}

	private:
		bool writeTokenFile(const QString& endpoint)
		{
			const QString path = QDir::tempPath() + QString("/fceux-bridge-%1.token").arg(QCoreApplication::applicationPid());
			QFile file(path);
			if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
			{
				fprintf(stderr, "[bridge] token file failed: %s\n", file.errorString().toLocal8Bit().constData());
				return false;
			}

			QTextStream out(&file);
			out << endpoint << '\n' << token << '\n';
			file.close();
			QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
			tokenFilePath = path;
			return true;
		}

		void registerSocket(QIODevice* socket)
		{
			sockets.insert(socket);
			authenticated.insert(socket, false);
			buffers.insert(socket, QByteArray());

			QObject::connect(socket, &QIODevice::readyRead, socket, [this, socket]() {
				readSocket(socket);
			});
			if (QTcpSocket* tcpSocket = qobject_cast<QTcpSocket*>(socket))
			{
				QObject::connect(tcpSocket, &QTcpSocket::disconnected, tcpSocket, [this, socket]() {
					forgetSocket(socket);
				});
			}
			else if (QLocalSocket* localSocket = qobject_cast<QLocalSocket*>(socket))
			{
				QObject::connect(localSocket, &QLocalSocket::disconnected, localSocket, [this, socket]() {
					forgetSocket(socket);
				});
			}
		}

		void acceptTcpConnections()
		{
			while (tcpServer != nullptr && tcpServer->hasPendingConnections())
			{
				registerSocket(tcpServer->nextPendingConnection());
			}
		}

		void acceptLocalConnections()
		{
			while (localServer != nullptr && localServer->hasPendingConnections())
			{
				registerSocket(localServer->nextPendingConnection());
			}
		}

#ifdef __linux__
		bool startAbstractLocalServer(const QString& socketName)
		{
			const QByteArray nameBytes = socketName.toUtf8();
			if (nameBytes.size() > static_cast<int>(sizeof(sockaddr_un::sun_path) - 2))
			{
				fprintf(stderr, "[bridge] abstract socket name too long\n");
				return false;
			}

			abstractListenFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
			if (abstractListenFd < 0)
			{
				fprintf(stderr, "[bridge] abstract socket failed: %s\n", strerror(errno));
				return false;
			}

			sockaddr_un addr;
			std::memset(&addr, 0, sizeof(addr));
			addr.sun_family = AF_UNIX;
			addr.sun_path[0] = '\0';
			std::memcpy(addr.sun_path + 1, nameBytes.constData(), static_cast<size_t>(nameBytes.size()));
			const socklen_t addrLen = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + nameBytes.size());
			if (::bind(abstractListenFd, reinterpret_cast<sockaddr*>(&addr), addrLen) < 0)
			{
				fprintf(stderr, "[bridge] abstract bind failed: %s\n", strerror(errno));
				::close(abstractListenFd);
				abstractListenFd = -1;
				return false;
			}
			if (::listen(abstractListenFd, 16) < 0)
			{
				fprintf(stderr, "[bridge] abstract listen failed: %s\n", strerror(errno));
				::close(abstractListenFd);
				abstractListenFd = -1;
				return false;
			}

			abstractNotifier = new QSocketNotifier(abstractListenFd, QSocketNotifier::Read);
			QObject::connect(abstractNotifier, &QSocketNotifier::activated, abstractNotifier, [this]() {
				acceptAbstractConnections();
			});
			return true;
		}

		void acceptAbstractConnections()
		{
			if (abstractNotifier != nullptr)
			{
				abstractNotifier->setEnabled(false);
			}
			while (abstractListenFd >= 0)
			{
				const int fd = ::accept4(abstractListenFd, nullptr, nullptr, SOCK_NONBLOCK);
				if (fd < 0)
				{
					if (errno != EAGAIN && errno != EWOULDBLOCK)
					{
						fprintf(stderr, "[bridge] abstract accept failed: %s\n", strerror(errno));
					}
					break;
				}
				QLocalSocket* socket = new QLocalSocket();
				if (!socket->setSocketDescriptor(fd, QLocalSocket::ConnectedState, QIODevice::ReadWrite))
				{
					::close(fd);
					socket->deleteLater();
					continue;
				}
				registerSocket(socket);
			}
			if (abstractNotifier != nullptr)
			{
				abstractNotifier->setEnabled(true);
			}
		}
#endif

		void forgetSocket(QIODevice* socket)
		{
			if (authenticated.value(socket, false))
			{
				FCEUXBridge::ClearAllJoypads();
			}
			sockets.remove(socket);
			authenticated.remove(socket);
			buffers.remove(socket);
			socket->deleteLater();
		}

		void disconnectSocket(QIODevice* socket)
		{
			if (authenticated.value(socket, false))
			{
				FCEUXBridge::ClearAllJoypads();
			}
			if (QTcpSocket* tcpSocket = qobject_cast<QTcpSocket*>(socket))
			{
				tcpSocket->disconnectFromHost();
			}
			else if (QLocalSocket* localSocket = qobject_cast<QLocalSocket*>(socket))
			{
				localSocket->disconnectFromServer();
			}
			else
			{
				socket->close();
			}
		}

		void readSocket(QIODevice* socket)
		{
			QByteArray buffer = buffers.value(socket);
			buffer += socket->readAll();

			int newline = -1;
			while ((newline = buffer.indexOf('\n')) >= 0)
			{
				QByteArray line = buffer.left(newline);
				buffer.remove(0, newline + 1);
				if (!line.isEmpty() && line.endsWith('\r'))
				{
					line.chop(1);
				}
				processLine(socket, QString::fromUtf8(line));
				if (!socket->isOpen())
				{
					break;
				}
			}

			buffers[socket] = buffer;
		}

		void processLine(QIODevice* socket, const QString& line)
		{
			const QString trimmed = line.trimmed();
			if (trimmed.isEmpty())
			{
				return;
			}

			QStringList parts;
			QString tokenError;
			if (!TokenizeCommand(trimmed, &parts, &tokenError) || parts.isEmpty())
			{
				writeLine(socket, QString("{\"ok\":false,\"error\":%1}").arg(JsonString(tokenError.isEmpty() ? QStringLiteral("empty command") : tokenError)));
				return;
			}
			const QString cmd = parts.value(0).toUpper();

			if (!authenticated.value(socket, false))
			{
				if (cmd != QStringLiteral("HELLO") || parts.size() != 2 || parts[1] != token)
				{
					writeLine(socket, QStringLiteral("{\"ok\":false,\"error\":\"auth required\"}"));
					disconnectSocket(socket);
					return;
				}
				authenticated[socket] = true;
				writeLine(socket, QString("{\"ok\":true,\"protocol\":1,\"server\":\"FCEUX\",\"version\":%1,\"paused\":%2}")
					.arg(JsonString(FCEU_VERSION_STRING))
					.arg(FCEUI_EmulationPaused() ? "true" : "false"));
				return;
			}

			if (cmd == QStringLiteral("PING"))
			{
				writeLine(socket, QStringLiteral("{\"ok\":true,\"pong\":true}"));
			}
			else if (cmd == QStringLiteral("STATUS"))
			{
				writeLine(socket, StatusResponse());
			}
			else if (cmd == QStringLiteral("PAUSE"))
			{
				writeLine(socket, PauseResponse());
			}
			else if (cmd == QStringLiteral("RESUME"))
			{
				writeLine(socket, ResumeResponse());
			}
			else if (cmd == QStringLiteral("FRAME"))
			{
				writeLine(socket, FrameCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("WAIT"))
			{
				writeLine(socket, WaitResponse());
			}
			else if (cmd == QStringLiteral("REGS"))
			{
				writeLine(socket, CpuRegsResponse());
			}
			else if (cmd == QStringLiteral("REG_SET"))
			{
				writeLine(socket, CpuRegSetCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("HISTORY"))
			{
				writeLine(socket, HistoryResponse(parts));
			}
			else if (cmd == QStringLiteral("HISTORY_CLEAR"))
			{
				writeLine(socket, HistoryClearResponse(parts));
			}
			else if (cmd == QStringLiteral("HISTORY_CONFIG"))
			{
				writeLine(socket, HistoryConfigResponse(parts));
			}
			else if (cmd == QStringLiteral("TRACE_START"))
			{
				writeLine(socket, TraceStartResponse(parts));
			}
			else if (cmd == QStringLiteral("TRACE_STOP"))
			{
				writeLine(socket, TraceStopResponse(parts));
			}
			else if (cmd == QStringLiteral("TRACE_STATUS"))
			{
				writeLine(socket, TraceStatusResponse(parts));
			}
			else if (cmd == QStringLiteral("BP_SET") || cmd == QStringLiteral("WATCH_SET"))
			{
				writeLine(socket, BreakpointSetResponse(cmd, parts));
			}
			else if (cmd == QStringLiteral("BP_LIST"))
			{
				writeLine(socket, BreakpointListResponse());
			}
			else if (cmd == QStringLiteral("BP_CLEAR"))
			{
				writeLine(socket, BreakpointClearResponse(parts));
			}
			else if (cmd == QStringLiteral("BP_CLEAR_ALL"))
			{
				writeLine(socket, BreakpointClearAllResponse());
			}
			else if (cmd == QStringLiteral("BREAK_STATUS"))
			{
				writeLine(socket, BreakStatusResponse());
			}
			else if (cmd == QStringLiteral("STEP"))
			{
				writeLine(socket, StepCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("STEP_OUT"))
			{
				writeLine(socket, StepOutCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("STEP_OVER"))
			{
				writeLine(socket, StepOverCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("DISASM"))
			{
				writeLine(socket, CpuDisasmCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("CALLSTACK"))
			{
				writeLine(socket, CpuCallStackCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("SYM_LOAD"))
			{
				writeLine(socket, SymbolLoadCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("SYM_RESOLVE"))
			{
				writeLine(socket, SymbolResolveCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("SYM_LOOKUP"))
			{
				writeLine(socket, SymbolLookupCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("LUA_EVAL"))
			{
				writeLine(socket, LuaEvalCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("LUA_LOAD"))
			{
				writeLine(socket, LuaLoadCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("LUA_RESET"))
			{
				writeLine(socket, LuaResetCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("LUA_STATUS"))
			{
				writeLine(socket, LuaStatusCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("RUN_UNTIL"))
			{
				writeLine(socket, RunUntilCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("MEMSEARCH"))
			{
				writeLine(socket, MemSearchCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("CART_INFO"))
			{
				writeLine(socket, CartInfoCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("BANK_INFO"))
			{
				writeLine(socket, BankInfoCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("MEMMAP"))
			{
				writeLine(socket, MemMapCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("ROM_PEEK") || cmd == QStringLiteral("ROM_DUMP"))
			{
				writeLine(socket, RomBytesCommandResponse(cmd, parts));
			}
			else if (cmd == QStringLiteral("PPU_STATE"))
			{
				writeLine(socket, PpuStateCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("APU_STATE"))
			{
				writeLine(socket, ApuStateCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("CDLOG_START"))
			{
				writeLine(socket, CdLogStartCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("CDLOG_STOP"))
			{
				writeLine(socket, CdLogStopCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("CDLOG_DUMP"))
			{
				writeLine(socket, CdLogDumpCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("PEEK") || cmd == QStringLiteral("MEMDUMP"))
			{
				writeLine(socket, CpuPeekCommandResponse(cmd, parts));
			}
			else if (cmd == QStringLiteral("PEEK16"))
			{
				writeLine(socket, CpuPeek16CommandResponse(parts));
			}
			else if (cmd == QStringLiteral("BUSPEEK"))
			{
				writeLine(socket, BusPeekCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("BUSPEEK16"))
			{
				writeLine(socket, BusPeek16CommandResponse(parts));
			}
			else if (cmd == QStringLiteral("PPU_PEEK"))
			{
				writeLine(socket, PpuPeekCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("PPU_POKE"))
			{
				writeLine(socket, PpuPokeCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("PPU_DUMP"))
			{
				writeLine(socket, PpuDumpCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("POKE") || cmd == QStringLiteral("POKE16"))
			{
				writeLine(socket, CpuPokeCommandResponse(cmd, parts));
			}
			else if (cmd == QStringLiteral("MEMLOAD"))
			{
				writeLine(socket, MemLoadCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("OAM_DUMP"))
			{
				writeLine(socket, OamDumpCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("OAM_POKE"))
			{
				writeLine(socket, OamPokeCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("PALETTE_DUMP"))
			{
				writeLine(socket, PaletteDumpCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("JOY"))
			{
				writeLine(socket, JoySetCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("JOY_CLEAR"))
			{
				writeLine(socket, JoyClearCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("INPUT_STATE"))
			{
				writeLine(socket, InputStateCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("RAWSCREEN") || cmd == QStringLiteral("SCREENSHOT"))
			{
				writeLine(socket, ScreenCommandResponse(cmd, parts));
			}
			else if (cmd == QStringLiteral("STATE_SAVE"))
			{
				writeLine(socket, StateSaveCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("STATE_LOAD"))
			{
				writeLine(socket, StateLoadCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("STATE_LIST"))
			{
				writeLine(socket, StateListCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("STATE_DROP"))
			{
				writeLine(socket, StateDropCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("LOAD_ROM"))
			{
				writeLine(socket, LoadRomCommandResponse(parts));
			}
			else if (cmd == QStringLiteral("RESET"))
			{
				writeLine(socket, ResetResponse());
			}
			else if (cmd == QStringLiteral("POWER"))
			{
				writeLine(socket, PowerResponse());
			}
			else if (cmd == QStringLiteral("APP_EXIT"))
			{
				writeLine(socket, AppExitResponse());
			}
			else if (cmd == QStringLiteral("QUIT"))
			{
				writeLine(socket, QStringLiteral("{\"ok\":true,\"closing\":true}"));
				disconnectSocket(socket);
			}
			else
			{
				writeLine(socket, QString("{\"ok\":false,\"error\":%1}").arg(JsonString(QString("unknown command: %1").arg(cmd))));
			}
		}

		void writeLine(QIODevice* socket, const QString& line)
		{
			socket->write(line.toUtf8());
			socket->write("\n");
			if (QTcpSocket* tcpSocket = qobject_cast<QTcpSocket*>(socket))
			{
				tcpSocket->flush();
			}
			else if (QLocalSocket* localSocket = qobject_cast<QLocalSocket*>(socket))
			{
				localSocket->flush();
			}
		}

		BridgeConfig config;
		QTcpServer* tcpServer = nullptr;
		QLocalServer* localServer = nullptr;
#ifdef __linux__
		int abstractListenFd = -1;
		QSocketNotifier* abstractNotifier = nullptr;
#endif
		QString token;
		QString tokenFilePath;
		QString unixSocketPath;
		QSet<QIODevice*> sockets;
		QHash<QIODevice*, bool> authenticated;
		QHash<QIODevice*, QByteArray> buffers;
};

BridgeServer* g_server = nullptr;

}

void PreParseArgs(int* argc, char** argv)
{
	if (argc == nullptr || argv == nullptr || *argc <= 1)
	{
		return;
	}

	int out = 1;
	for (int i = 1; i < *argc; i++)
	{
		const QString arg = QString::fromLocal8Bit(argv[i]);
		if (arg == QStringLiteral("--bridge-headless"))
		{
			g_config.enabled = true;
			g_config.headless = true;
			if (g_config.address.isEmpty())
			{
				g_config.address = defaultAddress();
			}
			continue;
		}
		if (arg == QStringLiteral("--bridge"))
		{
			g_config.enabled = true;
			if ((i + 1) < *argc)
			{
				const QString next = QString::fromLocal8Bit(argv[i + 1]);
				if (!next.startsWith('-'))
				{
					g_config.address = next;
					i++;
				}
			}
			if (g_config.address.isEmpty())
			{
				g_config.address = defaultAddress();
			}
			continue;
		}
		if (arg.startsWith(QStringLiteral("--bridge=")))
		{
			g_config.enabled = true;
			g_config.address = arg.mid(QStringLiteral("--bridge=").size());
			if (g_config.address.isEmpty())
			{
				g_config.address = defaultAddress();
			}
			continue;
		}
		if (g_config.headless && g_config.initialRom.isEmpty() && !arg.startsWith('-'))
		{
			g_config.initialRom = arg;
			continue;
		}
		argv[out++] = argv[i];
	}
	*argc = out;
	argv[out] = nullptr;
}

const BridgeConfig& GetConfig()
{
	return g_config;
}

bool Start()
{
	if (!g_config.enabled)
	{
		return true;
	}
	if (g_server != nullptr)
	{
		return true;
	}
	g_server = new BridgeServer(g_config);
	if (!g_server->start())
	{
		delete g_server;
		g_server = nullptr;
		return false;
	}
	FCEUXBridge::StartHistory();
	return true;
}

void Stop()
{
	CancelFrameGate();
	FCEUXBridge::ClearAllJoypads();
	FCEUXBridge::StopHistory();
	if (g_server != nullptr)
	{
		g_server->stop();
		delete g_server;
		g_server = nullptr;
	}
}

}
