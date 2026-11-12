#!/usr/bin/env python3
"""Drive the badge's screen from this PC's keyboard, over USB, like a remote control.

  tools/badge_remote.py                  # the badge found by USB ID
  tools/badge_remote.py /dev/ttyACM1     # this port

Key mode (at start): the arrow keys move the badge's selection, Enter selects,
Esc goes back, and Home, F1 (or h) go to the home screen. While a text field is
open (an app's Name, say) letters and pastes type into it, Backspace erases,
Enter presses OK and Esc cancels. Only the badge's errors show.
Uses the console's `key` and `kb` commands; `kb` alone says whether a field is open.

Command mode: F2 (or Ctrl+T) switches to a `sol> ` prompt and back. Type a console
command (Backspace, Ctrl+U or Esc clear, Up/Down recall this session's commands)
and Enter sends it; all the badge's output shows, as it arrives. A paste goes into
the line, its newlines as spaces. Nothing typed is saved anywhere.
Ctrl+C or Ctrl+Q quits.
Only the standard library (badge_serial.py drives the port with termios).
"""
import argparse
import codecs
import os
import re
import select
import sys
import termios
import time
import tty

from badge_serial import kb_lines, open_badge

ESC_WAIT = 0.05     # a bare Esc: no more bytes this soon after it
STALE = 1.0         # seconds a "kb: open|closed" reply stays good
ARROWS = {"A": "up", "B": "down", "C": "right", "D": "left"}
ECHO = {"up": "↑", "down": "↓", "left": "←", "right": "→", "select": "⏎", "back": "⎋", "home": "⌂"}
PROMPT = "sol> "
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
ERROR = re.compile(r"error|unknown|unrecognized|usage|no keyboard|busy|not hex|hex digits|^E \(", re.I)


class Keys:
    """Bytes from the terminal in, events out: ("key", name), ("enter"|"esc"|"bs"|"clear"|"mode"|"quit", None),
    ("text", typed) and ("paste", pasted). feed(b"", final=True) ends a sequence left waiting."""

    def __init__(self):
        self.utf8 = codecs.getincrementaldecoder("utf-8")("ignore")
        self.rest = ""
        self.paste = False

    def feed(self, data, final=False):
        s, out, i = self.rest + self.utf8.decode(data), [], 0
        self.rest = ""
        while i < len(s):
            if self.paste:
                end = s.find("\x1b[201~", i)
                if end < 0:
                    keep = 0 if final else next((k for k in range(min(5, len(s) - i), 0, -1) if "\x1b[201~".startswith(s[-k:])), 0)
                    out.append(("paste", s[i:len(s) - keep]))
                    self.rest = s[len(s) - keep:]
                    break
                out.append(("paste", s[i:end]))
                self.paste, i = False, end + 6
                continue
            c = s[i]
            if c == "\x1b":
                ev, n = self.escape(s[i + 1:])
                if n is None:                       # cut short: wait for the rest, or it was a bare Esc
                    if not final:
                        self.rest = s[i:]
                        break
                    ev, n = ("esc", None), 0
                if ev == "paste":
                    self.paste = True
                elif ev:
                    out.append(ev)
                i += 1 + n
                continue
            if c in "\x03\x11":
                out.append(("quit", None))
                break
            if c in "\r\n":
                out.append(("enter", None))
                i += 2 if s[i:i + 2] == "\r\n" else 1
                continue
            if c in "\x7f\x08":
                out.append(("bs", None))
            elif c == "\x14":
                out.append(("mode", None))
            elif c == "\x15":
                out.append(("clear", None))
            elif c >= " ":
                out.append(("text", c))
            i += 1
        return [e for e in out if e[0] not in ("text", "paste") or e[1]]

    @staticmethod
    def escape(s):
        """After an Esc: (event or None, characters used), or (None, None) when cut short."""
        if not s:
            return None, None
        if s[0] == "O":                             # SS3: \x1bOA, \x1bOH, \x1bOP (F1), \x1bOQ (F2)
            if len(s) < 2:
                return None, None
            k = s[1]
            return (("key", ARROWS[k]) if k in ARROWS else ("key", "home") if k in "HP"
                    else ("mode", None) if k == "Q" else None), 2
        if s[0] != "[":
            return ("esc", None), 0                 # Esc, then a key of its own
        if s[1:2] == "[":                           # the Linux console's F1, F2: \x1b[[A, \x1b[[B
            if len(s) < 3:
                return None, None
            return {"A": ("key", "home"), "B": ("mode", None)}.get(s[2]), 3
        j = 1
        while j < len(s) and "\x20" <= s[j] <= "\x3f":
            j += 1
        if j == len(s):
            return None, None
        k, args = s[j], s[1:j].split(";")
        if k in ARROWS:
            ev = ("key", ARROWS[k])
        elif k in "HP" or (k == "~" and args[0] in ("1", "7", "11")):
            ev = ("key", "home")
        elif k == "Q" or (k == "~" and args[0] == "12"):
            ev = ("mode", None)
        elif k == "~" and args[0] == "200":
            ev = "paste"
        else:
            ev = None
        return ev, j + 1


