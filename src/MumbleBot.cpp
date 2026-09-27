/*
 * mod-voicechat - Mumble control bot (see MumbleBot.h)
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "MumbleBot.h"
#include "MumbleProtocol.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <algorithm>
#include <future>
#include <optional>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
using namespace MumbleProto;

namespace VoiceChat
{
    namespace
    {
        // Mumble ACL-Rechte / permissions (src/ACL.h)
        constexpr uint32_t PERM_ENTER = 0x4, PERM_SPEAK = 0x8, PERM_MAKE_CHANNEL = 0x40, PERM_LINK = 0x80,
                           PERM_WHISPER = 0x100, PERM_TEXT = 0x200, PERM_MAKE_TEMP = 0x400, PERM_LISTEN = 0x800;

        uint64_t NowMs()
        {
            return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }
    }

    struct MumbleBot::Impl
    {
        MumbleBot& owner;
        Config cfg;
        asio::io_context io;
        std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work;
        std::thread thread;
        asio::ssl::context ssl{ asio::ssl::context::tls_client };
        std::unique_ptr<asio::ssl::stream<tcp::socket>> stream;
        asio::steady_timer pingTimer{ io }, reconnectTimer{ io }, maintTimer{ io };

        std::deque<std::vector<uint8_t>> writeQueue;
        bool writing = false;
        uint8_t header[TCP_HEADER_SIZE] = {};
        std::vector<uint8_t> body;
        uint64_t generation = 0;     // DE: verwirft Callbacks alter Verbindungen / discards callbacks of old connections

        std::atomic<bool> connected{ false }, stopping{ false };
        std::atomic<uint32_t> ownSession{ 0 };
        bool synced = false;

        struct Chan { uint32_t parent = 0; std::string name; };
        std::map<uint32_t, Chan> channels;
        std::map<uint32_t, std::pair<std::string, uint32_t>> users;   // session -> (name, channel)
        std::map<uint32_t, uint64_t> emptySince;
        std::map<std::pair<uint32_t, std::string>, uint64_t> creating;   // (parent, name) -> Zeitpunkt / time
        std::deque<std::pair<Tcp, std::vector<uint8_t>>> limited;      // rate-limitierte Nachrichten / rate-limited messages
        double tokens = 0;
        uint64_t lastRefill = 0;
        asio::steady_timer limitTimer{ io }, pendingTimer{ io };
        std::map<uint32_t, std::vector<std::string>> pendingMoves;
        bool rootAcl = false, lobbyAcl = false;

        std::mutex evMutex;
        std::vector<Event> events;

        explicit Impl(MumbleBot& o) : owner(o)
        {
            ssl.set_verify_mode(asio::ssl::verify_none);   // DE: Murmur ist selbstsigniert -> Pinning / self-signed -> pinning
        }

        void Log(const std::string& m) { if (owner.onLog) owner.onLog(m); }
        void Verbose(const std::string& m) { if (cfg.verbose) Log("bot: " + m); }
        void Emit(Event e) { std::lock_guard<std::mutex> g(evMutex); events.push_back(std::move(e)); }

        // --- Verbindung / connection ---------------------------------------------------------
        void Connect()
        {
            if (stopping) return;
            ++generation;
            uint64_t gen = generation;
            stream = std::make_unique<asio::ssl::stream<tcp::socket>>(io, ssl);
            auto resolver = std::make_shared<tcp::resolver>(io);
            resolver->async_resolve(cfg.host, std::to_string(cfg.port),
                [this, gen, resolver](const boost::system::error_code& ec, tcp::resolver::results_type res) {
                    if (gen != generation) return;
                    if (ec) return Fail("resolve: " + ec.message());
                    asio::async_connect(stream->lowest_layer(), res,
                        [this, gen](const boost::system::error_code& ec2, const tcp::endpoint&) {
                            if (gen != generation) return;
                            if (ec2) return Fail("connect: " + ec2.message());
                            stream->async_handshake(asio::ssl::stream_base::client, [this, gen](const boost::system::error_code& ec3) {
                                if (gen != generation) return;
                                if (ec3) return Fail("tls: " + ec3.message());
                                if (!CheckPin()) return Fail("certificate pin mismatch");
                                Send(Tcp::Version, EncodeVersion("mod-voicechat bot", "AzerothCore", ""));
                                Send(Tcp::Authenticate, EncodeAuthenticate(cfg.username, cfg.password, {}, true));
                                ReadHeader(gen);
                                SchedulePing(gen);
                            });
                        });
                });
        }

        bool CheckPin()
        {
            if (cfg.certPinSha256.empty()) return true;
            X509* cert = SSL_get_peer_certificate(stream->native_handle());
            if (!cert) return false;
            unsigned char md[EVP_MAX_MD_SIZE];
            unsigned int n = 0;
            X509_digest(cert, EVP_sha256(), md, &n);
            X509_free(cert);
            std::string hex;
            char b[3];
            for (unsigned int i = 0; i < n; ++i) { std::snprintf(b, sizeof(b), "%02x", md[i]); hex += b; }
            std::string want;
            for (char c : cfg.certPinSha256) if (c != ':' && c != ' ') want += char(std::tolower(static_cast<unsigned char>(c)));
            return hex == want;
        }

        void Fail(const std::string& reason)
        {
            bool wasConnected = connected.exchange(false);
            ++generation;
            if (stream)
            {
                boost::system::error_code ignored;
                stream->lowest_layer().close(ignored);
            }
            writeQueue.clear();
            writing = false;
            synced = false;
            ownSession = 0;
            channels.clear();
            users.clear();
            emptySince.clear();
            creating.clear();
            limited.clear();
            rootAcl = lobbyAcl = false;
            pingTimer.cancel();
            maintTimer.cancel();
            limitTimer.cancel();
            pendingTimer.cancel();
            Log("bot: " + reason);
            if (wasConnected) Emit({ Event::Disconnected, 0, "", reason });
            if (stopping) return;
            reconnectTimer.expires_after(std::chrono::seconds(5));
            reconnectTimer.async_wait([this](const boost::system::error_code& ec) { if (!ec) Connect(); });
        }

        void Send(Tcp type, const std::vector<uint8_t>& payload)
        {
            writeQueue.push_back(Frame(type, payload));
            if (!writing) DoWrite(generation);
        }

        // DE: Token-Bucket wie Murmurs leakyBucket. EN: token bucket like Murmur's leakyBucket.
        void SendLimited(Tcp type, const std::vector<uint8_t>& payload)
        {
            limited.emplace_back(type, payload);
            PumpLimited(generation);
        }

        void PumpLimited(uint64_t gen)
        {
            if (gen != generation) return;
            uint64_t now = NowMs();
            tokens = std::min(cfg.messageBurst, tokens + double(now - lastRefill) / 1000.0 * cfg.messageRate);
            lastRefill = now;
            while (!limited.empty() && tokens >= 1.0)
            {
                Send(limited.front().first, limited.front().second);
                limited.pop_front();
                tokens -= 1.0;
            }
            if (limited.empty()) return;
            auto waitMs = uint64_t((1.0 - tokens) / cfg.messageRate * 1000.0) + 5;
            limitTimer.expires_after(std::chrono::milliseconds(waitMs));
            limitTimer.async_wait([this, gen](const boost::system::error_code& ec) { if (!ec) PumpLimited(gen); });
        }

        void SchedulePending(uint64_t gen)
        {
            pendingTimer.expires_after(std::chrono::seconds(1));
            pendingTimer.async_wait([this, gen](const boost::system::error_code& ec) {
                if (ec || gen != generation) return;
                EnsureStructure();
                ProcessPending();
                SchedulePending(gen);
            });
        }

        void DoWrite(uint64_t gen)
        {
            if (writeQueue.empty() || !stream) { writing = false; return; }
            writing = true;
            asio::async_write(*stream, asio::buffer(writeQueue.front()), [this, gen](const boost::system::error_code& ec, size_t) {
                if (gen != generation) return;
                if (ec) return Fail("write: " + ec.message());
                writeQueue.pop_front();
                DoWrite(gen);
            });
        }

        void ReadHeader(uint64_t gen)
        {
            asio::async_read(*stream, asio::buffer(header, TCP_HEADER_SIZE), [this, gen](const boost::system::error_code& ec, size_t) {
                if (gen != generation) return;
                if (ec) return Fail("read: " + ec.message());
                uint32_t len = (uint32_t(header[2]) << 24) | (uint32_t(header[3]) << 16) | (uint32_t(header[4]) << 8) | header[5];
                if (len > MAX_TCP_PAYLOAD) return Fail("oversized message");
                body.resize(len);
                if (len == 0) { Handle(uint16_t((header[0] << 8) | header[1])); return ReadHeader(gen); }
                asio::async_read(*stream, asio::buffer(body), [this, gen](const boost::system::error_code& ec2, size_t) {
                    if (gen != generation) return;
                    if (ec2) return Fail("read: " + ec2.message());
                    Handle(uint16_t((header[0] << 8) | header[1]));
                    if (gen == generation) ReadHeader(gen);
                });
            });
        }

        void SchedulePing(uint64_t gen)
        {
            pingTimer.expires_after(std::chrono::seconds(10));
            pingTimer.async_wait([this, gen](const boost::system::error_code& ec) {
                if (ec || gen != generation) return;
                Send(Tcp::Ping, EncodePing(NowMs()));
                SchedulePing(gen);
            });
        }

        void ScheduleMaintenance(uint64_t gen)
        {
            maintTimer.expires_after(std::chrono::seconds(10));
            maintTimer.async_wait([this, gen](const boost::system::error_code& ec) {
                if (ec || gen != generation) return;
                CleanupEmptyChannels();
                ScheduleMaintenance(gen);
            });
        }

        // --- Nachrichten / messages ----------------------------------------------------------
        void Handle(uint16_t type)
        {
            switch (Tcp(type))
            {
                case Tcp::ServerSync:
                {
                    ServerSync s;
                    s.Decode(body);
                    ownSession = s.session;
                    synced = true;
                    connected = true;
                    Log("bot: connected, session " + std::to_string(s.session));
                    Emit({ Event::Connected, s.session, "", "" });
                    for (auto& u : users)
                        if (u.first != s.session) Emit({ Event::UserJoined, u.first, u.second.first, "" });
                    tokens = cfg.messageBurst;
                    lastRefill = NowMs();
                    EnsureStructure();
                    ScheduleMaintenance(generation);
                    SchedulePending(generation);
                    break;
                }
                case Tcp::Reject:
                {
                    Reject r;
                    r.Decode(body);
                    Fail("rejected (" + std::to_string(r.type) + "): " + r.reason);
                    break;
                }
                case Tcp::ChannelState:
                {
                    MumbleProto::ChannelState c;
                    if (!c.Decode(body) || !c.channelId) break;
                    Chan& ch = channels[*c.channelId];
                    if (c.parent) ch.parent = *c.parent;
                    if (c.name) ch.name = *c.name;
                    creating.erase({ ch.parent, ch.name });
                    Verbose("channel " + std::to_string(*c.channelId) + " '" + ch.name + "' parent " + std::to_string(ch.parent));
                    if (synced) { EnsureStructure(); ProcessPending(); }
                    break;
                }
                case Tcp::ChannelRemove:
                {
                    if (auto id = DecodeSessionField(body, 1)) { channels.erase(*id); emptySince.erase(*id); }
                    break;
                }
                case Tcp::UserState:
                {
                    MumbleProto::UserState u;
                    if (!u.Decode(body) || !u.session) break;
                    bool isNew = users.find(*u.session) == users.end();
                    auto& e = users[*u.session];
                    if (u.name) e.first = *u.name;
                    if (u.channelId) e.second = *u.channelId;
                    if (isNew && synced && *u.session != ownSession)
                        Emit({ Event::UserJoined, *u.session, e.first, "" });
                    if (synced) ProcessPending();
                    break;
                }
                case Tcp::UserRemove:
                {
                    auto s = DecodeSessionField(body, 1);
                    if (!s) break;
                    if (*s == ownSession) { Fail("removed from server"); break; }
                    users.erase(*s);
                    pendingMoves.erase(*s);
                    Emit({ Event::UserLeft, *s, "", "" });
                    break;
                }
                case Tcp::TextMessage:
                {
                    MumbleProto::TextMessage t;
                    if (t.Decode(body) && t.actor) Emit({ Event::Text, *t.actor, "", t.message });
                    break;
                }
                case Tcp::PermissionDenied:
                {
                    Reader r(body.data(), body.size());
                    Field f;
                    std::string detail;
                    while (r.Next(f))
                    {
                        if (f.number == 4 || f.number == 6) detail += " '" + f.Str() + "'";
                        else if (f.type == VARINT) detail += " f" + std::to_string(f.number) + "=" + std::to_string(f.value);
                    }
                    Log("bot: permission denied" + detail);
                    break;
                }
                default:
                    break;
            }
        }

        // --- Channels / channels -------------------------------------------------------------
        std::optional<uint32_t> FindChild(uint32_t parent, const std::string& name) const
        {
            for (const auto& c : channels)
                if (c.first != 0 && c.second.parent == parent && c.second.name == name) return c.first;
            return std::nullopt;
        }

        void RequestCreate(uint32_t parent, const std::string& name)
        {
            // DE: Nicht doppelt anfragen; ohne Antwort nach 5 s erneut (Murmur verwirft still bei Rate-Limit).
            // EN: do not ask twice; retry after 5 s without answer (Murmur silently drops on rate limit).
            uint64_t now = NowMs();
            auto it = creating.find({ parent, name });
            if (it != creating.end() && now - it->second < 5000) return;
            creating[{ parent, name }] = now;
            Verbose("create channel '" + name + "' under " + std::to_string(parent));
            MumbleProto::ChannelState c;
            c.parent = parent;
            c.name = name;
            c.temporary = false;   // DE: temporaer wuerde den Bot hineinziehen / temporary would pull the bot in
            SendLimited(Tcp::ChannelState, c.Encode());
        }

        void SendAcl(uint32_t channel, bool lobby)
        {
            Writer w;
            w.U32(1, channel);
            w.Bool(2, true);   // inherit_acls
            if (!lobby)
            {
                // DE: Alle: kein Betreten/Sprechen/Text/Mithoeren/Channels. Sprechen nur "in" (eigener Channel).
                //     Fluestern ueberall erlaubt (Gruppen ueber Maps hinweg); welche Stimmen abgespielt werden,
                //     entscheidet der Client anhand der Listen vom Worldserver (NEARBY: nah + Gruppe).
                // EN: everyone: no enter/speak/text/listen/channels. Speak only "in" (own channel).
                //     Whisper allowed everywhere (groups across maps); which voices are played is decided
                //     by the client using the lists from the worldserver (NEARBY: near + group).
                Writer deny;
                deny.Bool(1, true); deny.Bool(2, true); deny.Str(5, "all");
                deny.U32(7, PERM_ENTER | PERM_SPEAK | PERM_TEXT | PERM_MAKE_CHANNEL | PERM_MAKE_TEMP | PERM_LINK | PERM_LISTEN);
                w.Msg(4, deny);
                Writer whisper;
                whisper.Bool(1, true); whisper.Bool(2, true); whisper.Str(5, "all");
                whisper.U32(6, PERM_WHISPER);
                w.Msg(4, whisper);
                Writer in;
                in.Bool(1, true); in.Bool(2, true); in.Str(5, "in");
                in.U32(6, PERM_SPEAK);
                w.Msg(4, in);
            }
            else
            {
                // DE: In der Lobby darf jeder dem Bot schreiben (BIND). EN: in the lobby everyone may text the bot (BIND).
                Writer text;
                text.Bool(1, true); text.Bool(2, false); text.Str(5, "all");
                text.U32(6, PERM_TEXT);
                w.Msg(4, text);
            }
            SendLimited(Tcp::ACL, w.buf);
        }

        void EnsureStructure()
        {
            auto root = FindChild(0, cfg.rootChannel);
            if (!root) { RequestCreate(0, cfg.rootChannel); return; }
            if (!rootAcl) { SendAcl(*root, false); rootAcl = true; }
            auto lobby = FindChild(*root, cfg.lobbyChannel);
            if (!lobby) { RequestCreate(*root, cfg.lobbyChannel); return; }
            if (!lobbyAcl) { SendAcl(*lobby, true); lobbyAcl = true; }
            auto self = users.find(ownSession);
            if (self != users.end() && self->second.second != *lobby)
            {
                MumbleProto::UserState us;
                us.session = ownSession.load();
                us.channelId = *lobby;
                us.selfDeaf = true;    // DE: Bot hoert nichts (keine Audio-Last) / bot hears nothing (no audio load)
                us.selfMute = true;
                SendLimited(Tcp::UserState, us.Encode());   // eigener Zustand ist limitiert / own state is limited
            }
        }

        void ProcessPending()
        {
            auto root = FindChild(0, cfg.rootChannel);
            if (!root) return;
            for (auto it = pendingMoves.begin(); it != pendingMoves.end();)
            {
                auto user = users.find(it->first);
                if (user == users.end()) { it = pendingMoves.erase(it); continue; }
                uint32_t cur = *root;
                bool complete = true;
                for (const auto& seg : it->second)
                {
                    auto child = FindChild(cur, seg);
                    if (!child) { RequestCreate(cur, seg); complete = false; break; }
                    cur = *child;
                }
                if (!complete) { ++it; continue; }
                if (user->second.second != cur)
                {
                    Verbose("move session " + std::to_string(it->first) + " -> channel " + std::to_string(cur));
                    MumbleProto::UserState us;
                    us.session = it->first;
                    us.channelId = cur;
                    Send(Tcp::UserState, us.Encode());
                }
                emptySince.erase(cur);
                it = pendingMoves.erase(it);
            }
        }

        void CleanupEmptyChannels()
        {
            auto root = FindChild(0, cfg.rootChannel);
            if (!root) return;
            auto lobby = FindChild(*root, cfg.lobbyChannel);
            uint64_t now = NowMs();
            for (const auto& c : channels)
            {
                uint32_t id = c.first;
                if (id == 0 || id == *root || (lobby && id == *lobby)) continue;
                // DE: nur Blaetter unterhalb unseres Roots. EN: only leaves below our root.
                bool below = false;
                for (uint32_t p = c.second.parent, guard = 0; guard < 16; ++guard)
                {
                    if (p == *root) { below = true; break; }
                    auto pc = channels.find(p);
                    if (p == 0 || pc == channels.end()) break;
                    p = pc->second.parent;
                }
                if (!below) continue;
                bool hasChild = false, hasUser = false;
                for (const auto& o : channels) if (o.second.parent == id && o.first != id) { hasChild = true; break; }
                for (const auto& u : users) if (u.second.second == id) { hasUser = true; break; }
                if (hasChild || hasUser) { emptySince.erase(id); continue; }
                auto& since = emptySince[id];
                if (!since) since = now;
                else if (now - since > uint64_t(cfg.emptyChannelTimeoutSec) * 1000)
                {
                    Writer w;
                    w.U32(1, id);
                    Send(Tcp::ChannelRemove, w.buf);
                    since = now;   // DE: nicht doppelt senden / do not send twice
                }
            }
        }
    };

    MumbleBot::MumbleBot() : _impl(std::make_unique<Impl>(*this)) { }
    MumbleBot::~MumbleBot() { Stop(); }

    void MumbleBot::Start(const Config& cfg)
    {
        Stop();
        _impl = std::make_unique<Impl>(*this);
        Impl& d = *_impl;
        d.cfg = cfg;
        d.work = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(d.io.get_executor());
        asio::post(d.io, [&d]() { d.Connect(); });
        d.thread = std::thread([&d]() { d.io.run(); });
    }

    void MumbleBot::Stop()
    {
        if (!_impl || !_impl->thread.joinable()) return;
        Impl& d = *_impl;
        d.stopping = true;
        auto closed = std::make_shared<std::promise<void>>();
        asio::post(d.io, [&d, closed]() {
            ++d.generation;
            d.pingTimer.cancel(); d.reconnectTimer.cancel(); d.maintTimer.cancel();
            d.limitTimer.cancel(); d.pendingTimer.cancel();
            if (d.stream) { boost::system::error_code ignored; d.stream->lowest_layer().close(ignored); }
            closed->set_value();
        });
        // DE: Schliessen abwarten (max. 2 s), dann Thread beenden. EN: wait for the close (max 2 s), then end the thread.
        closed->get_future().wait_for(std::chrono::seconds(2));
        d.work.reset();
        d.io.stop();
        d.thread.join();
        d.connected = false;
    }

    bool MumbleBot::IsConnected() const { return _impl && _impl->connected; }
    uint32_t MumbleBot::OwnSession() const { return _impl ? _impl->ownSession.load() : 0; }

    void MumbleBot::MoveUser(uint32_t session, const std::vector<std::string>& path)
    {
        Impl* d = _impl.get();
        asio::post(d->io, [d, session, path]() { d->pendingMoves[session] = path; if (d->synced) d->ProcessPending(); });
    }

    void MumbleBot::Kick(uint32_t session, const std::string& reason)
    {
        Impl* d = _impl.get();
        asio::post(d->io, [d, session, reason]() {
            if (!d->synced) return;
            Writer w;
            w.U32(1, session);
            w.Str(3, reason);
            d->Send(Tcp::UserRemove, w.buf);
        });
    }

    void MumbleBot::SetMute(uint32_t session, bool mute)
    {
        Impl* d = _impl.get();
        asio::post(d->io, [d, session, mute]() {
            if (!d->synced) return;
            MumbleProto::UserState us;
            us.session = session;
            us.mute = mute;
            d->Send(Tcp::UserState, us.Encode());
        });
    }

    void MumbleBot::SendText(uint32_t session, const std::string& text)
    {
        Impl* d = _impl.get();
        asio::post(d->io, [d, session, text]() {
            if (!d->synced) return;
            MumbleProto::TextMessage t;
            t.sessions.push_back(session);
            t.message = text;
            d->SendLimited(Tcp::TextMessage, t.Encode());
        });
    }

    std::vector<MumbleBot::Event> MumbleBot::PollEvents()
    {
        std::vector<Event> out;
        if (!_impl) return out;
        std::lock_guard<std::mutex> g(_impl->evMutex);
        out.swap(_impl->events);
        return out;
    }
}
