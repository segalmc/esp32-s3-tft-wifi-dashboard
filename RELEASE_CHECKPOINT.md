# Approved hot-water release checkpoint

Release label: `stable-2026-11-13-verified`. Role: `verified-deployed-s3`.
Parent baseline: `5c9ed85cda21e0ec3242df3c931dcaf81ae021cd`.

Contains later local S3 reconciliation edits, not deployed or hardware-verified. TuyaControl.cpp SHA-256 e4a713d5a9a5e1ed22e8edf6f07f1980488a9144b53694812812cd7a81cc8bed. Use the annotated release tag for the tested S3 snapshot.

Source, tests, scripts and sanitized evidence are preserved. Private credentials, provisioning headers, signing keys and generated artifacts are excluded; restore them separately through secure local files. Existing historical credential exposure is not removed by this snapshot. Do not restore legacy proxy operation as a competing Tuya controller. No application logic edits or device operations were performed to create this checkpoint.

Existing stable-2026-11-13 tags are intentionally retained unchanged. The physical valve motor was not tested by the original-controller OTA verification.
