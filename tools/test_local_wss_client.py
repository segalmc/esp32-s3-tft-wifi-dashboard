import io
import json
from pathlib import Path
import socket
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from local_wss_client import mac, read_frame, write_frame, hello, state


class BufferSocket:
    def __init__(self, content=b""):
        self.content = io.BytesIO(content)
        self.sent = b""

    def recv(self, count):
        return self.content.read(count)

    def sendall(self, data):
        self.sent += data


class LocalWssClientTests(unittest.TestCase):
    def test_hmac_matches_shared_contract_vector(self):
        frame = {
            "link": "local", "deviceRole": "esp32",
            "installationId": "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
            "deviceId": "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
            "bootId": "boot-a",
            "deviceSessionId": "cccccccc-cccc-4ccc-8ccc-cccccccccccc",
            "connectionSessionId": "dddddddd-dddd-4ddd-8ddd-dddddddddddd",
            "serverNonce": "0" * 64, "clientNonce": "1" * 64,
            "expiresAt": "2026-10-25T10:00:10Z",
        }
        self.assertEqual(mac(bytes(range(32)).hex(), frame),
                         "9628293889914e8644c0e78fce9ce739cee099385b09a96449212749704ea14e")
        frame["deviceId"] = "cccccccc-cccc-4ccc-8ccc-cccccccccccc"
        self.assertNotEqual(mac(bytes(range(32)).hex(), frame),
                            "9628293889914e8644c0e78fce9ce739cee099385b09a96449212749704ea14e")

    def test_client_frame_is_masked(self):
        sock = BufferSocket()
        write_frame(sock, '{"a":1}')
        self.assertEqual(sock.sent[0], 0x81)
        self.assertTrue(sock.sent[1] & 0x80)
        self.assertEqual(sock.sent[1] & 0x7f, 7)
        mask, body = sock.sent[2:6], sock.sent[6:]
        self.assertEqual(bytes(value ^ mask[i % 4] for i, value in enumerate(body)),
                         b'{"a":1}')

    def test_reject_masked_or_oversized_server_frame(self):
        with self.assertRaises(ValueError):
            read_frame(BufferSocket(b"\x81\x80"))
        with self.assertRaises(ValueError):
            read_frame(BufferSocket(b"\x81\x7e\x08\x01"))

    def test_fresh_session_and_inferred_valve(self):
        identity = {"deviceId": "a", "installationId": "b"}
        one, two = hello(identity), hello(identity)
        self.assertNotEqual(one["deviceSessionId"], two["deviceSessionId"])
        self.assertNotEqual(one["connectionSessionId"], two["connectionSessionId"])
        frame = state(one, 4)
        self.assertEqual(frame["state"]["valve"]["evidence"], "inferred")
        self.assertEqual(frame["state"]["sequence"], frame["sequence"])

    def test_no_control_route_in_server(self):
        source = (Path(__file__).resolve().parents[1] / "src" / "LocalWssServer.cpp").read_text()
        self.assertNotIn("TuyaControl", source)
        self.assertNotIn("request(", source)
        self.assertNotIn("valve-open", source)
        self.assertNotIn("boiler-on", source)


if __name__ == "__main__":
    unittest.main()
