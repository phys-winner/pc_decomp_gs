"""Check external launcher lookup without requiring Android tools."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


class LauncherPathTest(unittest.TestCase):
    def test_search_order_and_missing_source(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / "game"
            (root / "tools").mkdir(parents=True)
            script = root / "tools/grief_launcher.py"
            shutil.copyfile(Path(__file__).with_name("grief_launcher.py"), script)
            env = dict(os.environ)
            env.pop("PORT_LAUNCHER", None)

            def run():
                return subprocess.run(
                    [sys.executable, "-c", "import runpy,sys; m=runpy.run_path(sys.argv[1]); print(m['LAUNCHER'].resolve())", str(script)],
                    env=env, capture_output=True, text=True)

            result = run()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Launcher base is missing", result.stderr)
            for candidate in (base / "launcher", root / "android/launcher", base / "override"):
                (candidate / "tools").mkdir(parents=True)
                (candidate / "tools/launcher.py").write_text("", encoding="utf-8")
                if candidate.name == "override":
                    env["PORT_LAUNCHER"] = str(candidate)
                result = run()
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(Path(result.stdout.strip()), candidate.resolve())
            env["PORT_LAUNCHER"] = str(base / "absent")
            result = run()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(Path(result.stdout.strip()), (root / "android/launcher").resolve())


if __name__ == "__main__":
    unittest.main()
