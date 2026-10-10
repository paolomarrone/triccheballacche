#include "ui_internal.h"
#include <stdlib.h>
#include <unistd.h>

struct UI {
	const UIOps *ops;
	void *view;
};
int ui_available(const Node *node) {
	return node->info.ui && (node->config.format == PLUGIN_CLAP || !access(node->info.ui, R_OK));
}
int ui_open(UI **out, Session *session, int id) {
	*out = NULL;
	const Node *node = session_score(session)->nodes + id;
	if (!ui_available(node))
		return 0;
	UI *ui = calloc(1, sizeof(*ui));
	if (!ui)
		return -1;
	ui->ops = node->config.format == PLUGIN_CLAP ? &clap_ui_ops : &perone_ui_ops;
	if (ui->ops->open(&ui->view, session, id)) {
		free(ui);
		return -1;
	}
	*out = ui;
	return 0;
}
void ui_show(UI *ui, int visible) {
	if (ui)
		ui->ops->show(ui->view, visible);
}
int ui_poll(UI *ui) {
	return ui ? ui->ops->poll(ui->view) : 0;
}
void ui_close(UI *ui) {
	if (ui) {
		ui->ops->close(ui->view);
		free(ui);
	}
}
