# Approved hot-water release checkpoint

Release label: `stable-2026-11-13-verified`. Role: `http-tooling-reference`.
Parent baseline: `5c9ed85cda21e0ec3242df3c931dcaf81ae021cd`.

Archival source/tooling reference only; this annotation does not assert new build, runtime or physical hardware validation.

Source, tests, scripts and sanitized evidence are preserved. Private credentials, provisioning headers, signing keys and generated artifacts are excluded; restore them separately through secure local files. Existing historical credential exposure is not removed by this snapshot. Do not restore legacy proxy operation as a competing Tuya controller. No application logic edits or device operations were performed to create this checkpoint.

Existing stable-2026-11-13 tags are intentionally retained unchanged. The physical valve motor was not tested by the original-controller OTA verification.