class Remote:
    def __init__(self, badge):
        self.badge, self.buf = badge, b""
        self.kb, self.checked = None, 0.0
        self.cmd, self.edit, self.history, self.back, self.echo = False, "", [], 0, None

    def pump(self, seconds=0.0, until=lambda: False):
        """Read what the badge says, for up to this long: errors show, `kb` replies set the state."""
        end = time.time() + seconds
        while True:
            data = self.badge.read_some()
            if not data:
                if until() or time.time() >= end:
                    return
                continue
            self.buf += data
            *lines, self.buf = self.buf.split(b"\n")
            for line in lines:
                self.line(ANSI.sub("", line.decode(errors="replace")).replace("\r", ""))

    def line(self, raw):
        text = re.sub(r"^(sol> ?)+", "", raw).rstrip()
        if self.cmd:
            if text and text.strip() == self.echo:      # the console echoing our line
                self.echo = None
            elif text or not raw.startswith("sol>"):
                say("\r\x1b[K" + text + "\r\n" + PROMPT + self.edit)
        text = text.strip()
        m = re.search(r"kb: (open|closed)\b", text)
        if m or "no keyboard is open" in text:
            self.kb, self.checked = bool(m) and m[1] == "open", time.time()
        if not self.cmd and not m and ERROR.search(text) and not text.startswith("Command returned non-zero"):
            say("\r\n" + text + "\r\n")

    def is_open(self):
        if self.kb is None or time.time() - self.checked > STALE:
            self.kb = None
            self.badge.send("kb")
            self.pump(0.5, lambda: self.kb is not None)
            if self.kb is None:
                self.kb, self.checked = False, time.time()
        return self.kb

    def keys(self, names):
        for i in range(0, len(names), 8):
            self.badge.send("key " + " ".join(names[i:i + 8]))
        self.kb = None                              # a press may open or close a field
        say(" ".join(ECHO[n] for n in names) + " ")

    def kb_cmd(self, line, echo):
        self.badge.send(line)
        self.pump(0.05)
        say(echo + " ")

    def type(self, text):
        lines = kb_lines(text)
        for line in lines:
            self.badge.send(line)
            self.pump(0.05)
        if lines:
            say("·")

    def command(self, kind, val):
        if kind in ("text", "paste"):
            add = "".join(c for c in val.replace("\r\n", " ").replace("\r", " ").replace("\n", " ") if c >= " ")
            self.edit += add
            say(add)
        elif kind == "bs" and self.edit:
            self.edit = self.edit[:-1]
            say("\b \b")
        elif kind in ("esc", "clear") or (kind == "key" and val in ("up", "down")):
            if kind == "key":
                self.back = max(0, self.back - 1) if val == "up" else min(len(self.history), self.back + 1)
                self.edit = self.history[self.back] if self.back < len(self.history) else ""
            else:
                self.edit = ""
            say("\r\x1b[K" + PROMPT + self.edit)
        elif kind == "enter":
            line, self.edit = self.edit, ""
            say("\r\n")
            if line.strip():
                if not self.history or self.history[-1] != line:
                    self.history.append(line)
                self.echo = line.strip()
                self.badge.send(line)
            self.back = len(self.history)
            say(PROMPT)

    def toggle(self):
        self.cmd, self.edit, self.back = not self.cmd, "", len(self.history)
        say("\r\n-- command mode (F2: keys) --\r\n" + PROMPT if self.cmd else "\r\n-- key mode (F2: commands) --\r\n")

    def run(self, events):
        i = 0
        while i < len(events):
            kind, val = events[i]
            j = i + 1
            while j < len(events) and events[j][0] == kind and kind in ("key", "bs", "text", "paste"):
                j += 1
            group = [v for _, v in events[i:j]]
            if kind == "quit":
                return False
            if kind == "mode":
                self.toggle()
            elif self.cmd:
                for v in group:
                    self.command(kind, v)
            elif kind == "clear":
                pass
            elif kind == "key":
                self.keys(group)
            elif kind in ("enter", "esc"):
                for _ in group:
                    if self.is_open():
                        self.kb_cmd("kb ok" if kind == "enter" else "kb cancel", "ok" if kind == "enter" else "cancel")
                        self.kb = None
                    else:
                        self.keys(["select" if kind == "enter" else "back"])
            elif kind == "bs":
                if self.is_open():
                    self.kb_cmd("kb del" + (f" {len(group)}" if len(group) > 1 else ""), "⌫" * len(group))
                else:
                    self.keys(["back"] * len(group))
            elif self.is_open():
                self.type("".join(group))
            elif kind == "text" and "h" in group:
                self.keys(["home"] * group.count("h"))
            i = j
        return True


