#!/usr/bin/env python3
"""Type into the badge's open text field from this PC, over USB (the console's `kb` command).

  tools/badge_type.py "My Coin"                  # type it (paste anything here)
  tools/badge_type.py "My Coin" --ok             # and press OK
  tools/badge_type.py /dev/ttyACM1 "My Coin"     # this port, not the one found by USB ID

Open the text field on the badge first (an app's Name, say). To type live, use badge_remote.py.
"""
import argparse
import sys

from badge_serial import kb_lines, open_badge


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="the badge's port, e.g. /dev/ttyACM0 (default: found by its USB ID)")
    ap.add_argument("text", help="text to type")
    ap.add_argument("--ok", action="store_true", help="press OK after typing")
    a = ap.parse_args()
    badge = open_badge(a.port)
    try:
        badge.send("kb")
        if "kb: open" not in badge.read_for(0.3):
            sys.exit("no text field is open on the badge")
        for line in kb_lines(a.text):
            badge.send(line)
            badge.read_for(0.05)
        if a.ok:
            badge.send("kb ok")
    finally:
        badge.close()


if __name__ == "__main__":
    main()
