/**************************************************************************/
/*  gdextension_manager.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/extension/gdextension.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/native_ptr.h"

GDVIRTUAL_NATIVE_PTR(GDExtensionInitializationFunction)

class GDExtensionManager : public Object {
	GDCLASS(GDExtensionManager, Object);

	int32_t level = -1;
	HashMap<String, Ref<GDExtension>> gdextension_map;
	HashMap<String, String> gdextension_class_icon_paths;

	// Paths of `.gdextension` files that parsed successfully but don't ship a library for the current
	// platform. The stored value is the resource file's modified time at the moment we skipped it, so
	// `ensure_extensions_loaded()` can retry once the developer edits the config (e.g., adds a Windows
	// entry) without spamming the log on every focus-in scan.
	HashMap<String, uint64_t> unsupported_extensions;

	// Serializes the async change-detection probe kicked off by `reload_extensions()`. We skip probes
	// while one is already in flight so rapid alt-tabbing doesn't stack worker tasks.
	SafeFlag reload_probe_in_progress;
	Vector<Ref<GDExtension>> reload_probe_snapshot;
	Vector<String> reload_probe_changed_paths;

	bool startup_callback_called = false;
	bool shutdown_callback_called = false;

	static void _bind_methods();

	static inline GDExtensionManager *singleton = nullptr;

public:
	enum LoadStatus {
		LOAD_STATUS_OK,
		LOAD_STATUS_FAILED,
		LOAD_STATUS_ALREADY_LOADED,
		LOAD_STATUS_NOT_LOADED,
		LOAD_STATUS_NEEDS_RESTART,
		LOAD_STATUS_UNSUPPORTED_PLATFORM,
	};

private:
	LoadStatus _load_extension_internal(const Ref<GDExtension> &p_extension, bool p_first_load);
	void _finish_load_extension(const Ref<GDExtension> &p_extension);
	LoadStatus _unload_extension_internal(const Ref<GDExtension> &p_extension);

#ifdef TOOLS_ENABLED
	static void _reload_all_scripts();

	// Runs on a worker thread: calls `has_library_changed()` on every snapshotted extension.
	// Pure reads of per-loader fields that are only written during parse/reload on the main thread,
	// and `reload_probe_in_progress` guards against overlapping reloads triggered from here.
	void _probe_extensions_thread();
	// Runs on the main thread after the probe finishes (via `call_deferred`): reloads the subset
	// of extensions whose library bytes actually changed, then emits signals.
	void _finish_extension_reload();
#endif

public:
	LoadStatus load_extension(const String &p_path);
	LoadStatus load_extension_from_function(const String &p_path, GDExtensionPtr<const GDExtensionInitializationFunction> p_init_func);
	LoadStatus load_extension_with_loader(const String &p_path, const Ref<GDExtensionLoader> &p_loader);
	LoadStatus reload_extension(const String &p_path);
	LoadStatus unload_extension(const String &p_path);
	bool is_extension_loaded(const String &p_path) const;
	Vector<String> get_loaded_extensions() const;
	Ref<GDExtension> get_extension(const String &p_path);

	bool class_has_icon_path(const String &p_class) const;
	String class_get_icon_path(const String &p_class) const;

	void initialize_extensions(GDExtension::InitializationLevel p_level);
	void deinitialize_extensions(GDExtension::InitializationLevel p_level);

#ifdef TOOLS_ENABLED
	void track_instance_binding(void *p_token, Object *p_object);
	void untrack_instance_binding(void *p_token, Object *p_object);
#endif

	static GDExtensionManager *get_singleton();

	void load_extensions();
	void reload_extensions();
	bool ensure_extensions_loaded(const HashSet<String> &p_extensions);

	void startup();
	void shutdown();
	void frame();

	GDExtensionManager();
	~GDExtensionManager();
};

VARIANT_ENUM_CAST(GDExtensionManager::LoadStatus)
