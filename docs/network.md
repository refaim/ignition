# Network play

Two to six copies of the game can race each other over a network. One
copy hosts the race; the others join it. The host's computer drivers
fill the grid up to six cars.

## Play over a network

All players must have the same game data.

1. On the host, choose **MULTI PLAYER**, then **NETWORK GAME**. Choose
   the race type, your car and the track. The network page shows
   **WAITING FOR PLAYERS**.
2. On each other computer, choose **MULTI PLAYER**, then **NETWORK
   GAME**, then your car. The network page shows **CONNECTING TO
   HOST...**, then **WAITING FOR PLAYERS**.
3. Each player's badge and name appear on the host's network page. When
   it shows **PRESS RETURN TO START**, the host presses Return, and the
   race starts on every computer.

**NETWORK GAME** looks for a game on the local network for one and a
half seconds. It joins the first game it finds, or hosts one when there
is none. To choose, start the game with one of these options:

| Option | Effect |
|---|---|
| `--host` | NETWORK GAME always hosts. |
| `--connect HOST` | NETWORK GAME joins the game at `HOST`, a name or an IP address, with `:PORT` or without. It goes back a page if there is no game there. |

The host uses UDP port 47624, which must be open in its firewall. The
game finds a host on the same network by broadcast; on another network,
use `--connect`.

On a network page, the game takes no key but Return on the host, as the
original does. After the race, every player goes back to the main menu,
and the game is over: start a new NETWORK GAME for the next race.

### Platforms

- macOS, Linux, Windows, Android and iOS play over the network.
- The PlayStation Vita plays over its Wi-Fi. It has no command line,
  so it finds a host by broadcast, on the same network.
- The web version has no network play: a browser cannot send UDP.
  NETWORK GAME goes back a page.

## How the original plays

`IGN_WIN.EXE` imports `DPLAYX.DLL` by ordinal: 1 is `DirectPlayCreate`
and 2 is `DirectPlayEnumerateA`. It uses the `IDirectPlay2A` interface
of DirectPlay 3. Its dialog (resource 0x66) lists only the IPX service
provider (0x45a7a0 keeps `DPSPGUID_IPX`), so the original plays over
IPX, on a local network.

The port keeps the game's own routines, messages and timing, and
replaces DirectPlay with `src/dplay.c`, which has the same calls,
results and system messages over UDP. The dialog's choices (Search,
Join, Host, Cancel) are made by the port: see `net_dialog_run` in
`src/net.c`.

The code:

- `src/net.c`: the session and the player table (0x459d30 to
  0x45acd0), the dialog's procedure, and the game's messages
  (0x40ce90 to 0x40e690).
- `src/dplay.c`: the stand-in for DirectPlay.
- `src/platform/udp.c`: the UDP socket (BSD sockets and Winsock). The
  Vita's is in `src/platform/vita.c`.
- `src/menu/pages.c`: the network page (0x40af54, in handler A).
- `src/race/race.c`: the race's waiting stages and the car exchange.
- `src/race/contact.c`: the ground under the cars the network moves
  (0x426b40).

### Players

Each copy keeps a table of the other players, up to 23 (the session's
size). A player keeps its index for the whole game; the table only
grows, and a player that leaves is marked inactive.

On the host, player `i` drives car `i + 1`, and the host drives car 0.
The host tells each client its car with message 1. A client drives car
0 on its own screen, so it swaps its car and car 0 in everything the
host sends (`net_slot`, 0x40e690).

### Messages

Each message is a block of little-endian fields. Its first `s32` is its
kind. The original sends none of them guaranteed, so it sends most of
them again until it gets an answer.

| Kind | From | Size | Content |
|---|---|---|---|
| 1 | host | 8 | the client's car |
| 3 | client | 4 | ready to start (race) |
| 4 | host | 4 | start (race) |
| 5 | both | 0xc6 | a car (`struct net_car`) |
| 6 | host | 0xf4 | the race's settings (`struct net_settings`) |
| 7 | client | 0x1b | the client's car, control and name |
| 8 | host | 4 | leave the menu for the race |
| 9 | client | 4 | the race is loaded |
| 10 | host | 4 | every client has loaded |

In the menu, every sixth frame, the host sends each client messages 1
and 6, and reads one message 7. A client answers each message with a
message 7. When the host starts the race, it sends message 8.

The race waits in stages (`struct net_game`, `stage`):

1. Stage 2: each client sends message 9 every sixth frame. When all
   have, the host sends message 10. The screen shows **WAITING FOR
   OTHER PLAYERS**.
2. Stage 3: each client sends message 3 every eleventh frame. When all
   have, the host sends message 4.
3. Stage 4: the race. Every frame the host sends each client every car,
   and each client sends the host its own car (message 5). A car's
   message also carries the race's message: the host quit (0x64), the
   host restarts (0x65), a player quit (0x66), or a player is out of an
   elimination race (3).

### The session protocol

`src/dplay.c` sends each message in a UDP datagram with a 20-byte
header:

| Offset | Size | Content |
|---|---|---|
| 0x00 | 4 | `IGNP` |
| 0x04 | 1 | the packet's kind |
| 0x05 | 1 | the version, 1 |
| 0x06 | 2 | the payload's size |
| 0x08 | 4 | the session's number |
| 0x0c | 4 | from: a player id |
| 0x10 | 4 | to: a player id, 0 for everyone |

| Kind | Name | Content |
|---|---|---|
| 1 | ENUM | A client asks for games, by broadcast. |
| 2 | SESSION | The host's answer: flags, size, players and name. |
| 3 | JOIN | A client asks to join, with its name; `from` is a random number. |
| 4 | WELCOME | The host's answer: the new player's id in `to`. |
| 5 | PLAYERS | The host's table of players: id, address, host flag and name. |
| 6 | DATA | One game message. |
| 7 | KEEPALIVE | A client is still there. |
| 8 | LEAVE | A player leaves; from the host, the game ends. |

The host sends the table of players twice a second; each client sends
a keep-alive twice a second. A player that the host has not heard from
for 15 seconds is dropped. A client that has not heard from the host
for 15 seconds has lost the game.

## Known limits

- DirectPlay moved a game to another host when its host left. The port
  ends the game instead.
- There is no way through NAT: the host and the clients must reach each
  other's UDP port directly.
- The cars' lap and finish data come from the network, as in the
  original. If packets are lost, a remote car's position jumps.
