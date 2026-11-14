/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_TOPLEVEL_WORKSPACE_H
#define LABWC_TOPLEVEL_WORKSPACE_H

struct view;

/* zhenn_toplevel_workspace_manager_v1: panels move views to workspaces */
void toplevel_workspace_init(void);

/* Called when an interactive move of @view ends; x/y in layout coords */
void toplevel_workspace_notify_move_end(struct view *view, double x, double y);

#endif /* LABWC_TOPLEVEL_WORKSPACE_H */
