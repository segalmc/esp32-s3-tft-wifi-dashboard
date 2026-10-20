#!/usr/bin/env python3
"""Compare the firmware's POSIX TZ rule with IANA Asia/Jerusalem at DST edges."""

import datetime as dt
import os
from pathlib import Path
import re
import time
from zoneinfo import ZoneInfo


source = (Path(__file__).resolve().parents[1] / "src/NetworkManager.cpp").read_text()
rule = re.search(r'configTzTime\("([^"]+)"', source)
assert rule, "firmware timezone rule not found"
os.environ["TZ"] = rule.group(1)
time.tzset()

for year, month, day, hour in ((2026, 3, 27, 0), (2026, 10, 24, 23)):
    edge = dt.datetime(year, month, day, hour, tzinfo=dt.timezone.utc)
    for delta in (-dt.timedelta(seconds=1), dt.timedelta(), dt.timedelta(seconds=1)):
        instant = edge + delta
        timestamp = instant.timestamp()
        actual = time.localtime(timestamp)
        expected = instant.astimezone(ZoneInfo("Asia/Jerusalem"))
        assert (actual.tm_year, actual.tm_mon, actual.tm_mday, actual.tm_hour,
                actual.tm_min, actual.tm_sec, actual.tm_isdst) == (
                    expected.year, expected.month, expected.day, expected.hour,
                    expected.minute, expected.second, int(bool(expected.dst()))
                ), (instant, actual, expected)

print("Israel timezone/DST transition tests passed")
