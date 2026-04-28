/**************************************************************************/
/*  claude_reload_server.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#pragma once

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "core/templates/vector.h"

class ClaudeReloadServer : public Object {
	GDCLASS(ClaudeReloadServer, Object);

	Ref<TCPServer> server;
	Thread thread;
	SafeFlag exit_thread;
	bool running = false;

	int bound_port = 0;
	String expected_token;

	static ClaudeReloadServer *singleton;

	void _thread_func();
	void _handle_connection(Ref<StreamPeerTCP> p_peer);
	bool _read_request(Ref<StreamPeerTCP> p_peer, String &r_method, String &r_path, String &r_token, String &r_body);
	void _send_response(Ref<StreamPeerTCP> p_peer, int p_status, const String &p_body);

	// Main-thread action dispatched from the worker via call_deferred.
	void _perform_reload(const PackedStringArray &p_paths);

	// Ensures a non-empty auth token exists in EditorSettings; generates one if missing.
	String _ensure_auth_token();

public:
	static ClaudeReloadServer *get_singleton() { return singleton; }

	void start();
	void stop();
	bool is_running() const { return running; }
	int get_bound_port() const { return bound_port; }

	ClaudeReloadServer();
	~ClaudeReloadServer();
};
