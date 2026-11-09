from pathlib import Path
import os
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
LAUNCHER = next((path for path in (
    Path(os.environ["PORT_LAUNCHER"]) if os.environ.get("PORT_LAUNCHER") else None,
    ROOT / "android/launcher", ROOT.parent / "launcher"
) if path and (path / "tools/launcher.py").is_file()), None)
if LAUNCHER is None:
    raise SystemExit("Launcher base is missing: set PORT_LAUNCHER, supply android/launcher or place it beside the project")
sys.path.insert(0, str(LAUNCHER / "tools"))
import launcher as port_launcher


def prepare(out):
    out = Path(out)
    prepared = port_launcher.prepare(ROOT / "app/port.json", ROOT / "app/AndroidManifest.xml", out)
    shutil.copy2(ROOT / "LICENSE", Path(prepared["assets"]) / "LICENSE")
    shutil.copy2(ROOT / "THIRD_PARTY_NOTICES.md", Path(prepared["assets"]) / "THIRD_PARTY_NOTICES.md")
    package = prepared["package"]
    sources = Path(prepared["sources"][0]).parent

    state = sources / "State.java"
    text = state.read_text(encoding="utf-8")
    text = text.replace("final class State {", "public final class State {")
    text = text.replace("static final String CHOICES", "public static final String CHOICES")
    text = text.replace("static boolean put(Context", "public static boolean put(Context")
    text = text.replace("static String get(Context", "public static String get(Context")
    state.write_text(text, encoding="utf-8", newline="\n")

    work = sources / "Work.java"
    text = work.read_text(encoding="utf-8")
    text = text.replace("context.startForegroundService(new Intent(context, WorkService.class));",
        "if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(new Intent(context, WorkService.class));\n" +
        "            else context.startService(new Intent(context, WorkService.class));")
    text = text.replace("NotificationManager manager = context.getSystemService(NotificationManager.class);\n            if (manager.getNotificationChannel(CHANNEL) == null)",
        "NotificationManager manager = context.getSystemService(NotificationManager.class);\n" +
        "            if (Build.VERSION.SDK_INT < 26) return;\n            if (manager.getNotificationChannel(CHANNEL) == null)")
    text = text.replace("Notification.Builder b = new Notification.Builder(this, CHANNEL).setContentIntent(tap).setOnlyAlertOnce(true)",
        "Notification.Builder b = (Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(this, CHANNEL) : new Notification.Builder(this)).setContentIntent(tap).setOnlyAlertOnce(true)")
    work.write_text(text, encoding="utf-8", newline="\n")

    activity = sources / "LauncherActivity.java"
    text = activity.read_text(encoding="utf-8")
    old = "card.addView(ui.buttons(play));"
    new = """card.addView(ui.buttons(play));
            {
                boolean russianData = org.griefsyndrome.port.PortSettings.hasRussianData(this);
                card.addView(ui.toggle("Играть на русском", russianData ? null : "Русские архивы недоступны",
                    russianData && "1".equals(State.get(this, State.CHOICES, "option.russian", "1")),
                    on -> State.put(this, State.CHOICES, "option.russian", russianData && on ? "1" : "0")));
            }"""
    if text.count(old) != 1:
        raise ValueError("Launcher play-button integration point changed")
    text = text.replace(old, new)
    activity.write_text(text, encoding="utf-8", newline="\n")
    return prepared
