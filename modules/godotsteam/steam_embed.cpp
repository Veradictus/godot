#if defined(STEAM_EMBEDDED_DLL) && defined(_WIN32)

#include "steam_embed.h"

#include <windows.h>
#include <cstddef>
#include <cstdio>

// Defined in the generated embedded_steam_dll.gen.cpp
extern const unsigned char embedded_steam_dll[];
extern const size_t embedded_steam_dll_size;

#ifdef _WIN64
static const wchar_t *STEAM_DLL_FILENAME = L"steam_api64.dll";
#else
static const wchar_t *STEAM_DLL_FILENAME = L"steam_api.dll";
#endif

static bool dll_loaded = false;

bool steam_embed_load_dll() {
	if (dll_loaded) {
		return true;
	}

	// Check if the DLL already exists next to the executable.
	wchar_t exe_path[MAX_PATH];
	GetModuleFileNameW(NULL, exe_path, MAX_PATH);
	wchar_t *last_sep = wcsrchr(exe_path, L'\\');
	if (last_sep) {
		*(last_sep + 1) = L'\0';
	}

	wchar_t local_dll[MAX_PATH];
	wcscpy_s(local_dll, MAX_PATH, exe_path);
	wcscat_s(local_dll, MAX_PATH, STEAM_DLL_FILENAME);

	if (GetFileAttributesW(local_dll) != INVALID_FILE_ATTRIBUTES) {
		// Already present next to the exe - the delay-load resolver
		// will find it via normal search, no extraction needed.
		dll_loaded = true;
		return true;
	}

	// Build extraction path: %TEMP%\godot_steam\<dll_name>
	wchar_t temp_dir[MAX_PATH];
	GetTempPathW(MAX_PATH, temp_dir);
	wcscat_s(temp_dir, MAX_PATH, L"godot_steam\\");
	CreateDirectoryW(temp_dir, NULL);

	wchar_t dll_path[MAX_PATH];
	wcscpy_s(dll_path, MAX_PATH, temp_dir);
	wcscat_s(dll_path, MAX_PATH, STEAM_DLL_FILENAME);

	// If a file already exists, check if it matches the expected size.
	// Re-extract on mismatch (SDK update).
	bool needs_extract = true;
	WIN32_FILE_ATTRIBUTE_DATA file_info;
	if (GetFileAttributesExW(dll_path, GetFileExInfoStandard, &file_info)) {
		if (file_info.nFileSizeLow == (DWORD)embedded_steam_dll_size && file_info.nFileSizeHigh == 0) {
			needs_extract = false;
		}
	}

	if (needs_extract) {
		HANDLE hFile = CreateFileW(dll_path, GENERIC_WRITE, 0, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hFile == INVALID_HANDLE_VALUE) {
			return false;
		}

		DWORD written = 0;
		BOOL ok = WriteFile(hFile, embedded_steam_dll,
				(DWORD)embedded_steam_dll_size, &written, NULL);
		CloseHandle(hFile);

		if (!ok || written != (DWORD)embedded_steam_dll_size) {
			DeleteFileW(dll_path);
			return false;
		}
	}

	// Preload the DLL from the temp path. Once loaded, the delay-load
	// resolver's LoadLibrary("steam_api64.dll") call will find the
	// already-loaded module by name and reuse it.
	HMODULE hMod = LoadLibraryW(dll_path);
	if (!hMod) {
		return false;
	}

	dll_loaded = true;
	return true;
}

#endif // STEAM_EMBEDDED_DLL && _WIN32
