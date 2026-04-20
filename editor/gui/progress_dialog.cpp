/**************************************************************************/
/*  progress_dialog.cpp                                                   */
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

#include "progress_dialog.h"

#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/themes/editor_scale.h"
#include "main/main.h"
#include "scene/gui/panel_container.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

// `BackgroundProgress` used to live here but was never actually added to the scene tree, so every
// call to `EditorProgressBG` updated an invisible widget. It has been replaced by
// `EditorBackgroundTaskPanel`, hosted by `EditorBottomPanel` and routed through
// `EditorNode::progress_*_task_bg`.

ProgressDialog *ProgressDialog::singleton = nullptr;

void ProgressDialog::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			Ref<StyleBox> style = main->get_theme_stylebox(SceneStringName(panel), SNAME("PopupMenu"));
			main_border_size = style->get_minimum_size();
			main->set_offset(SIDE_LEFT, style->get_margin(SIDE_LEFT));
			main->set_offset(SIDE_RIGHT, -style->get_margin(SIDE_RIGHT));
			main->set_offset(SIDE_TOP, style->get_margin(SIDE_TOP));
			main->set_offset(SIDE_BOTTOM, -style->get_margin(SIDE_BOTTOM));

			center_panel->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SceneStringName(panel), "PopupPanel"));
		} break;
	}
}

void ProgressDialog::_update_ui() {
	// `DisplayServer::process_events()` and `Main::iteration()` are both main-thread-only. Any
	// off-thread caller that reaches this (e.g. via a stray `ProgressDialog::task_step()` from a
	// worker during off-thread export) would flood the log and corrupt rendering. Bail silently
	// — the worker's EditorProgress has already routed progress to the footer panel in parallel.
	if (!Thread::is_main_thread()) {
		return;
	}
	// Run main loop for two frames.
	if (is_inside_tree()) {
		DisplayServer::get_singleton()->process_events();
		Main::iteration();
	}
}

void ProgressDialog::_popup() {
	// Activate processing of all inputs in EditorNode, and the EditorNode::input method
	// will discard every key input.
	EditorNode::get_singleton()->set_process_input(true);
	// Disable all other windows to prevent interaction with them.
	for (ObjectID wid : host_windows) {
		Window *w = ObjectDB::get_instance<Window>(wid);
		if (w) {
			w->set_process_mode(PROCESS_MODE_DISABLED);
		}
	}

	Size2 ms = main->get_combined_minimum_size();
	ms.width = MAX(500 * EDSCALE, ms.width);
	ms += main_border_size;

	center_panel->set_custom_minimum_size(ms);

	if (is_ready()) {
		_reparent_and_show();
	} else {
		callable_mp(this, &ProgressDialog::_reparent_and_show).call_deferred();
	}
}

void ProgressDialog::_reparent_and_show() {
	Window *current_window = SceneTree::get_singleton()->get_root()->get_last_exclusive_window();
	ERR_FAIL_NULL(current_window);
	reparent(current_window);

	// Ensures that events are properly released before the dialog blocks input.
	bool window_is_input_disabled = current_window->is_input_disabled();
	current_window->set_disable_input(!window_is_input_disabled);
	current_window->set_disable_input(window_is_input_disabled);

	show();
}

