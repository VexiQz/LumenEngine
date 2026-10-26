// lumen.net: TCP / UDP sockets with timeouts, select and name resolution
#include "LuaUtil.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <csignal>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#define LSOCK_INVALID INVALID_SOCKET
#define LSOCK_ERR() WSAGetLastError()
#define LSOCK_WOULDBLOCK(e) ((e) == WSAEWOULDBLOCK)
#define LSOCK_INTR(e) ((e) == WSAEINTR)
static void SockClose(socket_t s) { closesocket(s); }
static void SockNonBlocking(socket_t s, bool nb) { u_long v = nb ? 1 : 0; ioctlsocket(s, FIONBIO, &v); }
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define LSOCK_INVALID (-1)
#define LSOCK_ERR() errno
#define LSOCK_WOULDBLOCK(e) ((e) == EWOULDBLOCK || (e) == EAGAIN)
#define LSOCK_INTR(e) ((e) == EINTR)
static void SockClose(socket_t s) { close(s); }
static void SockNonBlocking(socket_t s, bool nb)
{
	int f = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, nb ? (f | O_NONBLOCK) : (f & ~O_NONBLOCK));
}
#endif

namespace lumen::lua::detail
{
	namespace
	{
		const char* kSock = "lumen.socket";

		void EnsureInit()
		{
#ifdef _WIN32
			static bool done = [] {
				WSADATA d;
				WSAStartup(MAKEWORD(2, 2), &d);
				return true;
			}();
			(void)done;
#else
			static bool done = [] {
				signal(SIGPIPE, SIG_IGN);
				return true;
			}();
			(void)done;
#endif
		}

		struct Sock
		{
			socket_t fd = LSOCK_INVALID;
			bool tcp = true;
			bool listening = false;
			double timeout = -1; // < 0 blocks forever, 0 = non blocking
			std::string buffer;  // for recv_line
			~Sock()
			{
				if (fd != LSOCK_INVALID)
					SockClose(fd);
			}
		};

		Sock* CheckSock(lua_State* L, int i = 1)
		{
			Sock* s = static_cast<Sock*>(luaL_checkudata(L, i, kSock));
			return s;
		}
		Sock* OpenSock(lua_State* L, int i = 1)
		{
			Sock* s = CheckSock(L, i);
			if (s->fd == LSOCK_INVALID)
				luaL_error(L, "socket is closed");
			return s;
		}

		std::string ErrText(int e)
		{
#ifdef _WIN32
			char buf[256];
			FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, (DWORD)e, 0, buf, sizeof(buf), nullptr);
			std::string s = buf;
			while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
			return s;
#else
			return strerror(e);
#endif
		}

		bool NetAllowed(lua_State* L) { return Policy(L).allowNetwork; }

