# Approved hot-water release checkpoint

Release label: `stable-2026-11-13-verified`. Role: `verified-deployed-s3`.
Parent baseline: `5c9ed85cda21e0ec3242df3c931dcaf81ae021cd`.

Snapshot matches the tested S3 source dependency fingerprint. TuyaControl.cpp SHA-256 f3ec13256c5adc6058a526a260c157146bccfef3e7cae0e98dee97b4ec025700. Firmware SHA-256 97cc917fcc97301193196619608028d4fee030f0168704374e274d44137e6270. Later reconciliation edits are on checkpoint/stable-2026-11-13-s3-postverification and are not hardware-verified.

Source, tests, scripts and sanitized evidence are preserved. Private credentials, provisioning headers, signing keys and generated artifacts are excluded; restore them separately through secure local files. Existing historical credential exposure is not removed by this snapshot. Do not restore legacy proxy operation as a competing Tuya controller. No application logic edits or device operations were performed to create this checkpoint.

Existing stable-2026-11-13 tags are intentionally retained unchanged. The physical valve motor was not tested by the original-controller OTA verification.
