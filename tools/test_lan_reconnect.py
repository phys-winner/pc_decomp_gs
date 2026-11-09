"""On-device regression: kill a LAN client, recover the host, reconnect and start."""
import argparse
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = "org.griefsyndrome.port"


def adb(serial, *args):
    return subprocess.check_output(["adb", "-s", serial, *args], text=True, encoding="utf-8")


def snapshot(serial):
    try:
        result = adb(serial, "shell", "uiautomator", "dump", "/sdcard/grief-lan-reconnect-test.xml")
    except subprocess.CalledProcessError:
        return None
    if "dumped to" not in result:
        return None
    return adb(serial, "shell", "cat", "/sdcard/grief-lan-reconnect-test.xml")


def button(xml, text):
    return next((n for n in ET.fromstring(xml).iter("node") if n.get("text", "").upper() == text), None)


def wait_for(serial, predicate, name, out, timeout=35):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        xml = snapshot(serial)
        if xml and predicate(xml):
            (out / (name + ".xml")).write_text(xml, encoding="utf-8")
            print("PASS: " + name, flush=True)
            return xml
        time.sleep(0.5)
    raise AssertionError("Timed out: " + name)


def ready(xml):
    start = button(xml, "НАЧАТЬ ИГРУ")
    return start is not None and start.get("enabled") == "true"


def waiting(xml):
    start = button(xml, "НАЧАТЬ ИГРУ")
    return (start is not None and start.get("enabled") == "false"
            and "Игра создана." in xml and button(xml, "ПОВТОРИТЬ ПОДКЛЮЧЕНИЕ") is None)


def tap_start(serial, xml):
    start = button(xml, "НАЧАТЬ ИГРУ")
    assert start is not None and start.get("enabled") == "true"
    x1, y1, x2, y2 = map(int, re.findall(r"\d+", start.get("bounds")))
    adb(serial, "shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))


def launch(serial, host, address):
    adb(serial, "shell", "am", "force-stop", PACKAGE)
    adb(serial, "shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb(serial, "shell", "wm", "dismiss-keyguard")
    adb(serial, "shell", "am", "start", "-n", PACKAGE + "/.GameActivity",
        "--ez", "lan", "true", "--ez", "lanHost", str(host).lower(),
        "--es", "lanAddress", address, "--ei", "lanPlayers", "3",
        "--es", "language", "ru", "--es", "userdata", "lan-reconnect-test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host")
    parser.add_argument("client")
    parser.add_argument("address")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    assert args.host != args.client
    args.out.mkdir(parents=True, exist_ok=True)
    launch(args.host, True, "127.0.0.1")
    wait_for(args.host, waiting, "initial-host-listening", args.out)
    launch(args.client, False, args.address)
    xml = wait_for(args.host, ready, "initial-client-connected", args.out)
    for cycle in range(2):
        adb(args.client, "shell", "am", "force-stop", PACKAGE)
        # Use the previously enabled button before the host can detect the dead client.
        tap_start(args.host, xml)
        wait_for(args.host, waiting, f"host-reopened-{cycle}", args.out)
        launch(args.client, False, args.address)
        xml = wait_for(args.host, ready, f"client-reconnected-{cycle}", args.out)
    adb(args.client, "shell", "am", "force-stop", PACKAGE)
    wait_for(args.host, lambda x: not ready(x) and "Клиент отключился." in x,
             "idle-disconnect-detected", args.out)
    launch(args.client, False, args.address)
    xml = wait_for(args.host, ready, "idle-client-reconnected", args.out)
    tap_start(args.host, xml)
    wait_for(args.host, lambda x: button(x, "НАЧАТЬ ИГРУ") is None and any(
        n.get("class") == "android.view.SurfaceView" and n.get("package") == PACKAGE
        for n in ET.fromstring(x).iter("node")),
             "recovered-host-started-game", args.out)
    pid = adb(args.host, "shell", "pidof", PACKAGE + ":game").strip()
    assert pid
    log = adb(args.host, "logcat", "-d", "--pid", pid, "-s", "GriefSyndrome")
    (args.out / "host.log").write_text(log, encoding="utf-8")
    assert log.count("LAN host reopening after disconnected session") >= 2
    assert log.count("LAN advertised:") >= 3
    print("PASS: two abrupt-disconnect/start failures recover automatically; idle disconnect/rejoin and game start succeed", flush=True)


if __name__ == "__main__":
    main()
