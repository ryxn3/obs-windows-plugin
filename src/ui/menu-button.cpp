// "Camera Frame" menu in OBS Studio's menu bar, the right-click "Add Windows
// Camera Frame" entry on sources, and the first-run welcome dialog.
//
// Uses the OBS frontend API to reach the main window. No Q_OBJECT classes, so
// no moc step is needed.

#include "menu-button.h"
#include "dialogs.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <cstring>

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QMainWindow>
#include "../wf-notify.h"
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QString>

#include "../wf-actions.h"
#include "../wf-prefs.h"
#include "../win-frame-styles.h"

#ifndef WF_PLUGIN_VERSION
#define WF_PLUGIN_VERSION "unknown"
#endif

/* ------------------------------------------------------------------ menu */

static QPointer<QMenu> g_menu;
static QPointer<QAction> g_menu_action;
static bool g_callback_registered = false;

static void add_frame_to_selection(QWidget *parent)
{
	char msg[256] = {0};
	size_t n = wf_add_filter_to_selected(msg, sizeof(msg));
	if (n)
		wf_ui_status("Windows Camera Frame added to the selected source.");
	else
		QMessageBox::information(parent, "Windows Camera Frame", QString::fromUtf8(msg));
}

static void build_menu(QMainWindow *win)
{
	QMenuBar *bar = win->menuBar();
	g_menu = bar->addMenu("Camera Frame");
	g_menu_action = g_menu->menuAction();
	g_menu->menuAction()->setToolTip("Windows Camera Frame plugin");

	QAction *add = g_menu->addAction("Add Frame to Selected Source");
	QObject::connect(add, &QAction::triggered, win, [win]() { add_frame_to_selection(win); });

	g_menu->addSeparator();

	QMenu *quick = g_menu->addMenu("Apply Style to Current Scene");
	for (int i = 0; i < STYLE_COUNT - 1; i++) {
		QString id = QString::fromUtf8(win_frame_style_table[i].id);
		QAction *a = quick->addAction(QString::fromUtf8(win_frame_style_table[i].display));
		QObject::connect(a, &QAction::triggered, win, [win, id]() {
			size_t n = wf_scene_apply_style(id.toUtf8().constData());
			if (n)
				wf_ui_status(QString("Applied the style to %1 frame(s) in the current scene.").arg(n).toUtf8().constData());
			else
				QMessageBox::information(win, "Windows Camera Frame",
							 "The current scene has no Windows Camera Frame filters yet.");
		});
	}

	QAction *copy = g_menu->addAction("Copy Style Between Sources...");
	QObject::connect(copy, &QAction::triggered, win, []() { wf_ui_show_copy_dialog(); });

	g_menu->addSeparator();

	QMenu *tests = g_menu->addMenu("Test Toast / Assistant");
	QAction *tt = tests->addAction("Send Test Toast");
	QObject::connect(tt, &QAction::triggered, win, []() {
		struct wf_msg m = {"follow", "New follower", "Oskar just followed the stream!"};
		wf_notify_push(WF_TARGET_TOAST, &m);
	});
	QAction *ta = tests->addAction("Send Test Assistant Message");
	QObject::connect(ta, &QAction::triggered, win, []() {
		struct wf_msg m = {"tip", "", "It looks like you're streaming. Would you like help with that?"};
		wf_notify_push(WF_TARGET_ASSISTANT, &m);
	});

	g_menu->addSeparator();

	QAction *presets = g_menu->addAction("Preset Manager...");
	QObject::connect(presets, &QAction::triggered, win, []() { wf_ui_show_presets_dialog(); });
	QAction *download = g_menu->addAction("Download Presets...");
	QObject::connect(download, &QAction::triggered, win, []() { wf_ui_show_download_dialog(); });

	g_menu->addSeparator();

	QAction *settings = g_menu->addAction("Settings...");
	QObject::connect(settings, &QAction::triggered, win, []() { wf_ui_show_settings_dialog(); });
	QAction *upd = g_menu->addAction("Check for Updates...");
	QObject::connect(upd, &QAction::triggered, win, []() { wf_ui_check_updates(); });
	QAction *welcome = g_menu->addAction("Quick Start...");
	QObject::connect(welcome, &QAction::triggered, win, []() { wf_ui_show_welcome(); });
	QAction *about = g_menu->addAction(QString("About (version %1)").arg(WF_PLUGIN_VERSION));
	QObject::connect(about, &QAction::triggered, win, []() { wf_ui_show_about(); });
}

/* ------------------------------------- right-click entry in the Sources list */

class MenuWatcher : public QObject {
public:
	bool eventFilter(QObject *o, QEvent *e) override
	{
		if (e->type() != QEvent::Polish)
			return false;
		QMenu *m = qobject_cast<QMenu *>(o);
		if (!m || m->property("wf_done").toBool())
			return false;
		QWidget *mw = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (!mw || m->parentWidget() != mw)
			return false;

		QAction *filters = nullptr;
		for (QAction *a : m->actions()) {
			QString t = a->text();
			t.remove('&');
			if (t == "Filters") {
				filters = a;
				break;
			}
		}
		if (!filters)
			return false;
		m->setProperty("wf_done", true);

		QAction *add = new QAction("Add Windows Camera Frame", m);
		QObject::connect(add, &QAction::triggered, mw, [mw]() { add_frame_to_selection(mw); });

		const QList<QAction *> acts = m->actions();
		int idx = acts.indexOf(filters);
		QAction *next = (idx >= 0 && idx + 1 < acts.size()) ? acts[idx + 1] : nullptr;
		m->insertAction(next, add);
		return false;
	}
};

static MenuWatcher *g_watcher = nullptr;

/* ------------------------------------------------------------- lifecycle */

static void create_ui()
{
	if (g_menu)
		return;
	QMainWindow *win = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!win || !win->menuBar())
		return;

	build_menu(win);

	if (!g_watcher) {
		g_watcher = new MenuWatcher();
		qApp->installEventFilter(g_watcher);
	}

	if (!wf_prefs_get()->welcome_shown)
		QMetaObject::invokeMethod(win, []() { wf_ui_show_welcome(); }, Qt::QueuedConnection);
}

static void destroy_ui()
{
	if (g_watcher) {
		qApp->removeEventFilter(g_watcher);
		delete g_watcher;
		g_watcher = nullptr;
	}
	if (g_menu) {
		QMainWindow *win = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (win && win->menuBar() && g_menu_action)
			win->menuBar()->removeAction(g_menu_action.data());
		delete g_menu.data();
	}
	g_menu = nullptr;
}

static void on_frontend_event(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		create_ui();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		destroy_ui();
		break;
	default:
		break;
	}
}

extern "C" void wf_ui_init(void)
{
	if (g_callback_registered)
		return;
	obs_frontend_add_event_callback(on_frontend_event, nullptr);
	g_callback_registered = true;
}

extern "C" void wf_ui_shutdown(void)
{
	if (g_callback_registered) {
		obs_frontend_remove_event_callback(on_frontend_event, nullptr);
		g_callback_registered = false;
	}
	destroy_ui();
}
