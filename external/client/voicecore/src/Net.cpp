/*
 * mod-voicechat - TLS (mbedTLS) and UDP sockets
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "voicecore/Net.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#if defined(MBEDTLS_PSA_CRYPTO_C)
#include <psa/crypto.h>
#endif

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
#define VC_CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define VC_CLOSESOCK ::close
#endif

namespace voicecore
{
    bool NetInit()
    {
#ifdef _WIN32
        static bool done = false;
        if (!done)
        {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
            done = true;
        }
#endif
        return true;
    }

    namespace
    {
        constexpr int CONNECT_TIMEOUT_MS   = 8000;
        constexpr int HANDSHAKE_TIMEOUT_MS = 10000;
        constexpr int WRITE_TIMEOUT_MS     = 5000;

        uint64_t NowMs()
        {
            return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        bool Cancelled(const std::atomic<bool>* c) { return c && c->load(); }

        // DE: Wartet max. timeoutMs, bis fd lesbar (write=false) bzw. schreibbar ist. >0 = bereit.
        // EN: waits up to timeoutMs until fd is readable (write=false) or writable. >0 = ready.
        int WaitSocket(int fd, bool write, int timeoutMs)
        {
            fd_set rs, ws, es;
            FD_ZERO(&rs); FD_ZERO(&ws); FD_ZERO(&es);
            FD_SET(fd, write ? &ws : &rs);
            FD_SET(fd, &es);
            timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
            return select(fd + 1, &rs, &ws, &es, &tv);
        }
    }

    static void SetNonBlocking(int fd)
    {
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &mode);
#else
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
    }

    // ---------------------------------------------------------------------------------------
    struct TlsSocket::Impl
    {
        mbedtls_net_context net;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context drbg;
        bool open = false;

        Impl()
        {
            mbedtls_net_init(&net);
            mbedtls_ssl_init(&ssl);
            mbedtls_ssl_config_init(&conf);
            mbedtls_entropy_init(&entropy);
            mbedtls_ctr_drbg_init(&drbg);
        }
        ~Impl()
        {
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ctr_drbg_free(&drbg);
            mbedtls_entropy_free(&entropy);
            mbedtls_net_free(&net);
        }
    };

    TlsSocket::TlsSocket() = default;
    TlsSocket::~TlsSocket() { Close(); }

    // DE: Nicht blockierender TCP-Connect mit Timeout und Abbruch. EN: non-blocking TCP connect with timeout and cancel.
    static int ConnectTcp(const std::string& host, uint16_t port, const std::atomic<bool>* cancel, std::string& error)
    {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
        {
            error = "connect: cannot resolve " + host;
            return -1;
        }
        int result = -1;
        for (addrinfo* a = res; a && result < 0 && !Cancelled(cancel); a = a->ai_next)
        {
            int fd = int(socket(a->ai_family, a->ai_socktype, a->ai_protocol));
            if (fd < 0) continue;
            SetNonBlocking(fd);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
            if (connect(fd, a->ai_addr, socklen_t(a->ai_addrlen)) == 0)
            {
                result = fd;
                break;
            }
#ifdef _WIN32
            bool pending = WSAGetLastError() == WSAEWOULDBLOCK;
#else
            bool pending = errno == EINPROGRESS;
#endif
            uint64_t deadline = NowMs() + CONNECT_TIMEOUT_MS;
            while (pending && !Cancelled(cancel) && NowMs() < deadline)
            {
                int s = WaitSocket(fd, true, 100);
                if (s < 0) break;
                if (s == 0) continue;
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
                if (err == 0) result = fd;
                break;
            }
            if (result < 0) VC_CLOSESOCK(fd);
        }
        freeaddrinfo(res);
        if (result < 0)
            error = Cancelled(cancel) ? "connect: cancelled" : "connect: cannot reach " + host + ":" + std::to_string(port);
        return result;
    }

    static std::string MbedError(int rc)
    {
        char buf[160];
        mbedtls_strerror(rc, buf, sizeof(buf));
        return std::string(buf) + " (" + std::to_string(rc) + ")";
    }

    bool TlsSocket::Connect(const std::string& host, uint16_t port, const std::string& pinSha256, std::string& error,
                            const std::atomic<bool>* cancel)
    {
        Close();
        NetInit();
        _impl = std::make_unique<Impl>();
        Impl& d = *_impl;
        int rc;

#if defined(MBEDTLS_PSA_CRYPTO_C)
        psa_crypto_init();   // TLS 1.3 in mbedTLS 3.x braucht PSA / needs PSA
#endif
        const char* pers = "mod-voicechat";
        if ((rc = mbedtls_ctr_drbg_seed(&d.drbg, mbedtls_entropy_func, &d.entropy,
                                        reinterpret_cast<const unsigned char*>(pers), std::strlen(pers))) != 0)
        { error = "drbg: " + MbedError(rc); return false; }

        d.net.fd = ConnectTcp(host, port, cancel, error);   // nicht blockierend / non-blocking
        if (d.net.fd < 0) return false;

        mbedtls_ssl_config_defaults(&d.conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_min_tls_version(&d.conf, MBEDTLS_SSL_VERSION_TLS1_2);
        // DE: Murmur nutzt meist selbstsignierte Zertifikate -> Pinning statt CA-Pruefung.
        // EN: Murmur usually uses self-signed certificates -> pinning instead of CA validation.
        mbedtls_ssl_conf_authmode(&d.conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
        mbedtls_ssl_conf_rng(&d.conf, mbedtls_ctr_drbg_random, &d.drbg);

        if ((rc = mbedtls_ssl_setup(&d.ssl, &d.conf)) != 0) { error = "ssl_setup: " + MbedError(rc); return false; }
        mbedtls_ssl_set_hostname(&d.ssl, host.c_str());
        mbedtls_ssl_set_bio(&d.ssl, &d.net, mbedtls_net_send, mbedtls_net_recv, nullptr);

        uint64_t deadline = NowMs() + HANDSHAKE_TIMEOUT_MS;
        while ((rc = mbedtls_ssl_handshake(&d.ssl)) != 0)
        {
            if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE)
            { error = "handshake: " + MbedError(rc); return false; }
            if (Cancelled(cancel)) { error = "handshake: cancelled"; return false; }
            if (NowMs() > deadline) { error = "handshake: timeout"; return false; }
            WaitSocket(d.net.fd, rc == MBEDTLS_ERR_SSL_WANT_WRITE, 100);
        }

        // DE: Fingerprint des Server-Zertifikats. EN: fingerprint of the server certificate.
        const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&d.ssl);
        if (crt)
        {
            unsigned char hash[32];
            mbedtls_sha256(crt->raw.p, crt->raw.len, hash, 0);
            char hex[65];
            for (int i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", hash[i]);
            _fingerprint = hex;
        }
        if (!pinSha256.empty())
        {
            std::string want;
            for (char c : pinSha256) if (c != ':' && c != ' ') want += char(std::tolower(static_cast<unsigned char>(c)));
            if (want != _fingerprint) { error = "certificate pin mismatch (server " + _fingerprint + ")"; return false; }
        }

        // DE: Remote-IP fuer UDP merken. EN: remember remote IP for UDP.
        sockaddr_storage ss{};
        socklen_t sl = sizeof(ss);
        if (getpeername(d.net.fd, reinterpret_cast<sockaddr*>(&ss), &sl) == 0)
        {
            char ip[INET6_ADDRSTRLEN] = {};
            if (ss.ss_family == AF_INET)
                inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(&ss)->sin_addr, ip, sizeof(ip));
            else
                inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(&ss)->sin6_addr, ip, sizeof(ip));
            _remoteIp = ip;
        }

        d.open = true;
        return true;
    }

    void TlsSocket::Close()
    {
        if (_impl && _impl->open)
            mbedtls_ssl_close_notify(&_impl->ssl);
        _impl.reset();
    }

    int TlsSocket::Read(uint8_t* buf, size_t len)
    {
        if (!_impl || !_impl->open) return -1;
        int rc = mbedtls_ssl_read(&_impl->ssl, buf, len);
        if (rc > 0) return rc;
        if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            || rc == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
#endif
            )
            return 0;
        return -1;   // 0 = EOF, sonst Fehler / otherwise error
    }

    bool TlsSocket::WriteAll(const uint8_t* buf, size_t len)
    {
        if (!_impl || !_impl->open) return false;
        size_t off = 0;
        uint64_t deadline = NowMs() + WRITE_TIMEOUT_MS;   // DE: Server liest nicht mehr -> Abbruch / server stopped reading -> give up
        while (off < len)
        {
            int rc = mbedtls_ssl_write(&_impl->ssl, buf + off, len - off);
            if (rc > 0) { off += size_t(rc); continue; }
            if (rc == MBEDTLS_ERR_SSL_WANT_READ || rc == MBEDTLS_ERR_SSL_WANT_WRITE)
            {
                if (NowMs() > deadline) return false;
                WaitSocket(_impl->net.fd, rc == MBEDTLS_ERR_SSL_WANT_WRITE, 10);
                continue;
            }
            return false;
        }
        return true;
    }

    bool TlsSocket::HasBuffered() const { return _impl && _impl->open && mbedtls_ssl_get_bytes_avail(&_impl->ssl) > 0; }
    int TlsSocket::Fd() const { return (_impl && _impl->open) ? _impl->net.fd : -1; }

    // ---------------------------------------------------------------------------------------
    UdpSocket::~UdpSocket() { Close(); }

    bool UdpSocket::Open(const std::string& host, uint16_t port)
    {
        Close();
        NetInit();
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
            return false;
        for (addrinfo* a = res; a; a = a->ai_next)
        {
            int fd = int(socket(a->ai_family, a->ai_socktype, a->ai_protocol));
            if (fd < 0) continue;
            if (connect(fd, a->ai_addr, socklen_t(a->ai_addrlen)) == 0)
            {
                SetNonBlocking(fd);
                _fd = fd;
                break;
            }
            VC_CLOSESOCK(fd);
        }
        freeaddrinfo(res);
        return _fd >= 0;
    }

    void UdpSocket::Close()
    {
        if (_fd >= 0) { VC_CLOSESOCK(_fd); _fd = -1; }
    }

    bool UdpSocket::Send(const uint8_t* buf, size_t len)
    {
        return _fd >= 0 && ::send(_fd, reinterpret_cast<const char*>(buf), int(len), 0) == int(len);
    }

    int UdpSocket::Recv(uint8_t* buf, size_t len)
    {
        if (_fd < 0) return -1;
        int n = int(::recv(_fd, reinterpret_cast<char*>(buf), int(len), 0));
        if (n >= 0) return n;
#ifdef _WIN32
        int e = WSAGetLastError();
        return (e == WSAEWOULDBLOCK || e == WSAECONNRESET) ? 0 : -1;   // ICMP unreachable -> ignorieren / ignore
#else
        return (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) ? 0 : -1;
#endif
    }

    void WaitReadable(int fd1, int fd2, int timeoutMs)
    {
        fd_set rs;
        FD_ZERO(&rs);
        int maxFd = -1;
        if (fd1 >= 0) { FD_SET(fd1, &rs); maxFd = fd1; }
        if (fd2 >= 0) { FD_SET(fd2, &rs); if (fd2 > maxFd) maxFd = fd2; }
        timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        if (maxFd < 0)
        {
#ifdef _WIN32
            Sleep(DWORD(timeoutMs));
#else
            select(0, nullptr, nullptr, nullptr, &tv);
#endif
            return;
        }
        select(maxFd + 1, &rs, nullptr, nullptr, &tv);
    }
}
