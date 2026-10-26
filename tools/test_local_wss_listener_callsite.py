"""Guard the listener call site against reversed server-presence logic."""

from pathlib import Path
import re

source = (Path(__file__).resolve().parents[1] / "src/LocalWssServer.cpp").read_text()
calls = re.findall(r"local_wss_clock_policy::start_listener\(([^)]*)\)", source)
assert len(calls) == 1, calls
assert re.fullmatch(r"\s*server_\s*!=\s*nullptr\s*,\s*ready\s*", calls[0]), calls
print("S3 listener call-site regression passed")
