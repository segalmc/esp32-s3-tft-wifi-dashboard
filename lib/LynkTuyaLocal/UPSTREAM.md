# Vendored LynkTuyaLocal

Source: https://github.com/F1chter/LynkTuyaLocal

Pinned commit: `43d18dae0499e11a5b4d664ead58f8ffc4187ad7` (2026-10-17).
The upstream license is in `LICENSE`. `src/LynkTuya.h` is locally patched for
authenticated Tuya 3.4 receive frames and nonce challenge, strict frame and
padding validation, bounded connection/response waits, checked handshake writes,
and the ESP32 Arduino mbedTLS API. Protocol logging is compile-time disabled.
The 3.5 path is retained from upstream but is outside this POC's target.
