# Recomp Hub

One launcher for all three recomps:

| Game | Recomp |
|---|---|
| Wii Sports Resort | [ResortRecomp](https://github.com/Hottuck71/ResortRecomp) |
| Wii Party | [PartyRecomp](https://github.com/Hottuck71/PartyRecomp) |
| Mario Party 9 | [Party9Recomp](https://github.com/Hottuck71/Party9Recomp) |

Recomp Hub contains no game code or data, and it downloads none: each recomp's release contains none either, and
the game is built on your PC from your own disc.

**Download:** [RecompHub.exe](https://github.com/Hottuck71/RecompHub/releases/latest/download/RecompHub.exe) from the
[latest release](https://github.com/Hottuck71/RecompHub/releases/latest). It is one file; nothing to install.

## Using it

Put `RecompHub.exe` in a folder with plenty of free space (about 20 GB per game) and open it.

1. **Pick a game** on the left.
2. **Get the game files.** *Download & install* fetches that recomp's newest release from GitHub (about 160 MB)
   into `Games\<recomp>` next to the hub. If you already unpacked a recomp, use *Use a folder I already have*
   instead. *Update to newest* later keeps your settings, save and build.
3. **Build.** Pick your disc image (.iso, .wbfs, .rvz...). DolphinTool (from Dolphin) unpacks it, and the hub
   finds it by itself when it can. Press *Build*. The first build takes 30 minutes or more. A wrong disc is
   reported straight away.
4. **Play** opens that game's own launcher: graphics, controllers, Mii Maker, save data and online rooms.
5. **Uninstall** (next to *Update to newest*) deletes a game the hub installed: its files, the built game and its
   save. Export your Miis and records first with *Save data* in the game's launcher if you want to keep them. For a
   folder you picked with *Use a folder I already have*, it only removes it from the hub and deletes nothing. Your
   disc image is never touched.

## Online

One relay server works for all three games.

- **Set up my server for me** runs the relay hidden on this PC, adds a Windows Firewall rule (Windows asks first),
  asks your router to forward TCP port 51020 (UPnP), shows your addresses, and points every installed game at the
  server. *Copy address for friends* copies the internet address to send to friends. If your router has UPnP
  turned off, the hub tells you to forward the port yourself. People on your own Wi-Fi can play without that.
- **Start my server automatically when I sign in to Windows** runs `RecompHub.exe --server` at sign-in.
- **Uninstall server** undoes all of that: stops the server, turns off starting at sign-in, closes the router port
  if the hub opened it, removes the firewall rule (Windows asks), sets games that used it back, and deletes `Server\`.
- **Join a friend's server**: type the address they sent and press *Use this server*. Every installed game then
  uses it. Then, in a game: Online, Join, and the host's room code.

## Building it

Needs MinGW-w64 gcc. The toolchain in any recomp's `builder\toolchain` works:

```powershell
.\build.ps1 -Toolchain "D:\Mario Party 9 PC\builder\toolchain" -Test
```

`-Test` also runs `tests\hub_test.c`: settings files, GitHub release lookups, unpack layouts, running programs,
and finding DolphinTool. The output is `dist\RecompHub.exe`, a single file with no DLLs.

## Adding another recomp

The games are one table near the top of `src/hub.c`:

```c
static const Game GAMES[] = {
    {L"Wii Sports Resort", L"ResortRecomp", L"Hottuck71/ResortRecomp", L"Wuhu Island's sports: ..."},
    ...
};
#define NGAMES 3
```

Add a line (title, folder name, GitHub `owner/repo`, one-line description), raise `NGAMES`, and build. The sidebar has
room for four games. The recomp must work the way these three do: a GitHub release with a `.zip` holding
`Launcher.exe`, that launcher accepting `--test-build <disc>`, and the built game named `<folder name>-Game.exe`.

## How it works

- Building runs `Launcher.exe --test-build <disc>` in the game's folder: that recomp's own headless setup. While it
  runs, the hub shows the newest line of the game's `build-log.txt`.
- The hub's settings are in `RecompHub.ini` next to it. It changes only two keys in each game's `launcher.ini`:
  `dolphin_tool` and `relay`.
- The relay is `wsr_relay.exe` from an installed recomp, copied to `Server\`. Its log is `Server\relay-log.txt`.
