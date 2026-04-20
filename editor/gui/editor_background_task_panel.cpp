/**************************************************************************/
/*  editor_background_task_panel.cpp                                      */
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

#include "editor_background_task_panel.h"

#include "core/object/callable_mp.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/label.h"
#include "scene/gui/popup.h"
#include "scene/gui/progress_bar.h"

void EditorBackgroundTaskPanel::add_task(const String &p_task, const String &p_label, int p_steps) {
	callable_mp(this, &EditorBackgroundTaskPanel::_deferred_add_task).call_deferred(p_task, p_label, p_steps, false);
}

void EditorBackgroundTaskPanel::add_queued_task(const String &p_task, const String &p_label) {
	// Steps of 1 is just a placeholder — queued rows don't render a progress bar.
	callable_mp(this, &EditorBackgroundTaskPanel::_deferred_add_task).call_deferred(p_task, p_label, 1, true);
}

void EditorBackgroundTaskPanel::promote_queued_task(const String &p_task, int p_steps) {
	callable_mp(this, &EditorBackgroundTaskPanel::_deferred_promote_queued_task).call_deferred(p_task, p_steps);
}

void EditorBackgroundTaskPanel::task_step(const String &p_task, int p_step) {
	// Same coalescing pattern as the old `BackgroundProgress`: only fire one deferred drain per
	// idle->busy transition. High-frequency step calls from a worker thread collapse into a single
	// main-thread flush.
	bool was_empty;
	{
		_THREAD_SAFE_METHOD_
		was_empty = pending_updates.is_empty();
		pending_updates[p_task] = p_step;
	}
	if (was_empty) {
		callable_mp(this, &EditorBackgroundTaskPanel::_drain_pending_updates).call_deferred();
	}
}

void EditorBackgroundTaskPanel::end_task(const String &p_task) {
	callable_mp(this, &EditorBackgroundTaskPanel::_deferred_end_task).call_deferred(p_task);
}

void EditorBackgroundTaskPanel::_deferred_add_task(const String &p_task, const String &p_label, int p_steps, bool p_queued) {
	_THREAD_SAFE_METHOD_
	if (tasks.has(p_task)) {
		return; // Idempotent — duplicate add from a racing caller.
	}
	Task t;
	t.label = p_label;
	t.steps = MAX(1, p_steps);
	t.current = 0;
	t.queued = p_queued;
	tasks[p_task] = t;

	_refresh_pill();
	if (popup->is_visible()) {
		_rebuild_popup_rows();
	}
}

void EditorBackgroundTaskPanel::_deferred_promote_queued_task(const String &p_task, int p_steps) {
	_THREAD_SAFE_METHOD_
	HashMap<String, Task>::Iterator it = tasks.find(p_task);
	if (!it) {
		// Promote on a task we don't have (e.g., ended before promotion ran). Treat as fresh add.
		Task t;
		t.label = p_task;
		t.steps = MAX(1, p_steps);
		t.queued = false;
		tasks[p_task] = t;
	} else {
		it->value.queued = false;
		it->value.steps = MAX(1, p_steps);
		it->value.current = 0;
	}

	_refresh_pill();
	if (popup->is_visible()) {
		_rebuild_popup_rows();
	}
}

void EditorBackgroundTaskPanel::_deferred_task_step(const String &p_task, int p_step) {
	_THREAD_SAFE_METHOD_
	HashMap<String, Task>::Iterator it = tasks.find(p_task);
	if (!it) {
		return;
	}
	if (p_step < 0) {
		it->value.current += 1;
	} else {
		it->value.current = p_step;
	}
	if (it->value.row_progress) {
		it->value.row_progress->set_value(it->value.current);
	}
	_refresh_pill();
}

void EditorBackgroundTaskPanel::_deferred_end_task(const String &p_task) {
	_THREAD_SAFE_METHOD_
	HashMap<String, Task>::Iterator it = tasks.find(p_task);
	if (!it) {
		return;
	}
	if (it->value.row) {
		memdelete(it->value.row);
	}
	tasks.erase(p_task);

	_refresh_pill();
	if (popup->is_visible() && tasks.is_empty()) {
		popup->hide();
	}
}

void EditorBackgroundTaskPanel::_drain_pending_updates() {
	HashMap<String, int> drained;
	{
		_THREAD_SAFE_METHOD_
		drained = pending_updates;
		pending_updates.clear();
	}
	for (const KeyValue<String, int> &kv : drained) {
		_deferred_task_step(kv.key, kv.value);
	}
}

void EditorBackgroundTaskPanel::_refresh_pill() {
	// Pill only represents running work. Queued tasks are surfaced through the popup, so a
	// pill with only queued entries would falsely imply active progress.
	const Task *first_running = nullptr;
	int running_count = 0;
	int queued_count = 0;
	for (const KeyValue<String, Task> &kv : tasks) {
		if (kv.value.queued) {
			queued_count++;
		} else {
			running_count++;
			if (!first_running) {
				first_running = &kv.value;
			}
		}
	}

	if (running_count == 0) {
		hide();
		return;
	}
	show();

	pill_progress->set_max(first_running->steps);
	pill_progress->set_value(first_running->current);

	String label = first_running->label;
	const int extras = (running_count - 1) + queued_count;
	if (extras > 0) {
		// Show how many additional tasks (running or queued) are hidden behind the pill.
		label = vformat(TTR("%s (+%d more)"), label, extras);
	}
	pill_label->set_text(label);
}

