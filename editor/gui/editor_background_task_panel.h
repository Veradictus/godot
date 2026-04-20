/**************************************************************************/
/*  editor_background_task_panel.h                                        */
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

#include "core/templates/hash_map.h"
#include "scene/gui/box_container.h"

class Button;
class Label;
class PopupPanel;
class ProgressBar;
class VBoxContainer;

// Inline, dual-mode background-task indicator hosted in the editor's bottom-panel HBox.
//
// Minimized mode: a compact "pill" button in the footer (ProgressBar + task label). Hidden when
// no tasks are active.
// Expanded mode: a PopupPanel anchored above the pill showing a row per active task.
//
// Thread safety: all mutators (`add_task`, `task_step`, `end_task`) are safe to call from worker
// threads. They record intent under a mutex and flush to the main thread via `call_deferred`, so
// the actual scene-tree mutation always happens on the main thread.
//
// Replaces the legacy orphaned `BackgroundProgress` widget that `EditorProgressBG` used to feed
// — that widget was instantiated but never added to the scene tree, so scan progress was
// invisible. `EditorNode::progress_*_task_bg` now routes through this class instead.
class EditorBackgroundTaskPanel : public HBoxContainer {
	GDCLASS(EditorBackgroundTaskPanel, HBoxContainer);
	_THREAD_SAFE_CLASS_

	struct Task {
		String label;
		int steps = 1;
		int current = 0;
		// `queued` tasks are placeholders visible only in the popup — no progress bar, dim
		// styling, and ignored by the pill. Callers (e.g. the export dialog's queue) convert
		// a queued task to a running one by calling `promote_queued_task()` when the work
		// actually starts.
		bool queued = false;
		// Lazily populated when the popup opens; torn down when it hides.
		HBoxContainer *row = nullptr;
		Label *row_label = nullptr;
		ProgressBar *row_progress = nullptr;
	};

	// Authoritative task state. Main-thread owned after the deferred hop, but still guarded by
	// `_THREAD_SAFE_METHOD_` so the worker-thread `task_step` can coalesce updates safely.
	HashMap<String, Task> tasks;
	// Worker-produced step batches waiting to be applied. Drained by a single deferred call per
	// idle->busy transition (same deadlock-avoidance pattern as the old `BackgroundProgress`).
	HashMap<String, int> pending_updates;

	// Minimized pill — this HBox's own children.
	ProgressBar *pill_progress = nullptr;
	Label *pill_label = nullptr;

	// Expanded popup.
	PopupPanel *popup = nullptr;
	VBoxContainer *popup_vbox = nullptr;

	void _deferred_add_task(const String &p_task, const String &p_label, int p_steps, bool p_queued);
	void _deferred_promote_queued_task(const String &p_task, int p_steps);
	void _deferred_task_step(const String &p_task, int p_step);
	void _deferred_end_task(const String &p_task);
	void _drain_pending_updates();

	// Style constants for the popup rows.
	Color _get_queued_modulate() const;

	void _refresh_pill();
	void _rebuild_popup_rows();
	void _toggle_popup();
	void _on_popup_hidden();

protected:
	void _notification(int p_what);
	virtual void gui_input(const Ref<InputEvent> &p_event) override;

public:
	// Thread-safe public API. Mirrors the legacy `BackgroundProgress` surface so existing callers
	// (`EditorProgressBG`, `EditorNode::progress_*_task_bg`) keep working unchanged.
	void add_task(const String &p_task, const String &p_label, int p_steps);
	void task_step(const String &p_task, int p_step = -1);
	void end_task(const String &p_task);

	// Queue-awareness API. Use `add_queued_task()` to record planned work that hasn't started
	// yet (e.g., export presets sitting behind a busy platform); the row appears dimmed in the
	// popup and is ignored by the pill. When the work actually starts, call
	// `promote_queued_task()` to give it a step count and flip it to "running". Both are
	// thread-safe and commute via `call_deferred`.
	void add_queued_task(const String &p_task, const String &p_label);
	void promote_queued_task(const String &p_task, int p_steps);

	EditorBackgroundTaskPanel();
};
