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
sudo cp mumble-server.ini.example /etc/mumble-server.ini    # or just copy the values
sudo systemctl restart mumble-server
sudo systemctl status mumble-server
```

- **Log:** `/var/log/mumble-server/mumble-server.log`
- **SuperUser password** (only needed from phase 5 on, for the bot): `sudo mumble-server -ini /etc/mumble-server.ini -supw <password>`

## Variant B: Docker

```sh
cd external/murmur
docker compose up -d
docker compose logs -f
```

The values are set as `MUMBLE_CONFIG_*` variables in `docker-compose.yml`.

## Variant C: Windows

1. Run the official Mumble installer from [mumble.info](https://www.mumble.info/downloads/) and select only the **Server** component.
2. Put `mumble-server.ini.example` next to the server exe as `mumble-server.ini`, and remove or adjust the Linux paths for `database`/`logfile`.
3. Start it: `mumble-server.exe -ini mumble-server.ini`

## Checking that the server works

Use `voicecli` from the `voice-win32` artifact (or build it yourself, see `external/client/README.en.md`). The tool does not receive its own signal, so use two windows:

```bat
voicecli.exe --host <server-ip> --user Listener --expect-tone 440 --seconds 8
voicecli.exe --host <server-ip> --user Sender --send-tone 440 --seconds 4
```

The listener must report `PASS`, and its output should show `udp=yes`.

## What comes later

- **From phase 5 on:** channel structure and ACLs so that everyone only hears their own map or instance, plus a bot account for AzerothCore. The template and setup script will follow here.
