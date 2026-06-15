*This project has been created as part of the 42 curriculum by alvalien.*

## Description

`ft_irc` is an IRC server written in C++98. It accepts multiple TCP clients on one port, authenticates them with a server password, and supports channels with operator commands (KICK, INVITE, TOPIC, MODE).

The server uses a single `poll()` loop, non-blocking sockets, per-client read buffers (for fragmented commands), and an outbound write queue flushed when `POLLOUT` is ready.

## Instructions

### Build

```bash
make
```

This produces `ircserv` (required name for evaluation). A symlink `ft_irc` is also created for convenience.

### Run

```bash
./ircserv <port> <password>
```

Example:

```bash
./ircserv 6667 mysecret
```

### Test with netcat

```bash
nc -C 127.0.0.1 6667
PASS mysecret
NICK alice
USER alice 0 * :Alice Example
JOIN #42
PRIVMSG #42 :Hello channel
TOPIC #42 :New topic
MODE #42 +t
PART #42
QUIT
```

Partial command test (subject example):

```bash
nc -C 127.0.0.1 6667
# type: com, Ctrl+D, man, Ctrl+D, d, Enter
```

### Test with an IRC client

Configure your client (irssi, HexChat, etc.) to connect to `127.0.0.1`, set the server password, then register with PASS / NICK / USER as usual.

## Implemented commands

| Command | Description |
|---------|-------------|
| PASS | Server password |
| NICK / USER | Registration and welcome (001–004) |
| JOIN / PART | Channels (with key, invite-only, user limit) |
| PRIVMSG | Channel and private messages |
| TOPIC | View/set topic (+t restricts to operators) |
| KICK / INVITE | Operator actions |
| MODE | Channel modes `i`, `t`, `k`, `o`, `l` |
| PING / PONG | Keepalive |
| QUIT | Disconnect |
| CAP | Minimal LS/END for modern clients |

## Bonus: ft_bot

A built-in bot answers commands without a separate process:

- Private: `PRIVMSG ft_bot :!help` (or `!ping`, `!info`, `!time`)
- In a channel: `PRIVMSG #chan :!help` (any message starting with `!`)

The bot nickname is `ft_bot`. Bonus is only graded if the mandatory part is perfect during evaluation.

## Resources

- [RFC 1459](https://www.rfc-editor.org/rfc/rfc1459) — Internet Relay Chat Protocol
- [RFC 2812](https://www.rfc-editor.org/rfc/rfc2812) — IRC client protocol (modern reference)
- Subject PDF: `irc.pdf`
- Beej's Guide to Network Programming

### AI usage

AI was used to review the subject requirements, complete missing mandatory features (MODE, command routing, `poll`/POLLOUT write queue), fix disconnect/write ordering, and draft this README. All code was reviewed and compiled locally before submission.
