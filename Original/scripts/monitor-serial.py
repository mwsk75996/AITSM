#!/usr/bin/env python3
"""Find og stream den serielle konsol-output fra en Thingy:91 X.

Thingy:91 X eksponerer to USB CDC ACM-porte. Applikationskonsollen (UART0)
ligger på den laveste CDC-interface (typisk ``-if01`` -> ``ttyACM0``), mens den
anden port (``-if04``) ikke viser applikationsloggen. Scriptet finder selv den
rigtige port via ``/dev/serial/by-id`` og streamer outputtet, så firmwarens log
kan følges lokalt.

Brug:
    scripts/monitor-serial.py                 # auto-detektér og stream
    scripts/monitor-serial.py --list          # vis fundne porte
    scripts/monitor-serial.py -p /dev/ttyACM0 # vælg port manuelt
    scripts/monitor-serial.py -b 921600       # anden baudrate

Kræver ``pyserial`` (``pip install pyserial`` eller ``apt install python3-serial``).
"""

from __future__ import annotations

import argparse
import glob
import re
import sys

BY_ID_GLOB = "/dev/serial/by-id/*"
RAW_GLOBS = ("/dev/ttyACM*", "/dev/ttyUSB*")
MATCH_SUBSTRINGS = ("thingy", "nordic")
DEFAULT_BAUD = 115200

INTERFACE_RE = re.compile(r"-if(\d+)$")


class DetectionError(Exception):
    """Rejst når den rigtige port ikke entydigt kan bestemmes."""


def interface_number(link: str) -> int:
    """Returnér CDC-interfacetallet fra et by-id-navn (fx ``...-if01`` -> 1)."""
    match = INTERFACE_RE.search(link)
    return int(match.group(1)) if match else 1_000


def physical_device(link: str) -> str:
    """Fjern interface-suffikset, så to porte på samme enhed får samme nøgle."""
    return INTERFACE_RE.sub("", link)


def pick_console_by_id(links: list[str]) -> str | None:
    """Vælg konsolporten blandt by-id-links.

    Returnerer ``None`` hvis ingen Thingy/Nordic-enhed findes, og rejser
    ``DetectionError`` hvis flere fysiske enheder gør valget tvetydigt.
    """
    thingy = [link for link in links if any(s in link.lower() for s in MATCH_SUBSTRINGS)]
    if not thingy:
        return None

    devices: dict[str, list[str]] = {}
    for link in thingy:
        devices.setdefault(physical_device(link), []).append(link)

    if len(devices) > 1:
        raise DetectionError(
            "Flere Thingy:91 X-enheder fundet; angiv --port for at vælge."
        )

    links = next(iter(devices.values()))
    links.sort(key=interface_number)
    return links[0]


def detect_port() -> str:
    """Find konsolporten via by-id med rå ttyACM/ttyUSB som fallback."""
    by_id = sorted(glob.glob(BY_ID_GLOB))
    console = pick_console_by_id(by_id)
    if console is not None:
        return console

    raw = sorted({p for pattern in RAW_GLOBS for p in glob.glob(pattern)})
    if not raw:
        raise DetectionError(
            "Ingen Thingy:91 X fundet. Tjek at enheden er tændt og tilsluttet med USB."
        )
    if len(raw) > 1:
        raise DetectionError(
            "Flere serielle porte fundet (" + ", ".join(raw) + "); angiv --port."
        )
    return raw[0]


def list_ports() -> int:
    """Print fundne kandidater til fejlfinding."""
    links = sorted(glob.glob(BY_ID_GLOB))
    if not links:
        print("Ingen /dev/serial/by-id-poster fundet.", file=sys.stderr)
        return 0

    try:
        console = pick_console_by_id(links)
    except DetectionError:
        console = None

    for link in links:
        marker = " (konsol)" if link == console else ""
        print(f"{link}{marker}")
    return 0


def stream(port: str, baud: int) -> int:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        print(
            "pyserial mangler. Installér med 'pip install pyserial' "
            "eller 'sudo apt install python3-serial'.",
            file=sys.stderr,
        )
        return 1

    print(f"Lytter på {port} @ {baud} 8N1. Afbryd med Ctrl+C.", file=sys.stderr)
    with serial.Serial(port, baud, timeout=0.2) as connection:
        connection.dtr = True
        connection.rts = True
        try:
            while True:
                data = connection.read(4096)
                if data:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
        except KeyboardInterrupt:
            print("\nStopper.", file=sys.stderr)
    return 0


def run_self_test() -> int:
    """Lille indbygget test af portvalget (ingen hardware nødvendig)."""
    a = "/dev/serial/by-id/usb-Nordic_Semiconductor_Thingy:91_X_UART_AAA-if01"
    b = "/dev/serial/by-id/usb-Nordic_Semiconductor_Thingy:91_X_UART_AAA-if04"
    c = "/dev/serial/by-id/usb-Nordic_Semiconductor_Thingy:91_X_UART_BBB-if01"
    d = "/dev/serial/by-id/usb-Some_Other_Device-if00"

    checks = [
        (pick_console_by_id([b, a]) == a, "vælger laveste interface"),
        (pick_console_by_id([a]) == a, "enkel port"),
        (pick_console_by_id([d]) is None, "ignorerer fremmed enhed"),
        (pick_console_by_id([]) is None, "ingen enheder"),
        (interface_number(b) == 4, "parser interfacenummer"),
    ]

    multiple_ok = False
    try:
        pick_console_by_id([a, c])
    except DetectionError:
        multiple_ok = True
    checks.append((multiple_ok, "afviser flere fysiske enheder"))

    failed = [name for ok, name in checks if not ok]
    for ok, name in checks:
        print(f"[{'ok' if ok else 'FEJL'}] {name}")

    if failed:
        print(f"{len(failed)} self-test(s) fejlede.", file=sys.stderr)
        return 1
    print(f"Alle {len(checks)} self-tests bestod.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Find og stream Thingy:91 X' serielle konsol-output."
    )
    parser.add_argument("-p", "--port", help="Seriel port (overstyr auto-detektion).")
    parser.add_argument(
        "-b",
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"Baudrate (standard {DEFAULT_BAUD}).",
    )
    parser.add_argument("-l", "--list", action="store_true", help="Vis fundne porte.")
    parser.add_argument(
        "--self-test", action="store_true", help="Kør indbygget test af portvalget."
    )
    args = parser.parse_args()

    if args.self_test:
        return run_self_test()

    if args.list:
        return list_ports()

    port = args.port
    if not port:
        try:
            port = detect_port()
        except DetectionError as error:
            print(str(error), file=sys.stderr)
            return 1
        print(f"Auto-detekteret konsolport: {port}", file=sys.stderr)

    return stream(port, args.baud)


if __name__ == "__main__":
    sys.exit(main())
