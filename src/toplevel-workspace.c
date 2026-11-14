// SPDX-License-Identifier: GPL-2.0-only
/*
 * zhenn_toplevel_workspace_manager_v1: lets a panel move a window to a
 * workspace, e.g. when an app icon or the window itself (dragged by its
 * titlebar) is dropped on a workspace button.
 *
 * When an interactive move ends over a layer-shell surface, bound clients
 * get toplevel_dropped with the cursor position. If one of them answers
 * with move_to_workspace soon after, the window also goes back to where
 * it was before the move, so it does not stay parked over the panel.
 */
#include "toplevel-workspace.h"
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/util/log.h>
#include "foreign-toplevel/foreign.h"
#include "henn-toplevel-workspace-unstable-v1-protocol.h"
#include "input/cursor.h"
#include "labwc.h"
#include "output.h"
#include "view.h"
#include "workspaces.h"

#define MANAGER_VERSION 1
/* How long after a drop move_to_workspace still restores the geometry */
#define DROP_TIMEOUT_MS 2000

static struct {
	struct wl_global *global;
	struct wl_list resources;
	struct wl_listener on_display_destroy;
	/* Last drop; only compared, never dereferenced */
	const struct view *dropped_view;
	int64_t dropped_msec;
} state;

static int64_t
now_msec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void
move_to_workspace(struct view *view, struct workspace *workspace)
{
	bool dropped = view == state.dropped_view
		&& now_msec() - state.dropped_msec < DROP_TIMEOUT_MS;
	state.dropped_view = NULL;

	if (dropped && !view->fullscreen) {
		/* Undo where the move left it (maybe snapped or maximized) */
		view_maximize(view, VIEW_AXIS_NONE);
		view_set_untiled(view);
		if (view_is_floating(view)) {
			view_apply_natural_geometry(view);
		}
	}

	if (view->workspace == workspace) {
		return;
	}
	if (view->visible_on_all_workspaces) {
		view_toggle_visible_on_all_workspaces(view);
	}
	view_move_to_workspace(view, workspace);
	/* The workspace may live on another output */
	if (server.workspaces.per_output
			&& output_is_usable(workspace->output)
			&& view->output != workspace->output) {
		view_move_to_output(view, workspace->output);
	}
	if (server.active_view == view && !workspaces_is_visible(workspace)) {
		desktop_focus_topmost_view();
	}
	wlr_log(WLR_INFO, "moved view to workspace %s", workspace->name);
}

static void
handle_move_to_workspace(struct wl_client *client, struct wl_resource *resource,
		struct wl_resource *toplevel_resource, const char *name)
{
	struct wlr_foreign_toplevel_handle_v1 *handle =
		wl_resource_get_user_data(toplevel_resource);
	struct view *view = handle ? handle->data : NULL;
	struct workspace *workspace = workspaces_find_exact(name);
	if (!view || !workspace) {
		return;
	}
	move_to_workspace(view, workspace);
}

static void
handle_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zhenn_toplevel_workspace_manager_v1_interface impl = {
	.destroy = handle_destroy,
	.move_to_workspace = handle_move_to_workspace,
};

static void
handle_resource_destroy(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
handle_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
		&zhenn_toplevel_workspace_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &impl, NULL,
		handle_resource_destroy);
	wl_list_insert(&state.resources, wl_resource_get_link(resource));
}

static void
handle_display_destroy(struct wl_listener *listener, void *data)
{
	wl_list_remove(&state.on_display_destroy.link);
	state.global = NULL;
}

void
toplevel_workspace_init(void)
{
	wl_list_init(&state.resources);
	state.global = wl_global_create(server.wl_display,
		&zhenn_toplevel_workspace_manager_v1_interface, MANAGER_VERSION,
		NULL, handle_bind);
	if (!state.global) {
		wlr_log(WLR_ERROR, "cannot create toplevel-workspace global");
		return;
	}
	state.on_display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(server.wl_display,
		&state.on_display_destroy);
}

void
toplevel_workspace_notify_move_end(struct view *view, double x, double y)
{
	if (!state.global || wl_list_empty(&state.resources)) {
		return;
	}
	struct wlr_foreign_toplevel_handle_v1 *handle = view->foreign_toplevel
		? foreign_toplevel_get_wlr_handle(view->foreign_toplevel) : NULL;
	if (!handle) {
		return;
	}
	struct cursor_context ctx = get_cursor_context();
	if (ctx.type != LAB_NODE_LAYER_SURFACE) {
		return;
	}

	state.dropped_view = view;
	state.dropped_msec = now_msec();

	struct wl_resource *resource;
	wl_resource_for_each(resource, &state.resources) {
		struct wl_client *client = wl_resource_get_client(resource);
		struct wl_resource *toplevel;
		wl_resource_for_each(toplevel, &handle->resources) {
			if (wl_resource_get_client(toplevel) == client) {
				zhenn_toplevel_workspace_manager_v1_send_toplevel_dropped(
					resource, toplevel, (int)x, (int)y);
				break;
			}
		}
	}
}
