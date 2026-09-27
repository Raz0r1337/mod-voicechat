# Murmur (Mumble server) for mod-voicechat

> ⚠️ **WARNING: This project is at a very early stage. Everything is still untested and unverified. It is NOT released for playing!**

🇩🇪 [Deutsche Version](README.md)

Murmur is the voice server. It runs separately and can live on the same machine as AzerothCore. Players need **no** Mumble client; `voice.dll` takes care of that.

Important in every variant:
- **`opusthreshold=0`**, because `voice.dll` only speaks Opus.
- **Open TCP and UDP 64738** in the firewall. Without UDP, voice runs over TCP, which also works but has slightly more latency.

## Variant A: Linux package (Debian/Ubuntu)

```sh
sudo apt install mumble-server
sudo cp /etc/mumble/mumble-server.ini /etc/mumble/mumble-server.ini.orig   # backup
sudo nano /etc/mumble/mumble-server.ini   # copy the values from mumble-server.ini.example
sudo systemctl restart mumble-server
sudo systemctl status mumble-server
```

- **Configuration:** `/etc/mumble/mumble-server.ini` (older packages up to Mumble 1.4: `/etc/mumble-server.ini`)
- **Log:** `sudo journalctl -u mumble-server -f`
- **SuperUser password** (required for the module, the bot logs in with it): `sudo mumble-server -ini /etc/mumble/mumble-server.ini -supw <password>`

## Variant B: Docker

```sh
cd external/murmur
docker compose up -d
docker compose logs -f
```

The values are set as `MUMBLE_CONFIG_*` variables in `docker-compose.yml`.

## Variant C: Windows

1. Run the official Mumble installer from [mumble.info](https://www.mumble.info/downloads/) and select only the **Server** component.
2. Put `mumble-server.ini.example` next to the server exe as `mumble-server.ini`. If needed, enable `database` and `logfile`; they are commented out there.
3. Start it: `mumble-server.exe -ini mumble-server.ini`

## Checking that the server works

Use `voicecli` from the `voice-win32` artifact (or build it yourself, see `external/client/README.en.md`). The tool does not receive its own signal, so use two windows:

```bat
voicecli.exe --host <server-ip> --user Listener --expect-tone 440 --seconds 8
voicecli.exe --host <server-ip> --user Sender --send-tone 440 --seconds 4
```

The listener must report `PASS`, and its output should show `udp=yes`.

## Channels and ACLs (phase 5, automatic)

You do not have to create anything in Murmur by hand. The module's bot logs in as `SuperUser` and creates everything itself:

- `WoW/` with this ACL:
  - Nobody may enter channels on their own, speak, write text, listen in (Listen) or create channels.
  - Speaking is only allowed in the own channel (group `in`).
  - Whispering is allowed so that group members hear each other across maps. The worldserver decides which voices a client plays.
- `WoW/Lobby`: the bot sits here, and clients may write it the binding nonce here.
- `WoW/<realm>/Map-<id>[/Inst-<id>]`: these channels are created on demand. The bot deletes empty channels after `Voice.Bot.EmptyChannelTimeout` seconds.

Mumble users without a binding to a character are kicked after `Voice.BindTimeoutSeconds`. Put exceptions (e.g. admins with a regular Mumble client) into `Voice.AllowedExternalUsers`.

**Rate limits:** Murmur silently drops channel, ACL and text messages that come too fast (`messagelimit`/`messageburst`). The bot therefore throttles itself (`Voice.Bot.MessageRate`/`MessageBurst`). If you set the Murmur values lower than the defaults, lower the bot values as well.

**Autoban:** by default Murmur bans an IP after 10 connections within 120 s, successful logins included. Players behind a shared IP (LAN, NAT) would get banned this way. The template therefore sets `autobanSuccessfulConnections=false`. After errors, the bot reconnects with backoff (5 s to 60 s), so it never locks itself out.
