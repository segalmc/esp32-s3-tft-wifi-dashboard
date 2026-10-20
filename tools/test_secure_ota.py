"""Host-side checks for private ESPOTA credentials and upload invocation."""

import contextlib
import io
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest import mock

from secure_espota import load_password, upload
from set_ota_password import store_password


DUMMY_PASSWORD = "ABCDEFGHJKLMNPQRSTUV"  # 20-character test fixture, not a device secret.


class SecureOtaTest(unittest.TestCase):
    def test_platformio_command_omits_auth_argument(self):
        class FakeEnvironment:
            def __init__(self):
                self.values = {}

            def subst(self, key):
                return "espota" if key == "$UPLOAD_PROTOCOL" else ""

            def Replace(self, **values):
                self.values.update(values)

        environment = FakeEnvironment()
        script = Path(__file__).with_name("secure_ota_upload.py").read_text()
        with mock.patch.dict(os.environ, {"NATURE_GLASS_OTA_IP": "192.168.31.231"}):
            exec(script, {"env": environment, "Import": lambda name: None})
        self.assertEqual(environment.values["UPLOAD_PORT"], "192.168.31.231")
        command = environment.values["UPLOADCMD"]
        self.assertEqual(environment.values["UPLOADERFLAGS"], [])
        self.assertIn("secure_espota.py", command)
        self.assertNotIn("auth", command)
        self.assertNotIn("UPLOAD_FLAGS", command)

    def test_private_file_and_replacement(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "nature-glass" / "ota-password"
            store_password(DUMMY_PASSWORD, path)
            self.assertEqual(load_password(path), DUMMY_PASSWORD)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertEqual(stat.S_IMODE(path.parent.stat().st_mode), 0o700)
            replacement = DUMMY_PASSWORD[::-1]
            store_password(replacement, path)
            self.assertEqual(load_password(path), replacement)
            os.chmod(path, 0o644)
            with self.assertRaises(ValueError):
                load_password(path)
            with self.assertRaises(ValueError):
                store_password(DUMMY_PASSWORD, path)

    def test_rejects_symlink_and_bad_content(self):
        with tempfile.TemporaryDirectory() as root:
            directory = Path(root) / "nature-glass"
            directory.mkdir(mode=0o700)
            target = directory / "target"
            target.write_text(DUMMY_PASSWORD)
            os.chmod(target, 0o600)
            link = directory / "ota-password"
            link.symlink_to(target)
            with self.assertRaises(OSError):
                load_password(link)
            with self.assertRaises(ValueError):
                store_password(DUMMY_PASSWORD, link)
            target.write_text("invalid")
            with self.assertRaises(ValueError):
                load_password(target)

    def test_uploader_receives_secret_in_process_only(self):
        with tempfile.TemporaryDirectory() as root:
            fake_espota = Path(root) / "espota.py"
            fake_espota.write_text(
                "FLASH = 0\n"
                "def serve(host, local_ip, port, local_port, password, image, command):\n"
                f"    assert password == {DUMMY_PASSWORD!r}\n"
                "    assert host == '192.168.31.230' and local_ip == '0.0.0.0'\n"
                "    assert port == 3232 and 10000 <= local_port <= 60000\n"
                "    assert image == 'firmware.bin' and command == FLASH\n"
                "    return 0\n"
            )
            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
                self.assertEqual(upload(fake_espota, "192.168.31.230",
                                        "firmware.bin", DUMMY_PASSWORD), 0)
            self.assertNotIn(DUMMY_PASSWORD, output.getvalue())


if __name__ == "__main__":
    unittest.main()
