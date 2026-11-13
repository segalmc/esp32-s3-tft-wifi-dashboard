#!/usr/bin/env python3
"""Guard concurrent snapshot/time ordering at actual API/heating call sites."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
for path, snapshot, end, clock in [
    ("src/BoilerWeb.cpp", "const auto link_state = telemetry_->snapshot();",
     "String body;", "link_now"),
    ("src/main.cpp", "const auto heating_telemetry = local_http.snapshot();",
     "display_manager.updateTelemetry", "heating_now_ms"),
]:
    source = (root / path).read_text()
    block = source.split(snapshot, 1)[1].split(end, 1)[0]
    declaration = f"const uint32_t {clock} = millis();"
    assert declaration in block, f"{path}: clock must follow telemetry snapshot"
    before, after = block.split(declaration, 1)
    assert not re.search(r"\b(?:now|now_ms)\s*-", block), path
    assert not re.search(r"\b" + clock + r"\s*-", before), path
    assert re.search(r"\b" + clock + r"\s*-", after), path

# The observed race: a task updates its timestamp during status preparation.
old_now, updated_timestamp, after_snapshot = 1000, 1003, 1004
assert ((old_now - updated_timestamp) & 0xffffffff) > 35000
assert ((after_snapshot - updated_timestamp) & 0xffffffff) == 1
# Sampling after the snapshot also preserves legitimate millis() wrap.
assert ((3 - 0xfffffffe) & 0xffffffff) == 5
print("PASS telemetry snapshot/time ordering: concurrent update and millis wrap")
