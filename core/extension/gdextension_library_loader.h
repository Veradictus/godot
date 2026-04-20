/**************************************************************************/
/*  gdextension_library_loader.h                                          */
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

#include "core/extension/gdextension_loader.h"
#include "core/io/config_file.h"
#include "core/os/shared_object.h"

#include <functional>

class GDExtensionLibraryLoader : public GDExtensionLoader {
	GDSOFTCLASS(GDExtensionLibraryLoader, GDExtensionLoader);

	friend class GDExtensionManager;
	friend class GDExtension;

private:
	String resource_path;

	void *library = nullptr; // pointer if valid.
	String library_path;
	String entry_symbol;

#ifdef TOOLS_ENABLED
	bool is_reloadable = false;
#endif

	Vector<SharedObject> library_dependencies;

	HashMap<String, String> class_icon_paths;

#ifdef TOOLS_ENABLED
	// Fingerprint of the config + library files captured at load/reload time. `has_library_changed()`
	// uses size as a cheap primary check, mtime as a secondary hint, and finally an MD5 tiebreak when
	// size matches but mtime diverges (e.g., "touch" or Windows-style file-copy that rewrites the
	// timestamp without actually changing bytes). These fields are written on the main thread during
	// parse/reload and only read by the async probe thread after the main thread has published them.
	mutable uint64_t resource_last_modified_time = 0;
	mutable uint64_t library_last_modified_time = 0;
	mutable int64_t resource_size = -1;
	mutable int64_t library_size = -1;
	mutable String library_hash;

	void update_library_fingerprint(uint64_t p_resource_mtime, uint64_t p_library_mtime,
			int64_t p_resource_size, int64_t p_library_size, const String &p_library_hash) {
		resource_last_modified_time = p_resource_mtime;
		library_last_modified_time = p_library_mtime;
		resource_size = p_resource_size;
		library_size = p_library_size;
		library_hash = p_library_hash;
	}
#endif

public:
	static String find_extension_library(const String &p_path, Ref<ConfigFile> p_config, std::function<bool(String)> p_has_feature, PackedStringArray *r_tags = nullptr);
	static Vector<SharedObject> find_extension_dependencies(const String &p_path, Ref<ConfigFile> p_config, std::function<bool(String)> p_has_feature);

	virtual Error open_library(const String &p_path) override;
	virtual Error initialize(GDExtensionInterfaceGetProcAddress p_get_proc_address, const Ref<GDExtension> &p_extension, GDExtensionInitialization *r_initialization) override;
	virtual void close_library() override;
	virtual bool is_library_open() const override;
	virtual bool has_library_changed() const override;
	virtual bool library_exists() const override;

	Error parse_gdextension_file(const String &p_path);
};
