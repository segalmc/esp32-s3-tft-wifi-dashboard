"""Check the server installs its retrying TLS transport before WS frame reads."""

from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src/LocalWssServer.cpp").read_text()
header = (root / "include/LocalWssServer.h").read_text()
frame_start = source.index("esp_err_t LocalWssServer::onFrame")
frame_end = source.index("void LocalWssServer::onHello", frame_start)
frame = source[frame_start:frame_end]
install = frame.index("httpd_sess_set_recv_override(server_, fd, tlsReceive)")
read_slice = frame.index("setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO")
receive = frame.index("httpd_ws_recv_frame(req, &frame, kMaxFrame)")
assert read_slice < install < receive
assert "kSocketReadSliceMs" in frame
assert "kReadBudgetMs" in source
assert "config.user_cb = tlsSession" in source
assert "config.httpd.global_user_ctx = this" in source
assert re.search(r"start_listener\(server_\s*!=\s*nullptr\s*,\s*ready\)", source)
print("S3 TLS receive call-site regression passed")