		// waits until the socket is readable/writable; returns 1 ready, 0 timeout, -1 error
		int Wait(socket_t fd, bool write, double timeout)
		{
			if (timeout < 0)
				return 1;
			fd_set set;
			FD_ZERO(&set);
			FD_SET(fd, &set);
			timeval tv;
			tv.tv_sec = (long)timeout;
			tv.tv_usec = (long)((timeout - (double)(long)timeout) * 1e6);
			const int r = select((int)fd + 1, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
			return r < 0 ? -1 : (r == 0 ? 0 : 1);
		}

		bool ParseAddr(const std::string& host, int port, bool udp, sockaddr_storage& out, socklen_t& len, int& family, std::string& err)
		{
			addrinfo hints;
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
			addrinfo* res = nullptr;
			const std::string portStr = std::to_string(port);
			if (getaddrinfo(host.empty() ? nullptr : host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
			{
				err = "cannot resolve " + host;
				return false;
			}
			memcpy(&out, res->ai_addr, res->ai_addrlen);
			len = (socklen_t)res->ai_addrlen;
			family = res->ai_family;
			freeaddrinfo(res);
			return true;
		}

		std::string AddrToString(const sockaddr* sa, int& port)
		{
			char buf[INET6_ADDRSTRLEN] = {0};
			port = 0;
			if (sa->sa_family == AF_INET)
			{
				const sockaddr_in* a = (const sockaddr_in*)sa;
				inet_ntop(AF_INET, &a->sin_addr, buf, sizeof(buf));
				port = ntohs(a->sin_port);
			}
			else if (sa->sa_family == AF_INET6)
			{
				const sockaddr_in6* a = (const sockaddr_in6*)sa;
				inet_ntop(AF_INET6, &a->sin6_addr, buf, sizeof(buf));
				port = ntohs(a->sin6_port);
			}
			return buf;
		}

		int PushSockResult(lua_State* L, Sock* s, int e)
		{
			(void)s;
			lua_pushnil(L);
			if (LSOCK_WOULDBLOCK(e))
				lua_pushstring(L, "timeout");
			else
				Push(L, ErrText(e));
			return 2;
		}

		int TcpConnect(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			EnsureInit();
			const std::string host = Str(L, 1);
			const int port = (int)luaL_checkinteger(L, 2);
			const double timeout = luaL_optnumber(L, 3, 10);
			sockaddr_storage addr;
			socklen_t len;
			int family;
			std::string err;
			if (!ParseAddr(host, port, false, addr, len, family, err))
				return Fail(L, err);
			socket_t fd = socket(family, SOCK_STREAM, 0);
			if (fd == LSOCK_INVALID)
				return Fail(L, ErrText(LSOCK_ERR()));
			SockNonBlocking(fd, true);
			int rc = connect(fd, (sockaddr*)&addr, len);
			if (rc != 0)
			{
				const int e = LSOCK_ERR();
#ifdef _WIN32
				const bool pending = LSOCK_WOULDBLOCK(e);
#else
				const bool pending = e == EINPROGRESS || LSOCK_WOULDBLOCK(e);
#endif
				if (!pending)
				{
					SockClose(fd);
					return Fail(L, ErrText(e));
				}
				const int w = Wait(fd, true, timeout < 0 ? 3600 : timeout);
				if (w <= 0)
				{
					SockClose(fd);
					return Fail(L, w == 0 ? "connection timed out" : "connection failed");
				}
				int soerr = 0;
				socklen_t sl = sizeof(soerr);
				getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&soerr, &sl);
				if (soerr != 0)
				{
					SockClose(fd);
					return Fail(L, ErrText(soerr));
				}
			}
			SockNonBlocking(fd, false);
			Sock* s = NewUdata<Sock>(L, kSock);
			s->fd = fd;
			s->tcp = true;
			int one = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
			return 1;
		}
		int TcpListen(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			EnsureInit();
			const std::string host = lua_isnoneornil(L, 1) ? "127.0.0.1" : Str(L, 1);
			const int port = (int)luaL_checkinteger(L, 2);
			sockaddr_storage addr;
			socklen_t len;
			int family;
			std::string err;
			if (!ParseAddr(host, port, false, addr, len, family, err))
				return Fail(L, err);
			socket_t fd = socket(family, SOCK_STREAM, 0);
			if (fd == LSOCK_INVALID)
				return Fail(L, ErrText(LSOCK_ERR()));
			int one = 1;
#ifndef _WIN32
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));
#endif
			if (bind(fd, (sockaddr*)&addr, len) != 0 || listen(fd, (int)luaL_optinteger(L, 3, 32)) != 0)
			{
				const std::string e = ErrText(LSOCK_ERR());
				SockClose(fd);
				return Fail(L, e);
			}
			Sock* s = NewUdata<Sock>(L, kSock);
			s->fd = fd;
			s->listening = true;
			return 1;
		}
		int UdpOpen(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			EnsureInit();
			const std::string host = lua_isnoneornil(L, 1) ? "0.0.0.0" : Str(L, 1);
			const int port = (int)luaL_optinteger(L, 2, 0);
			sockaddr_storage addr;
			socklen_t len;
			int family;
			std::string err;
			if (!ParseAddr(host, port, true, addr, len, family, err))
				return Fail(L, err);
			socket_t fd = socket(family, SOCK_DGRAM, 0);
			if (fd == LSOCK_INVALID)
				return Fail(L, ErrText(LSOCK_ERR()));
			int one = 1;
			setsockopt(fd, SOL_SOCKET, SO_BROADCAST, (const char*)&one, sizeof(one));
			if (bind(fd, (sockaddr*)&addr, len) != 0)
			{
				const std::string e = ErrText(LSOCK_ERR());
				SockClose(fd);
				return Fail(L, e);
			}
			Sock* s = NewUdata<Sock>(L, kSock);
			s->fd = fd;
			s->tcp = false;
			return 1;
		}

