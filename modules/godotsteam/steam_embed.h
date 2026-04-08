#ifndef STEAM_EMBED_H
#define STEAM_EMBED_H

#if defined(STEAM_EMBEDDED_DLL) && defined(_WIN32)

// Extracts the embedded Steam API DLL to a temporary directory
// and preloads it so the delay-load resolver finds it.
// Must be called before any Steam API function is used.
bool steam_embed_load_dll();

#endif // STEAM_EMBEDDED_DLL && _WIN32

#endif // STEAM_EMBED_H
