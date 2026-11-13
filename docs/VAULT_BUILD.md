# Vault-enabled build tools

This checkout keeps firmware/application behavior unchanged. The new build wrappers load exact private compatibility files from the central age vault through the sibling hot-water-tools project, then remove files they created. Existing private files are never overwritten. Run tools/vault_build.py (original firmware: poc/original-esp32/firmware/tools/vault_build.py). Set HOT_WATER_AGE_IDENTITY to your existing identity path; unattended use additionally requires --allow-noninteractive. The tools root may be overridden with HOT_WATER_TOOLS_ROOT.

No upload, installation, relay or valve operation is performed by the default wrapper commands. The S3 OTA target is a configurable hostname placeholder; identify the physical target independently before any later authorized upload. Existing secure uploader configuration remains a fallback. Canonical S3 uses NATURE_GLASS_OTA_PASSWORD_FILE for a private path override; the original uses ORIGINAL_ESP32_OTA_PASSWORD_FILE. Password values never belong in arguments.

The verified release tag is unchanged. Later S3 reconciliation code is preserved and simulation-tested on a separate branch/worktree, not deployed. Full migration and recovery documents are in the Projects root and encrypted recovery archive.
