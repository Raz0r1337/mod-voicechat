# Murmur (Mumble-Server) für mod-voicechat

> ⚠️ **WARNUNG: Dieses Projekt befindet sich in einem sehr frühen Stadium. Alles ist noch ungetestet und ungeprüft. Es ist NICHT zum Spielen freigegeben!**

🇬🇧 [English version](README.en.md)

Murmur ist der Voice-Server. Er läuft separat und kann auf demselben Rechner wie AzerothCore stehen. Die Spieler brauchen **keinen** Mumble-Client, das übernimmt `voice.dll`.

Wichtig in jeder Variante:
- **`opusthreshold=0`**, denn `voice.dll` spricht nur Opus.
- **TCP und UDP 64738** in der Firewall freigeben. Ohne UDP läuft die Sprache über TCP, das geht auch, hat aber etwas mehr Latenz.

## Variante A: Linux-Paket (Debian/Ubuntu)

```sh
sudo apt install mumble-server
sudo cp mumble-server.ini.example /etc/mumble-server.ini    # oder nur die Werte übernehmen
sudo systemctl restart mumble-server
sudo systemctl status mumble-server
```

- **Log:** `/var/log/mumble-server/mumble-server.log`
- **SuperUser-Passwort** (erst ab Phase 5 für den Bot nötig): `sudo mumble-server -ini /etc/mumble-server.ini -supw <passwort>`

## Variante B: Docker

```sh
cd external/murmur
docker compose up -d
docker compose logs -f
```

Die Werte stehen als `MUMBLE_CONFIG_*`-Variablen in `docker-compose.yml`.

## Variante C: Windows

1. Den offiziellen Mumble-Installer von [mumble.info](https://www.mumble.info/downloads/) starten und nur die Komponente **Server** auswählen.
2. `mumble-server.ini.example` als `mumble-server.ini` neben die Server-exe legen und die Linux-Pfade bei `database`/`logfile` entfernen oder anpassen.
3. Starten: `mumble-server.exe -ini mumble-server.ini`

## Testen, ob der Server passt

Mit `voicecli` aus dem Artefakt `voice-win32` (bzw. selbst gebaut, siehe `external/client/README.md`). Das Tool empfängt dabei sein eigenes Signal nicht, deshalb zwei Fenster:

```bat
voicecli.exe --host <server-ip> --user Hoerer --expect-tone 440 --seconds 8
voicecli.exe --host <server-ip> --user Sender --send-tone 440 --seconds 4
```

Der Hörer muss `PASS` melden, und in seiner Ausgabe sollte `udp=yes` stehen.

## Was später dazukommt

- **Ab Phase 5:** Channel-Struktur und ACLs, damit jeder nur seine Map bzw. Instanz hört, dazu ein Bot-Account für AzerothCore. Vorlage und Setup-Skript folgen dann hier.
