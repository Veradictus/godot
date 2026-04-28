/**************************************************************************/
/*  claude_reload_server.cpp                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "claude_reload_server.h"

#include "core/config/project_settings.h"
#include "core/crypto/crypto_core.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/variant/variant.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"

ClaudeReloadServer *ClaudeReloadServer::singleton = nullptr;

ClaudeReloadServer::ClaudeReloadServer() {
	singleton = this;
	server.instantiate();
}

ClaudeReloadServer::~ClaudeReloadServer() {
	stop();
	singleton = nullptr;
}

String ClaudeReloadServer::_ensure_auth_token() {
	EditorSettings *es = EditorSettings::get_singleton();
	String token = es->get_setting("network/claude_reload/auth_token");
	if (!token.is_empty()) {
		return token;
	}

	uint8_t buf[24];
	CryptoCore::RandomGenerator rng;
	if (rng.init() != OK || rng.get_random_bytes(buf, sizeof(buf)) != OK) {
		// Fall back to a time-seeded pseudo-random token. Good enough for a loopback
		// endpoint that only runs on the developer's own machine.
		uint64_t seed = OS::get_singleton()->get_ticks_usec() ^ (uint64_t)this;
		for (size_t i = 0; i < sizeof(buf); i++) {
			seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
			buf[i] = (uint8_t)(seed >> 56);
		}
	}

	token = String::hex_encode_buffer(buf, sizeof(buf));
	es->set_setting("network/claude_reload/auth_token", token);
	es->save();
	return token;
}

void ClaudeReloadServer::start() {
	if (running) {
		return;
	}

	EditorSettings *es = EditorSettings::get_singleton();
	if (!(bool)es->get_setting("network/claude_reload/enabled")) {
		return;
	}

	expected_token = _ensure_auth_token();
	int port = (int)es->get_setting("network/claude_reload/port");

	const Error err = server->listen((uint16_t)port, IPAddress("127.0.0.1"));
	if (err != OK) {
		ERR_PRINT(vformat("ClaudeReloadServer: failed to bind 127.0.0.1:%d (err %d).", port, err));
		return;
	}

	bound_port = server->get_local_port();

	// Drop a discovery file next to the project so external tooling (Claude
	// PostToolUse hooks, scripts, etc.) can find the live port and token
	// without the user copying them by hand. File lives under `.godot/` which
	// is already gitignored in every Godot project.
	{
		const String project_dir = ProjectSettings::get_singleton()->get_resource_path();
		if (!project_dir.is_empty()) {
			const String dot_dir = project_dir.path_join(".godot");
			Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
			if (da.is_valid() && !da->dir_exists(dot_dir)) {
				da->make_dir_recursive(dot_dir);
			}
			Dictionary info;
			info["port"] = bound_port;
			info["token"] = expected_token;
			const String discovery_path = dot_dir.path_join("claude_reload.json");
			Ref<FileAccess> f = FileAccess::open(discovery_path, FileAccess::WRITE);
			if (f.is_valid()) {
				f->store_string(JSON::stringify(info));
			}
		}
	}

	exit_thread.clear();
	thread.start(
			[](void *p_self) {
				static_cast<ClaudeReloadServer *>(p_self)->_thread_func();
			},
			this);
	running = true;

	if (EditorNode::get_log()) {
		EditorNode::get_log()->add_message(
				vformat("Claude reload bridge listening on 127.0.0.1:%d (token in Editor Settings → Network → Claude Reload).", bound_port),
				EditorLog::MSG_TYPE_EDITOR);
	} else {
		print_line(vformat("ClaudeReloadServer listening on 127.0.0.1:%d", bound_port));
	}
}

void ClaudeReloadServer::stop() {
	if (!running) {
		return;
	}
	exit_thread.set();
	thread.wait_to_finish();
	server->stop();
	running = false;
	bound_port = 0;
}

void ClaudeReloadServer::_thread_func() {
	while (!exit_thread.is_set()) {
		if (server->is_connection_available()) {
			Ref<StreamPeerTCP> peer = server->take_connection();
			if (peer.is_valid()) {
				_handle_connection(peer);
			}
		} else {
			OS::get_singleton()->delay_usec(50 * 1000); // 50 ms idle poll.
		}
	}
}

bool ClaudeReloadServer::_read_request(Ref<StreamPeerTCP> p_peer, String &r_method, String &r_path, String &r_token, String &r_body) {
	// Read the header block (terminated by \r\n\r\n) with a hard budget, so a
	// misbehaving client cannot stall the worker thread.
	const uint64_t deadline_usec = OS::get_singleton()->get_ticks_usec() + 2 * 1000 * 1000; // 2 seconds
	Vector<uint8_t> buf;
	int header_end = -1;
	int content_length = 0;

	while (header_end < 0) {
		if (OS::get_singleton()->get_ticks_usec() > deadline_usec || exit_thread.is_set()) {
			return false;
		}
		p_peer->poll();
		const StreamPeerTCP::Status st = p_peer->get_status();
		if (st == StreamPeerTCP::STATUS_NONE || st == StreamPeerTCP::STATUS_ERROR) {
			return false;
		}
		int avail = p_peer->get_available_bytes();
		if (avail <= 0) {
			OS::get_singleton()->delay_usec(5 * 1000);
			continue;
		}
		const int prev_size = buf.size();
		buf.resize(prev_size + avail);
		int got = 0;
		p_peer->get_partial_data(buf.ptrw() + prev_size, avail, got);
		buf.resize(prev_size + got);

		// Scan for the end-of-headers delimiter.
		for (int i = MAX(0, prev_size - 3); i + 3 < buf.size(); i++) {
			if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
				header_end = i + 4;
				break;
			}
		}
		if (buf.size() > 16384) {
			return false; // Header block too large.
		}
	}

	String headers;
	headers.append_utf8((const char *)buf.ptr(), header_end);
	Vector<String> lines = headers.split("\r\n", false);
	if (lines.is_empty()) {
		return false;
	}

	Vector<String> request_line = lines[0].split(" ", false);
	if (request_line.size() < 2) {
		return false;
	}
	r_method = request_line[0];
	r_path = request_line[1];

	for (int i = 1; i < lines.size(); i++) {
		const String &line = lines[i];
		int colon = line.find(":");
		if (colon <= 0) {
			continue;
		}
		const String name = line.substr(0, colon).strip_edges().to_lower();
		const String value = line.substr(colon + 1).strip_edges();
		if (name == "content-length") {
			content_length = value.to_int();
		} else if (name == "x-claude-token") {
			r_token = value;
		}
	}

	if (content_length < 0 || content_length > 1 * 1024 * 1024) {
		return false; // Absurd body size.
	}

	// Read the body.
	int body_have = buf.size() - header_end;
	while (body_have < content_length) {
		if (OS::get_singleton()->get_ticks_usec() > deadline_usec || exit_thread.is_set()) {
			return false;
		}
		p_peer->poll();
		const StreamPeerTCP::Status st = p_peer->get_status();
		if (st == StreamPeerTCP::STATUS_NONE || st == StreamPeerTCP::STATUS_ERROR) {
			return false;
		}
		int avail = p_peer->get_available_bytes();
		if (avail <= 0) {
			OS::get_singleton()->delay_usec(5 * 1000);
			continue;
		}
		const int prev_size = buf.size();
		buf.resize(prev_size + avail);
		int got = 0;
		p_peer->get_partial_data(buf.ptrw() + prev_size, avail, got);
		buf.resize(prev_size + got);
		body_have = buf.size() - header_end;
	}

	r_body = String();
	if (content_length > 0) {
		r_body.append_utf8((const char *)(buf.ptr() + header_end), content_length);
	}
	return true;
}

void ClaudeReloadServer::_send_response(Ref<StreamPeerTCP> p_peer, int p_status, const String &p_body) {
	const char *reason = p_status == 200 ? "OK"
			: p_status == 400            ? "Bad Request"
			: p_status == 401            ? "Unauthorized"
			: p_status == 404            ? "Not Found"
										 : "Error";
	CharString body_utf8 = p_body.utf8();
	String headers = vformat(
			"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
			p_status, reason, body_utf8.length());
	CharString headers_utf8 = headers.utf8();
	p_peer->put_data((const uint8_t *)headers_utf8.ptr(), headers_utf8.length());
	if (body_utf8.length() > 0) {
		p_peer->put_data((const uint8_t *)body_utf8.ptr(), body_utf8.length());
	}
}

void ClaudeReloadServer::_handle_connection(Ref<StreamPeerTCP> p_peer) {
	// Wait for the peer to report CONNECTED before reading. take_connection can
	// hand back a peer still mid-handshake.
	const uint64_t deadline_usec = OS::get_singleton()->get_ticks_usec() + 1 * 1000 * 1000;
	while (p_peer->get_status() == StreamPeerTCP::STATUS_CONNECTING) {
		if (OS::get_singleton()->get_ticks_usec() > deadline_usec || exit_thread.is_set()) {
			return;
		}
		p_peer->poll();
		OS::get_singleton()->delay_usec(2 * 1000);
	}
	if (p_peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		return;
	}

	String method, path, token, body;
	if (!_read_request(p_peer, method, path, token, body)) {
		_send_response(p_peer, 400, "{\"error\":\"bad_request\"}");
		p_peer->disconnect_from_host();
		return;
	}

	if (expected_token.is_empty() || token != expected_token) {
		_send_response(p_peer, 401, "{\"error\":\"unauthorized\"}");
		p_peer->disconnect_from_host();
		return;
	}

	if (method != "POST" || !(path == "/reload" || path == "/rescan")) {
		_send_response(p_peer, 404, "{\"error\":\"not_found\"}");
		p_peer->disconnect_from_host();
		return;
	}

	PackedStringArray paths;
	if (!body.is_empty()) {
		const Variant parsed = JSON::parse_string(body);
		if (parsed.get_type() == Variant::DICTIONARY) {
			Dictionary d = parsed;
			if (d.has("paths")) {
				const Variant v = d["paths"];
				if (v.get_type() == Variant::ARRAY) {
					Array arr = v;
					for (int i = 0; i < arr.size(); i++) {
						paths.push_back(String(arr[i]));
					}
				}
			}
		}
	}

	// Dispatch the actual editor mutation to the main thread.
	callable_mp(this, &ClaudeReloadServer::_perform_reload).call_deferred(paths);

	_send_response(p_peer, 200, "{\"ok\":true}");
	p_peer->disconnect_from_host();
}

void ClaudeReloadServer::_perform_reload(const PackedStringArray &p_paths) {
	// Step 1 — full rescan. Picks up modifications, additions, and removals.
	// Same call used by the editor's focus-in path, so import/reimport also runs.
	EditorFileSystem *efs = EditorFileSystem::get_singleton();
	if (efs) {
		efs->scan_changes();
	}

	// Step 2 — for script-like resources, push a live reload into the running
	// game (if any) and refresh editor tabs.
	ScriptEditor *se = ScriptEditor::get_singleton();
	if (se) {
		bool any_script = false;
		for (int i = 0; i < p_paths.size(); i++) {
			const String &path = p_paths[i];
			const String ext = path.get_extension().to_lower();
			if (ext == "gd" || ext == "gdshader" || ext == "gdshaderinc" || ext == "cs") {
				se->trigger_live_script_reload(path);
				any_script = true;
			}
		}
		// reload_scripts(false) only rewrites tabs whose mtime actually changed,
		// so this is safe to call unconditionally when anything script-shaped
		// was touched.
		if (any_script || p_paths.is_empty()) {
			se->reload_scripts(false);
		}
	}
}
