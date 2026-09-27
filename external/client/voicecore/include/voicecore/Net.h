/*
 * mod-voicechat - TLS (mbedTLS) and UDP sockets
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace voicecore
{
    // DE: Einmalige Initialisierung (Winsock). EN: one-time initialisation (Winsock).
    bool NetInit();

    class TlsSocket
    {
    public:
        TlsSocket();
        ~TlsSocket();

        // DE: pinSha256 (Hex, optional) = erwarteter SHA-256 des Server-Zertifikats.
        // EN: pinSha256 (hex, optional) = expected SHA-256 of the server certificate.
        bool Connect(const std::string& host, uint16_t port, const std::string& pinSha256, std::string& error);
        void Close();

        // DE: >0 Bytes, 0 = nichts da, <0 = Fehler/geschlossen. EN: >0 bytes, 0 = nothing, <0 = error/closed.
        int Read(uint8_t* buf, size_t len);
        bool WriteAll(const uint8_t* buf, size_t len);
        bool HasBuffered() const;

        int Fd() const;
        std::string PeerFingerprint() const { return _fingerprint; }   // SHA-256 hex
        std::string RemoteIp() const { return _remoteIp; }

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
        std::string _fingerprint;
        std::string _remoteIp;
    };

    class UdpSocket
    {
    public:
        ~UdpSocket();
        bool Open(const std::string& host, uint16_t port);   // connected UDP socket
        void Close();
        bool Send(const uint8_t* buf, size_t len);
        int Recv(uint8_t* buf, size_t len);                  // >0 bytes, 0 = nothing, <0 = error
        int Fd() const { return _fd; }

    private:
        int _fd = -1;
    };

    // DE: Wartet max. timeoutMs auf Daten an fd1/fd2 (-1 = ignorieren).
    // EN: Waits up to timeoutMs for data on fd1/fd2 (-1 = ignore).
    void WaitReadable(int fd1, int fd2, int timeoutMs);
}
