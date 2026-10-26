"""Guard the real HTTPD orphan cleanup path used after WebSocket upgrade."""

from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "src/LocalWssServer.cpp").read_text()
manager = source[source.index("void LocalWssServer::run()"):
                 source.index("esp_err_t LocalWssServer::wsEntry")]
assert "httpd_get_client_list(server_, &count, fds)" in manager
assert "local_wss_session_policy::expired_unclaimed(" in manager
assert "httpd_sess_trigger_close(server_, expired)" in manager
assert "kAuthTimeoutMs" in manager
print("S3 unclaimed HTTPD session call-site regression passed")