void EditorBackgroundTaskPanel::_rebuild_popup_rows() {
	// Drop stale row pointers before rebuilding — the children below will be freed in one sweep.
	for (KeyValue<String, Task> &kv : tasks) {
		kv.value.row = nullptr;
		kv.value.row_label = nullptr;
		kv.value.row_progress = nullptr;
	}
	while (popup_vbox->get_child_count() > 0) {
		Node *c = popup_vbox->get_child(0);
		popup_vbox->remove_child(c);
		memdelete(c);
	}

	if (tasks.is_empty()) {
		Label *empty = memnew(Label);
		empty->set_text(TTR("No background tasks."));
		popup_vbox->add_child(empty);
		return;
	}

	// Render running tasks first, then queued ones. Within each section we preserve insertion
	// order so the popup reads top-to-bottom like a timeline.
	const Color queued_modulate = _get_queued_modulate();
	for (int pass = 0; pass < 2; pass++) {
		const bool want_queued = (pass == 1);
		for (KeyValue<String, Task> &kv : tasks) {
			if (kv.value.queued != want_queued) {
				continue;
			}
			HBoxContainer *row = memnew(HBoxContainer);
			row->add_theme_constant_override("separation", 8 * EDSCALE);

			Label *lbl = memnew(Label);
			lbl->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
			lbl->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
			if (kv.value.queued) {
				// TRANSLATORS: Suffix shown on queued (not yet running) background tasks.
				lbl->set_text(vformat(TTR("%s — waiting"), kv.value.label));
			} else {
				lbl->set_text(kv.value.label);
			}
			row->add_child(lbl);

			if (kv.value.queued) {
				// Queued rows get no progress bar — they're just placeholders. A dim modulate
				// makes them visually distinct from the running tasks above.
				row->set_self_modulate(queued_modulate);
			} else {
				ProgressBar *pb = memnew(ProgressBar);
				pb->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
				pb->set_max(kv.value.steps);
				pb->set_value(kv.value.current);
				pb->set_h_size_flags(SIZE_EXPAND_FILL);
				row->add_child(pb);
				kv.value.row_progress = pb;
			}

			popup_vbox->add_child(row);
			kv.value.row = row;
			kv.value.row_label = lbl;
		}
	}
}

Color EditorBackgroundTaskPanel::_get_queued_modulate() const {
	// ~55% alpha on the default text color reads as "disabled but present" without requiring a
	// theme lookup — the popup uses the editor's base PopupPanel stylebox so inherited colors
	// already match.
	return Color(1, 1, 1, 0.55);
}

void EditorBackgroundTaskPanel::_toggle_popup() {
	if (popup->is_visible()) {
		popup->hide();
		return;
	}
	_rebuild_popup_rows();

	// Anchor above the pill, right-aligned with our right edge so the popup grows leftward as
	// more tasks are added without clipping off-screen.
	const Size2 size = popup->get_contents_minimum_size();
	Vector2 global_pos = get_screen_position();
	global_pos.x += get_size().x - size.x;
	global_pos.y -= size.y + 4 * EDSCALE;
	popup->popup(Rect2i(global_pos, size));
}

void EditorBackgroundTaskPanel::_on_popup_hidden() {
	// Row controls were children of `popup_vbox`; they'll be freed on the next rebuild. Just null
	// the back-pointers so stale steps don't try to update them.
	for (KeyValue<String, Task> &kv : tasks) {
		kv.value.row = nullptr;
		kv.value.row_label = nullptr;
		kv.value.row_progress = nullptr;
	}
}

void EditorBackgroundTaskPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_MOUSE_ENTER:
		case NOTIFICATION_MOUSE_EXIT: {
			queue_redraw();
		} break;
	}
}

void EditorBackgroundTaskPanel::gui_input(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT) {
		_toggle_popup();
		accept_event();
	}
}

EditorBackgroundTaskPanel::EditorBackgroundTaskPanel() {
	set_mouse_filter(MOUSE_FILTER_STOP);
	add_theme_constant_override("separation", 6 * EDSCALE);
	hide(); // No tasks yet.

	pill_progress = memnew(ProgressBar);
	pill_progress->set_theme_type_variation("PopupProgressBar");
	pill_progress->set_custom_minimum_size(Size2(80, 0) * EDSCALE);
	pill_progress->set_show_percentage(false);
	pill_progress->set_v_size_flags(SIZE_SHRINK_CENTER);
	pill_progress->set_mouse_filter(MOUSE_FILTER_IGNORE); // Let clicks reach the HBox.
	add_child(pill_progress);

	pill_label = memnew(Label);
	pill_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	pill_label->set_custom_minimum_size(Size2(120, 0) * EDSCALE);
	pill_label->set_v_size_flags(SIZE_SHRINK_CENTER);
	pill_label->set_mouse_filter(MOUSE_FILTER_IGNORE);
	add_child(pill_label);

	popup = memnew(PopupPanel);
	popup->connect("popup_hide", callable_mp(this, &EditorBackgroundTaskPanel::_on_popup_hidden));
	add_child(popup);

	popup_vbox = memnew(VBoxContainer);
	popup_vbox->add_theme_constant_override("separation", 6 * EDSCALE);
	popup->add_child(popup_vbox);
}