		int SAccept(lua_State* L)
		{
			Sock* s = OpenSock(L);
			if (!s->listening)
				return Fail(L, "not a listening socket");
			const int w = Wait(s->fd, false, s->timeout);
			if (w == 0)
				return Fail(L, "timeout");
			sockaddr_storage addr;
			socklen_t len = sizeof(addr);
			socket_t c = accept(s->fd, (sockaddr*)&addr, &len);
			if (c == LSOCK_INVALID)
				return PushSockResult(L, s, LSOCK_ERR());
			SockNonBlocking(c, false);
			int one = 1;
			setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
			Sock* cs = NewUdata<Sock>(L, kSock);
			cs->fd = c;
			int port;
			Push(L, AddrToString((sockaddr*)&addr, port));
			lua_pushinteger(L, port);
			return 3;
		}
		int SSend(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const std::string_view d = View(L, 2);
			size_t sent = 0;
			while (sent < d.size())
			{
				const int w = Wait(s->fd, true, s->timeout);
				if (w == 0) { lua_pushnil(L); lua_pushstring(L, "timeout"); lua_pushinteger(L, (lua_Integer)sent); return 3; }
#ifdef _WIN32
				const int n = send(s->fd, d.data() + sent, (int)std::min<size_t>(d.size() - sent, 1 << 20), 0);
#else
				const ssize_t n = send(s->fd, d.data() + sent, std::min<size_t>(d.size() - sent, 1 << 20), MSG_NOSIGNAL);
#endif
				if (n < 0)
				{
					const int e = LSOCK_ERR();
					if (LSOCK_INTR(e)) continue;
					if (LSOCK_WOULDBLOCK(e) && s->timeout == 0) { lua_pushnil(L); lua_pushstring(L, "timeout"); lua_pushinteger(L, (lua_Integer)sent); return 3; }
					lua_pushnil(L);
					Push(L, ErrText(e));
					lua_pushinteger(L, (lua_Integer)sent);
					return 3;
				}
				sent += (size_t)n;
				if (s->timeout == 0) break; // non blocking: report partial writes
			}
			lua_pushinteger(L, (lua_Integer)sent);
			return 1;
		}
		// recv(max) -> data | nil,"timeout" | nil,"closed"
		int SRecv(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const lua_Integer max = luaL_optinteger(L, 2, 65536);
			luaL_argcheck(L, max > 0 && max <= (1 << 26), 2, "size out of range");
			if (!s->buffer.empty())
			{
				const size_t n = std::min<size_t>(s->buffer.size(), (size_t)max);
				Push(L, std::string_view(s->buffer).substr(0, n));
				s->buffer.erase(0, n);
				return 1;
			}
			const int w = Wait(s->fd, false, s->timeout);
			if (w == 0)
				return Fail(L, "timeout");
			std::string buf((size_t)max, 0);
#ifdef _WIN32
			const int n = recv(s->fd, &buf[0], (int)max, 0);
#else
			const ssize_t n = recv(s->fd, &buf[0], (size_t)max, 0);
#endif
			if (n == 0)
				return Fail(L, "closed");
			if (n < 0)
				return PushSockResult(L, s, LSOCK_ERR());
			Push(L, std::string_view(buf).substr(0, (size_t)n));
			return 1;
		}
		int SRecvExact(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const lua_Integer want = luaL_checkinteger(L, 2);
			luaL_argcheck(L, want >= 0 && want <= (1 << 28), 2, "size out of range");
			while (s->buffer.size() < (size_t)want)
			{
				const int w = Wait(s->fd, false, s->timeout);
				if (w == 0) return Fail(L, "timeout");
				char tmp[16384];
#ifdef _WIN32
				const int n = recv(s->fd, tmp, sizeof(tmp), 0);
#else
				const ssize_t n = recv(s->fd, tmp, sizeof(tmp), 0);
#endif
				if (n == 0) return Fail(L, "closed");
				if (n < 0) return PushSockResult(L, s, LSOCK_ERR());
				s->buffer.append(tmp, (size_t)n);
			}
			Push(L, std::string_view(s->buffer).substr(0, (size_t)want));
			s->buffer.erase(0, (size_t)want);
			return 1;
		}
		// recv_line() -> line without terminator (\n or \r\n); keeps partial data buffered across timeouts
		int SRecvLine(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const size_t limit = (size_t)luaL_optinteger(L, 2, 1 << 20);
			for (;;)
			{
				const size_t nl = s->buffer.find('\n');
				if (nl != std::string::npos)
				{
					std::string line = s->buffer.substr(0, nl);
					s->buffer.erase(0, nl + 1);
					if (!line.empty() && line.back() == '\r') line.pop_back();
					Push(L, line);
					return 1;
				}
				if (s->buffer.size() > limit) return Fail(L, "line too long");
				const int w = Wait(s->fd, false, s->timeout);
				if (w == 0) return Fail(L, "timeout");
				char tmp[4096];
#ifdef _WIN32
				const int n = recv(s->fd, tmp, sizeof(tmp), 0);
#else
				const ssize_t n = recv(s->fd, tmp, sizeof(tmp), 0);
#endif
				if (n == 0)
				{
					if (!s->buffer.empty())
					{
						Push(L, s->buffer);
						s->buffer.clear();
						return 1;
					}
					return Fail(L, "closed");
				}
				if (n < 0) return PushSockResult(L, s, LSOCK_ERR());
				s->buffer.append(tmp, (size_t)n);
			}
		}
		int SSendTo(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const std::string_view d = View(L, 2);
			const std::string host = Str(L, 3);
			const int port = (int)luaL_checkinteger(L, 4);
			sockaddr_storage addr;
			socklen_t len;
			int family;
			std::string err;
			if (!ParseAddr(host, port, true, addr, len, family, err))
				return Fail(L, err);
			const auto n = sendto(s->fd, d.data(), (int)d.size(), 0, (sockaddr*)&addr, len);
			if (n < 0)
				return PushSockResult(L, s, LSOCK_ERR());
			lua_pushinteger(L, (lua_Integer)n);
			return 1;
		}
		int SRecvFrom(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const lua_Integer max = luaL_optinteger(L, 2, 65536);
			luaL_argcheck(L, max > 0 && max <= 65536, 2, "size out of range");
			const int w = Wait(s->fd, false, s->timeout);
			if (w == 0)
				return Fail(L, "timeout");
			std::string buf((size_t)max, 0);
			sockaddr_storage addr;
			socklen_t len = sizeof(addr);
			const auto n = recvfrom(s->fd, &buf[0], (int)max, 0, (sockaddr*)&addr, &len);
			if (n < 0)
				return PushSockResult(L, s, LSOCK_ERR());
			Push(L, std::string_view(buf).substr(0, (size_t)n));
			int port;
			Push(L, AddrToString((sockaddr*)&addr, port));
			lua_pushinteger(L, port);
			return 3;
		}
		int SClose(lua_State* L)
		{
			Sock* s = CheckSock(L);
			if (s->fd != LSOCK_INVALID)
			{
				SockClose(s->fd);
				s->fd = LSOCK_INVALID;
			}
			lua_pushboolean(L, 1);
			return 1;
		}
		int SIsOpen(lua_State* L) { lua_pushboolean(L, CheckSock(L)->fd != LSOCK_INVALID); return 1; }
		int SSetTimeout(lua_State* L)
		{
			Sock* s = CheckSock(L);
			s->timeout = lua_isnoneornil(L, 2) ? -1 : luaL_checknumber(L, 2);
			return 0;
		}
		int SPeer(lua_State* L)
		{
			Sock* s = OpenSock(L);
			sockaddr_storage a;
			socklen_t l = sizeof(a);
			if (getpeername(s->fd, (sockaddr*)&a, &l) != 0)
				return Fail(L, ErrText(LSOCK_ERR()));
			int port;
			Push(L, AddrToString((sockaddr*)&a, port));
			lua_pushinteger(L, port);
			return 2;
		}
		int SLocal(lua_State* L)
		{
			Sock* s = OpenSock(L);
			sockaddr_storage a;
			socklen_t l = sizeof(a);
			if (getsockname(s->fd, (sockaddr*)&a, &l) != 0)
				return Fail(L, ErrText(LSOCK_ERR()));
			int port;
			Push(L, AddrToString((sockaddr*)&a, port));
			lua_pushinteger(L, port);
			return 2;
		}
		int SOption(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const std::string name = Str(L, 2);
			const int v = lua_toboolean(L, 3) ? 1 : 0;
			int rc = -1;
			if (name == "nodelay") rc = setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&v, sizeof(v));
			else if (name == "keepalive") rc = setsockopt(s->fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&v, sizeof(v));
			else if (name == "reuseaddr") rc = setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&v, sizeof(v));
			else if (name == "broadcast") rc = setsockopt(s->fd, SOL_SOCKET, SO_BROADCAST, (const char*)&v, sizeof(v));
			else return luaL_error(L, "unknown option '%s'", name.c_str());
			lua_pushboolean(L, rc == 0);
			return 1;
		}
		int SShutdown(lua_State* L)
		{
			Sock* s = OpenSock(L);
			const std::string how = lua_isnoneornil(L, 2) ? "both" : Str(L, 2);
#ifdef _WIN32
			const int h = how == "read" ? SD_RECEIVE : how == "write" ? SD_SEND : SD_BOTH;
#else
			const int h = how == "read" ? SHUT_RD : how == "write" ? SHUT_WR : SHUT_RDWR;
#endif
			lua_pushboolean(L, shutdown(s->fd, h) == 0);
			return 1;
		}
		int SToString(lua_State* L)
		{
			Sock* s = CheckSock(L);
			Push(L, StrFormat("socket(%s%s, %s)", s->tcp ? "tcp" : "udp", s->listening ? " listening" : "", s->fd == LSOCK_INVALID ? "closed" : "open"));
			return 1;
		}
		// net.select(readers, writers, timeout) -> readable, writable (arrays of the same socket objects)
		int NSelect(lua_State* L)
		{
			fd_set rs, ws;
			FD_ZERO(&rs);
			FD_ZERO(&ws);
			socket_t maxfd = 0;
			auto collect = [&](int idx, fd_set& set) {
				if (!lua_istable(L, idx)) return;
				const lua_Integer n = (lua_Integer)luaL_len(L, idx);
				for (lua_Integer i = 1; i <= n; ++i)
				{
					lua_rawgeti(L, idx, i);
					Sock* s = static_cast<Sock*>(luaL_checkudata(L, -1, kSock));
					if (s->fd != LSOCK_INVALID && (s->buffer.empty() || &set == &ws))
					{
						FD_SET(s->fd, &set);
						if (s->fd > maxfd) maxfd = s->fd;
					}
					lua_pop(L, 1);
				}
			};
			collect(1, rs);
			collect(2, ws);
			// sockets with buffered data are readable immediately
			bool buffered = false;
			if (lua_istable(L, 1))
			{
				const lua_Integer n = (lua_Integer)luaL_len(L, 1);
				for (lua_Integer i = 1; i <= n; ++i)
				{
					lua_rawgeti(L, 1, i);
					Sock* s = static_cast<Sock*>(luaL_checkudata(L, -1, kSock));
					if (!s->buffer.empty()) buffered = true;
					lua_pop(L, 1);
				}
			}
			const double timeout = lua_isnoneornil(L, 3) ? -1 : luaL_checknumber(L, 3);
			timeval tv;
			timeval* tvp = nullptr;
			if (timeout >= 0 || buffered)
			{
				const double t = buffered ? 0 : timeout;
				tv.tv_sec = (long)t;
				tv.tv_usec = (long)((t - (double)(long)t) * 1e6);
				tvp = &tv;
			}
			const int r = select((int)maxfd + 1, &rs, &ws, nullptr, tvp);
			if (r < 0)
				return Fail(L, ErrText(LSOCK_ERR()));
			auto result = [&](int idx, fd_set& set, bool alsoBuffered) {
				lua_newtable(L);
				if (!lua_istable(L, idx)) return;
				lua_Integer o = 1;
				const lua_Integer n = (lua_Integer)luaL_len(L, idx);
				for (lua_Integer i = 1; i <= n; ++i)
				{
					lua_rawgeti(L, idx, i);
					Sock* s = static_cast<Sock*>(luaL_checkudata(L, -1, kSock));
					if (s->fd != LSOCK_INVALID && ((r > 0 && FD_ISSET(s->fd, &set)) || (alsoBuffered && !s->buffer.empty())))
						lua_rawseti(L, -2, o++);
					else
						lua_pop(L, 1);
				}
			};
			result(1, rs, true);
			result(2, ws, false);
			return 2;
		}
		int NResolve(lua_State* L)
		{
			if (!NetAllowed(L))
				return Fail(L, "network access is disabled");
			EnsureInit();
			addrinfo hints;
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			hints.ai_socktype = SOCK_STREAM;
			addrinfo* res = nullptr;
			const std::string host = Str(L, 1);
			if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res)
				return Fail(L, "cannot resolve " + host);
			lua_newtable(L);
			lua_Integer i = 1;
			for (addrinfo* a = res; a; a = a->ai_next)
			{
				int port;
				const std::string s = AddrToString(a->ai_addr, port);
				if (s.empty()) continue;
				Push(L, s);
				lua_rawseti(L, -2, i++);
			}
			freeaddrinfo(res);
			return 1;
		}
		int NHostname(lua_State* L)
		{
			EnsureInit();
			char buf[256] = {0};
			if (gethostname(buf, sizeof(buf) - 1) != 0)
				return Fail(L, "gethostname failed");
			lua_pushstring(L, buf);
			return 1;
		}
		int NIsIp(lua_State* L)
		{
			const std::string s = Str(L, 1);
			unsigned char buf[sizeof(in6_addr)];
			lua_pushboolean(L, inet_pton(AF_INET, s.c_str(), buf) == 1 || inet_pton(AF_INET6, s.c_str(), buf) == 1);
			return 1;
		}
	} // namespace

	int OpenNet(lua_State* L)
	{
		static const luaL_Reg sm[] = {{"accept", SAccept}, {"send", SSend}, {"recv", SRecv}, {"recv_exact", SRecvExact}, {"recv_line", SRecvLine},
			{"send_to", SSendTo}, {"recv_from", SRecvFrom}, {"close", SClose}, {"is_open", SIsOpen}, {"settimeout", SSetTimeout}, {"peer", SPeer},
			{"sockname", SLocal}, {"option", SOption}, {"shutdown", SShutdown}, {nullptr, nullptr}};
		static const luaL_Reg smm[] = {{"__tostring", SToString}, {"__close", SClose}, {nullptr, nullptr}};
		MakeClass(L, kSock, sm, smm, GcUdata<Sock>);
		static const luaL_Reg fns[] = {{"tcp_connect", TcpConnect}, {"tcp_listen", TcpListen}, {"udp_open", UdpOpen}, {"select", NSelect},
			{"resolve", NResolve}, {"hostname", NHostname}, {"is_ip", NIsIp}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}
} // namespace lumen::lua::detail
