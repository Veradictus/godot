//================================================================================================//
// GodotSteam - register_types.cpp
//================================================================================================//
//
// Copyright (c) 2015-Current | GP Garcia, Chris Ridenour, and Contributors
//
// View all contributors at https://godotsteam.com/contribute/contributors/
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//================================================================================================//

#include "register_types.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#include "godotsteam.h"
#include "godotsteam_multiplayer_peer.h"
#include "godotsteam_project_settings.h"

#if defined(STEAM_EMBEDDED_DLL) && defined(_WIN32)
#include "steam_embed.h"
#endif

#ifdef TOOLS_ENABLED
#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"

class EditorExportGodotSteam : public EditorExportPlugin {
	GDCLASS(EditorExportGodotSteam, EditorExportPlugin);

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override {
		String sdk_path = String(GODOTSTEAM_SDK_PATH);
		String macos_target = "Contents/MacOS";

		if (p_features.has("macos")) {
			String lib_path = sdk_path.path_join("redistributable_bin/osx/libsteam_api.dylib");
			Vector<String> tags;
			tags.push_back("macos");
			// Must be in Contents/MacOS/ because the dylib install name is @loader_path/libsteam_api.dylib.
			add_shared_object(lib_path, tags, macos_target);
		} else if (p_features.has("windows")) {
			String lib_path = sdk_path.path_join(p_features.has("x86_64") ? "redistributable_bin/win64/steam_api64.dll" : "redistributable_bin/steam_api.dll");
			Vector<String> tags;
			tags.push_back("windows");
			add_shared_object(lib_path, tags);
		} else if (p_features.has("linux")) {
			String subdir = p_features.has("arm64") ? "linuxarm64" : (p_features.has("x86_64") ? "linux64" : "linux32");
			String lib_path = sdk_path.path_join("redistributable_bin/" + subdir + "/libsteam_api.so");
			Vector<String> tags;
			tags.push_back("linux");
			add_shared_object(lib_path, tags);
		}

		// Bundle steam_appid.txt from the project root next to the executable (desktop only).
		if (!p_features.has("android") && !p_features.has("ios") && !p_features.has("web") && FileAccess::exists("res://steam_appid.txt")) {
			String appid_path = ProjectSettings::get_singleton()->globalize_path("res://steam_appid.txt");
			Vector<String> tags;
			add_shared_object(appid_path, tags, p_features.has("macos") ? macos_target : String());
		}
	}

public:
	virtual String get_name() const override { return "GodotSteam"; }
};

static void _godotsteam_editor_init() {
	Ref<EditorExportGodotSteam> steam_export;
	steam_export.instantiate();
	EditorExport::get_singleton()->add_export_plugin(steam_export);
}
#endif // TOOLS_ENABLED

static Steam *SteamPtr = nullptr;


void initialize_godotsteam_module(ModuleInitializationLevel level) {
	if (level == MODULE_INITIALIZATION_LEVEL_CORE) {
#if defined(STEAM_EMBEDDED_DLL) && defined(_WIN32)
		steam_embed_load_dll();
#endif
		GDREGISTER_CLASS(Steam);
		SteamPtr = memnew(Steam);
		Engine::get_singleton()->add_singleton(Engine::Singleton("Steam", Steam::get_singleton()));
 
		// Setup Project Settings
		SteamProjectSettings::register_settings();

		if (Engine::get_singleton()->is_editor_hint()) {
			return;
		}

		if (!SteamProjectSettings::get_auto_init()) {
			return;
		}

		Steam::get_singleton()->run_internal_initialization();
	}
	if (level == MODULE_INITIALIZATION_LEVEL_SERVERS) {
		ClassDB::register_class<SteamPacketPeer>();
		ClassDB::register_class<SteamMultiplayerPeer>();
#ifdef TOOLS_ENABLED
		EditorNode::add_init_callback(_godotsteam_editor_init);
#endif
	}
	if (level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		if (SteamProjectSettings::get_auto_init() && SteamProjectSettings::get_embed_callbacks()) {
			WARN_PRINT_ONCE("[STEAM] Cannot use auto-initialization and embed callbacks together currently. Embed callbacks ignored; call run_callbacks() manually.");
			// This just warns until we can fix the inability to link to SceneTree this early.
			// Steam::get_singleton()->set_internal_callbacks();
		}
	}
}


void uninitialize_godotsteam_module(ModuleInitializationLevel level) {
	if (level == MODULE_INITIALIZATION_LEVEL_CORE) {
		Engine::get_singleton()->remove_singleton("Steam");
		memdelete(SteamPtr);
	}
}