def say(text):
    sys.stderr.write(text)
    sys.stderr.flush()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="the badge's port, e.g. /dev/ttyACM0 (default: found by its USB ID)")
    a = ap.parse_args()
    if not sys.stdin.isatty():
        sys.exit("run this in a terminal: it reads the keys as you press them")
    badge = open_badge(a.port)
    remote = Remote(badge)
    badge.send("")                                  # the first line after opening often arrives garbled
    badge.read_for(0.3)
    badge.send("kb")
    remote.pump(0.5, lambda: remote.kb is not None)
    if remote.kb is None:
        print("the badge didn't answer kb yet (keys still go to it)", file=sys.stderr)
    print("badge remote, key mode: arrows move, Enter select/OK, Esc back/cancel, Backspace back/erase, "
          "Home/F1 (or h) home, type while a text field is open; F2 or Ctrl+T: command mode; Ctrl+Q quits",
          file=sys.stderr)
    fd = sys.stdin.fileno()
    saved = termios.tcgetattr(fd)
    tty.setraw(fd)
    say("\x1b[?2004h")                              # bracketed paste: a pasted newline isn't Enter
    keys, last = Keys(), 0.0
    try:
        while True:
            wait = ESC_WAIT if keys.rest else 0.2
            ready, _, _ = select.select([fd, badge.fd], [], [], wait)
            if badge.fd in ready:
                remote.pump()
            if fd in ready:
                events, last = keys.feed(os.read(fd, 4096)), time.time()
            elif keys.rest and time.time() - last >= ESC_WAIT:
                events = keys.feed(b"", final=True)
            else:
                continue
            if not remote.run(events):
                break
    finally:
        say("\x1b[?2004l")
        termios.tcsetattr(fd, termios.TCSADRAIN, saved)
        print(file=sys.stderr)
        badge.close()


if __name__ == "__main__":
    main()
