#pragma once
// Copy to ignored include/LocalWssConfig.h on an isolated development board.
// Use a locally resolvable DNS name in the certificate's subjectAltName.
// Do not commit the filled copy, certificate, key, or HMAC secret.
#define LOCAL_WSS_HOSTNAME "s3.example.invalid"
#define LOCAL_WSS_PORT 9443
#define LOCAL_WSS_DEVICE_ID "00000000-0000-4000-8000-000000000001"
#define LOCAL_WSS_INSTALLATION_ID "00000000-0000-4000-8000-000000000002"
// 32 random bytes in lowercase hex, unique to this device and installation.
#define LOCAL_WSS_SECRET_HEX ""
// Optional second key for a manually staged rotation.
#define LOCAL_WSS_SECOND_SECRET_HEX ""
#define LOCAL_WSS_CERT_PEM R"PEM()PEM"
#define LOCAL_WSS_KEY_PEM R"PEM()PEM"
#define LOCAL_WSS_CA_PEM R"PEM()PEM"