void ProgressDialog::add_task(const String &p_task, const String &p_label, int p_steps, bool p_can_cancel) {
	// Hard guard: this method mutates the scene tree (add_child on `main`, popping the dialog
	// window, pumping `DisplayServer::process_events()`) which is main-thread-only. Redirect any
	// off-thread caller to the non-blocking footer panel so we don't flood the log with
	// thread-safety errors or corrupt rendering during an off-thread export.
	if (!Thread::is_main_thread()) {
		EditorNode::progress_add_task_bg(p_task, p_label, p_steps);
		return;
	}
	if (MessageQueue::get_singleton()->is_flushing()) {
		ERR_PRINT("Do not use progress dialog (task) while flushing the message queue or using call_deferred()!");
		return;
	}

	ERR_FAIL_COND_MSG(tasks.has(p_task), "Task '" + p_task + "' already exists.");
	ProgressDialog::Task t;
	t.vb = memnew(VBoxContainer);
	VBoxContainer *vb2 = memnew(VBoxContainer);
	t.vb->add_margin_child(p_label, vb2);
	t.progress = memnew(ProgressBar);
	t.progress->set_theme_type_variation("PopupProgressBar");
	t.progress->set_max(p_steps);
	t.progress->set_value(p_steps);
	vb2->add_child(t.progress);
	t.state = memnew(Label);
	t.state->set_clip_text(true);
	vb2->add_child(t.state);
	main->add_child(t.vb);

	tasks[p_task] = t;
	if (p_can_cancel) {
		cancel_hb->show();
	} else {
		cancel_hb->hide();
	}
	cancel_hb->move_to_front();
	canceled = false;
	_popup();
	if (p_can_cancel) {
		cancel->grab_focus();
	}
	_update_ui();
}

bool ProgressDialog::task_step(const String &p_task, const String &p_state, int p_step, bool p_force_redraw) {
	if (!Thread::is_main_thread()) {
		EditorNode::progress_task_step_bg(p_task, p_step);
		return false;
	}
	ERR_FAIL_COND_V(!tasks.has(p_task), canceled);

	Task &t = tasks[p_task];
	if (!p_force_redraw) {
		uint64_t tus = OS::get_singleton()->get_ticks_usec();
		if (tus - t.last_progress_tick < 200000) { //200ms
			return canceled;
		}
	}
	if (p_step < 0) {
		t.progress->set_value(t.progress->get_value() + 1);
	} else {
		t.progress->set_value(p_step);
	}

	t.state->set_text(p_state);
	t.last_progress_tick = OS::get_singleton()->get_ticks_usec();
	_update_ui();

	return canceled;
}

void ProgressDialog::end_task(const String &p_task) {
	if (!Thread::is_main_thread()) {
		EditorNode::progress_end_task_bg(p_task);
		return;
	}
	ERR_FAIL_COND(!tasks.has(p_task));
	Task &t = tasks[p_task];

	memdelete(t.vb);
	tasks.erase(p_task);

	if (tasks.is_empty()) {
		hide();
		EditorNode::get_singleton()->set_process_input(false);
		for (ObjectID wid : host_windows) {
			Window *w = ObjectDB::get_instance<Window>(wid);
			if (w) {
				w->set_process_mode(PROCESS_MODE_INHERIT);
			}
		}
	} else {
		_popup();
	}
}

void ProgressDialog::add_host_window(ObjectID p_window) {
	host_windows.push_back(p_window);
}

void ProgressDialog::remove_host_window(ObjectID p_window) {
	host_windows.erase(p_window);
}

void ProgressDialog::_cancel_pressed() {
	canceled = true;
}

ProgressDialog::ProgressDialog() {
	// We want to cover the entire screen to prevent the user from interacting with the Editor.
	set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	// Be sure it's the top most component.
	set_z_index(RSE::CANVAS_ITEM_Z_MAX);
	singleton = this;
	hide();

	center_panel = memnew(PanelContainer);
	add_child(center_panel);
	center_panel->set_h_size_flags(SIZE_SHRINK_BEGIN);
	center_panel->set_v_size_flags(SIZE_SHRINK_BEGIN);

	main = memnew(VBoxContainer);
	center_panel->add_child(main);

	cancel_hb = memnew(HBoxContainer);
	main->add_child(cancel_hb);
	cancel_hb->hide();
	cancel = memnew(Button);
	cancel_hb->add_spacer();
	cancel_hb->add_child(cancel);
	cancel->set_text(TTR("Cancel"));
	cancel_hb->add_spacer();
	cancel->connect(SceneStringName(pressed), callable_mp(this, &ProgressDialog::_cancel_pressed));
}

ProgressDialog::~ProgressDialog() {
	singleton = nullptr;
}
