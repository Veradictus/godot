#ifndef PATCH_SIGNING_KEY_H
#define PATCH_SIGNING_KEY_H

// Public key used to verify content patches before mounting them (see
// ProjectSettings::mount_runtime_patches). Injected at build time from the
// PATCH_SIGNING_PUBLIC_KEY environment variable (a base64-encoded PEM) into
// patch_signing_key.gen.cpp. Empty when the engine is built without that
// variable, in which case patch mounting fails closed. The matching private key
// (PATCH_SIGNING_PRIVATE_KEY) lives only on the build machine.
extern const char *patch_signing_public_key_pem;

#endif // PATCH_SIGNING_KEY_H
