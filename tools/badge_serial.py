"""The badge's USB serial console, with the standard library only (termios)."""
import errno
import fcntl
import glob
import os
import re
import sys
import termios
import time

VID = "303a"        # Espressif's USB vendor ID
KB_CHUNK = 40       # characters per `kb hex` line (the console takes lines up to 128)


def find_port(sysfs="/sys/class/tty"):
    """The port of the one badge plugged in, found by its USB vendor ID; exits if none or several."""
    ports = []
    for path in sorted(glob.glob(f"{sysfs}/ttyACM*/device/../idVendor")):
        with open(path) as f:
            if f.read().strip() == VID:
                ports.append("/dev/" + path.split("/")[-4])
    if len(ports) != 1:
        sys.exit(f"{'several badges: ' + ', '.join(ports) if ports else 'no badge'} (USB {VID}): give its port")
    return ports[0]


class Badge:
    def __init__(self, port):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        attrs[0] = attrs[1] = attrs[3] = 0                  # raw: no input, output or line processing
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.rest = ""                                      # what came after read_until's match

    def write(self, data):
        while data:
            try:
                data = data[os.write(self.fd, data):]
            except BlockingIOError:
                time.sleep(0.01)

    def send(self, line):
        self.write(line.encode() + b"\r\n")

    def read_some(self):
        """Whatever has arrived, or b"" after a short nap (VMIN=0 and VTIME=0 give b"", not an error)."""
        try:
            data = os.read(self.fd, 4096)
        except BlockingIOError:
            data = b""
        if not data:
            time.sleep(0.01)
        return data

    def read_for(self, seconds):
        out, self.rest, end = self.rest, "", time.time() + seconds
        while time.time() < end:
            out += self.read_some().decode(errors="replace")
        return out

    def read_until(self, pattern, timeout=8.0):
        """Read until the regex `pattern` matches. Returns (output, match or None); the rest waits."""
        out, self.rest, end = self.rest, "", time.time() + timeout
        while not (m := re.search(pattern, out)) and time.time() < end:
            out += self.read_some().decode(errors="replace")
        if m:
            out, self.rest = out[:m.end()], out[m.end():]
        return out, m

    def read_echo(self, line, timeout=8.0, quiet=0.12):
        """Read until the console echoes `line`, then until `quiet` seconds pass with nothing new.
        Returns (output, echoed); the badge stops reading while it writes flash, so wait for this
        before sending the next line."""
        out, m = self.read_until(re.escape(line), timeout)
        out, self.rest = out + self.rest, ""
        if not m:
            return out, False
        end, last = time.time() + timeout, time.time()
        while time.time() - last < quiet and time.time() < end:
            data = self.read_some()
            if data:
                out, last = out + data.decode(errors="replace"), time.time()
        return out, True

    def close(self):
        os.close(self.fd)


def holders(port):
    """Other programs (of this user) with the port open."""
    real, found = os.path.realpath(port), []
    for pid in filter(str.isdigit, os.listdir("/proc")):
        if int(pid) == os.getpid():
            continue
        try:
            if any(os.readlink(f"/proc/{pid}/fd/{f}") == real for f in os.listdir(f"/proc/{pid}/fd")):
                with open(f"/proc/{pid}/comm") as f:
                    found.append(f"{f.read().strip()} (pid {pid})")
        except OSError:
            pass
    return found


def open_badge(port=None):
    """The badge on `port` (default: find_port()), locked for this program; exits with why if it can't."""
    port = port or find_port()
    others = holders(port)
    if others:
        sys.exit(f"{port} is in use by {', '.join(others)}: close it there first")
    try:
        badge = Badge(port)
        fcntl.flock(badge.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        sys.exit(f"{port} is locked by another program (a serial terminal?): close it there first")
    except OSError as e:
        hint = {"EBUSY": " (another program has it: close it there first)",
                "EACCES": " (no permission: are you in the dialout group?)",
                "ENOENT": " (is the badge plugged in?)"}.get(errno.errorcode.get(e.errno), "")
        sys.exit(f"can't open {port}: {e.strerror}{hint}")
    return badge


def kb_lines(text):
    """The `kb hex` console lines that type text into the open text field: printable ASCII, line breaks as spaces."""
    text = "".join(c for c in re.sub(r"\r\n?|\n", " ", text) if " " <= c <= "~")
    return ["kb hex " + text[i:i + KB_CHUNK].encode().hex() for i in range(0, len(text), KB_CHUNK)]
