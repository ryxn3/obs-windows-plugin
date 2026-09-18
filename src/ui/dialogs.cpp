// Qt dialogs for the Windows Camera Frame plugin.
// No Q_OBJECT classes are used (handlers are lambdas), so no moc step is needed.

#include "dialogs.h"
#include "../wf-notify.h"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <cstring>
#include <string>
#include <thread>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QSpinBox>
#include <QClipboard>
#include <QStringList>
#include <QUrl>
#include <QVBoxLayout>

#include "../wf-actions.h"
#include "../wf-prefs.h"
#include "../win-frame-presets.h"
#include "../win-frame-styles.h"

#ifndef WF_PLUGIN_VERSION
#define WF_PLUGIN_VERSION "unknown"
#endif

static QWidget *main_window()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

static void copy_out(char *out, size_t n, const QString &s)
{
	QByteArray b = s.toUtf8();
	size_t len = (size_t)b.size() < n - 1 ? (size_t)b.size() : n - 1;
	memcpy(out, b.constData(), len);
	out[len] = 0;
}

/* ------------------------------------------------------------ small helpers */

extern "C" bool wf_ui_get_open_path(const char *title, const char *filter, char *out, size_t out_sz)
{
	QString p = QFileDialog::getOpenFileName(main_window(), QString::fromUtf8(title), QString(),
						 QString::fromUtf8(filter));
	if (p.isEmpty())
		return false;
	copy_out(out, out_sz, p);
	return true;
}

extern "C" bool wf_ui_get_save_path(const char *title, const char *default_name, const char *filter, char *out,
				    size_t out_sz)
{
	QString p = QFileDialog::getSaveFileName(main_window(), QString::fromUtf8(title),
						 QString::fromUtf8(default_name), QString::fromUtf8(filter));
	if (p.isEmpty())
		return false;
	copy_out(out, out_sz, p);
	return true;
}

extern "C" bool wf_ui_get_text(const char *title, const char *label, const char *initial, char *out, size_t out_sz)
{
	bool ok = false;
	QString t = QInputDialog::getText(main_window(), QString::fromUtf8(title), QString::fromUtf8(label),
					  QLineEdit::Normal, QString::fromUtf8(initial ? initial : ""), &ok);
	if (!ok || t.trimmed().isEmpty())
		return false;
	copy_out(out, out_sz, t.trimmed());
	return true;
}

extern "C" void wf_ui_message(const char *title, const char *text)
{
	QMessageBox::information(main_window(), QString::fromUtf8(title), QString::fromUtf8(text));
}

extern "C" void wf_ui_status(const char *text)
{
	QMainWindow *w = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (w && w->statusBar())
		w->statusBar()->showMessage(QString::fromUtf8(text), 6000);
}

/* -------------------------------------------------------------------- about */

extern "C" void wf_ui_show_about(void)
{
	QMessageBox::about(main_window(), "About Windows Camera Frame",
			   QString("<b>Windows Camera Frame</b><br>Version %1<br><br>"
				   "Wraps a camera or video source in a Windows-era (3.1 to 11) or other "
				   "OS window frame, with 36 built-in styles, presets and camera effects.<br><br>"
				   "Built %2.")
				   .arg(WF_PLUGIN_VERSION)
				   .arg(__DATE__));
}

/* ------------------------------------------------------------------ welcome */

extern "C" void wf_ui_show_welcome(void)
{
	QDialog *dlg = new QDialog(main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("Welcome to Windows Camera Frame");
	auto *lay = new QVBoxLayout(dlg);
	auto *text = new QLabel(
		"<h3>Windows Camera Frame is installed</h3>"
		"<ol>"
		"<li>Right-click a camera or video source and choose <b>Add Windows Camera Frame</b> "
		"(or open <b>Filters</b> and add it there).</li>"
		"<li>Pick a look from the <b>Windows Style</b> list: Windows 3.1 to 11, XP Luna variants, "
		"Aero, Metro, Windows Phone, macOS, GNOME, KDE, Amiga and Atari.</li>"
		"<li>Use the <b>Camera Frame</b> menu at the top of OBS to apply a style to the whole "
		"scene, copy a style between sources, manage and download presets, and change settings.</li>"
		"</ol>");
	text->setWordWrap(true);
	text->setTextFormat(Qt::RichText);
	lay->addWidget(text);

	auto *again = new QCheckBox("Show this again next time OBS starts");
	lay->addWidget(again);

	auto *row = new QHBoxLayout();
	auto *add = new QPushButton("Add a frame to the selected source");
	auto *close = new QPushButton("Close");
	row->addWidget(add);
	row->addStretch(1);
	row->addWidget(close);
	lay->addLayout(row);

	QObject::connect(add, &QPushButton::clicked, dlg, [dlg]() {
		char msg[256];
		size_t n = wf_add_filter_to_selected(msg, sizeof(msg));
		if (n)
			wf_ui_status("Camera Frame added to the selected source.");
		else
			QMessageBox::information(dlg, "Windows Camera Frame", QString::fromUtf8(msg));
	});
	QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
	QObject::connect(dlg, &QDialog::finished, dlg, [again]() {
		wf_prefs_get()->welcome_shown = !again->isChecked();
		wf_prefs_save();
	});
	dlg->show();
}

/* ---------------------------------------------------------- preset manager */

static void collect_name(const char *name, void *p)
{
	static_cast<QStringList *>(p)->append(QString::fromUtf8(name));
}

static void refill(QListWidget *list)
{
	QStringList names;
	win_frame_presets_enum(collect_name, &names);
	names.sort(Qt::CaseInsensitive);
	list->clear();
	list->addItems(names);
}

extern "C" void wf_ui_show_presets_dialog(void)
{
	QDialog *dlg = new QDialog(main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("Camera Frame - Preset Manager");
	dlg->resize(460, 420);

	auto *lay = new QVBoxLayout(dlg);
	lay->addWidget(new QLabel("Presets are JSON files. Import, export, rename and delete them here."));
	auto *list = new QListWidget();
	list->setSelectionMode(QAbstractItemView::SingleSelection);
	lay->addWidget(list, 1);
	refill(list);

	auto *row1 = new QHBoxLayout();
	auto *bImport = new QPushButton("Import...");
	auto *bExport = new QPushButton("Export...");
	auto *bRename = new QPushButton("Rename...");
	row1->addWidget(bImport);
	row1->addWidget(bExport);
	row1->addWidget(bRename);
	lay->addLayout(row1);
	auto *row2 = new QHBoxLayout();
	auto *bDup = new QPushButton("Duplicate");
	auto *bDel = new QPushButton("Delete");
	auto *bNet = new QPushButton("Download online...");
	row2->addWidget(bDup);
	row2->addWidget(bDel);
	row2->addWidget(bNet);
	lay->addLayout(row2);
	auto *bClose = new QPushButton("Close");
	lay->addWidget(bClose, 0, Qt::AlignRight);

	auto current = [list]() -> QString {
		auto *it = list->currentItem();
		return it ? it->text() : QString();
	};

	QObject::connect(bImport, &QPushButton::clicked, dlg, [dlg, list]() {
		const QStringList files = QFileDialog::getOpenFileNames(dlg, "Import presets", QString(), "Preset (*.json)");
		int ok = 0, bad = 0;
		for (const QString &f : files) {
			QString stem = QFileInfo(f).completeBaseName();
			if (win_frame_presets_import(f.toUtf8().constData(), stem.toUtf8().constData()))
				ok++;
			else
				bad++;
		}
		refill(list);
		if (!files.isEmpty())
			QMessageBox::information(dlg, "Import", QString("Imported %1 preset(s). %2 file(s) were not valid presets.")
									.arg(ok)
									.arg(bad));
	});
	QObject::connect(bExport, &QPushButton::clicked, dlg, [dlg, current]() {
		QString n = current();
		if (n.isEmpty())
			return;
		QString p = QFileDialog::getSaveFileName(dlg, "Export preset", n + ".json", "Preset (*.json)");
		if (p.isEmpty())
			return;
		if (!win_frame_presets_export(n.toUtf8().constData(), p.toUtf8().constData()))
			QMessageBox::warning(dlg, "Export", "The preset could not be exported.");
	});
	QObject::connect(bRename, &QPushButton::clicked, dlg, [dlg, list, current]() {
		QString n = current();
		if (n.isEmpty())
			return;
		bool ok = false;
		QString nn = QInputDialog::getText(dlg, "Rename preset", "New name:", QLineEdit::Normal, n, &ok).trimmed();
		if (!ok || nn.isEmpty() || nn == n)
			return;
		if (!win_frame_presets_rename(n.toUtf8().constData(), nn.toUtf8().constData()))
			QMessageBox::warning(dlg, "Rename", "The preset could not be renamed.");
		refill(list);
	});
	QObject::connect(bDup, &QPushButton::clicked, dlg, [list, current]() {
		QString n = current();
		if (n.isEmpty())
			return;
		win_frame_presets_duplicate(n.toUtf8().constData(), (n + " Copy").toUtf8().constData());
		refill(list);
	});
	QObject::connect(bDel, &QPushButton::clicked, dlg, [dlg, list, current]() {
		QString n = current();
		if (n.isEmpty())
			return;
		if (QMessageBox::question(dlg, "Delete preset", QString("Delete \"%1\"?").arg(n)) == QMessageBox::Yes) {
			win_frame_presets_delete(n.toUtf8().constData());
			refill(list);
		}
	});
	QObject::connect(bNet, &QPushButton::clicked, dlg, []() { wf_ui_show_download_dialog(); });
	QObject::connect(bClose, &QPushButton::clicked, dlg, &QDialog::accept);
	dlg->show();
}

/* ------------------------------------------------------------- copy style */

static void collect_framed_owner(obs_source_t *owner, obs_source_t *, void *p)
{
	QString n = QString::fromUtf8(obs_source_get_name(owner));
	QStringList *l = static_cast<QStringList *>(p);
	if (!l->contains(n))
		l->append(n);
}

static void collect_video_source(obs_source_t *src, void *p)
{
	static_cast<QStringList *>(p)->append(QString::fromUtf8(obs_source_get_name(src)));
}

extern "C" void wf_ui_show_copy_dialog(void)
{
	QStringList framed, all;
	wf_enum_framed(collect_framed_owner, &framed);
	wf_enum_video_sources(collect_video_source, &all);
	framed.sort(Qt::CaseInsensitive);
	all.sort(Qt::CaseInsensitive);

	if (framed.isEmpty()) {
		QMessageBox::information(main_window(), "Copy style",
					 "No source has a Windows Camera Frame yet. Add one first.");
		return;
	}

	QDialog *dlg = new QDialog(main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("Camera Frame - Copy Style");
	auto *form = new QFormLayout();
	auto *from = new QComboBox();
	from->addItems(framed);
	auto *to = new QComboBox();
	to->addItems(all);
	form->addRow("Copy the style from:", from);
	form->addRow("to this source:", to);

	auto *lay = new QVBoxLayout(dlg);
	lay->addLayout(form);
	auto *note = new QLabel("If the destination has no frame yet, one is added.");
	note->setWordWrap(true);
	lay->addWidget(note);
	auto *status = new QLabel();
	status->setWordWrap(true);
	lay->addWidget(status);
	auto *row = new QHBoxLayout();
	auto *go = new QPushButton("Copy style");
	auto *close = new QPushButton("Close");
	row->addStretch(1);
	row->addWidget(go);
	row->addWidget(close);
	lay->addLayout(row);

	QObject::connect(go, &QPushButton::clicked, dlg, [from, to, status]() {
		char msg[256];
		wf_copy_style(from->currentText().toUtf8().constData(), to->currentText().toUtf8().constData(), msg,
			      sizeof(msg));
		status->setText(QString::fromUtf8(msg));
	});
	QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
	dlg->show();
}

/* --------------------------------------------------------------- settings */

extern "C" void wf_ui_show_settings_dialog(void)
{
	QDialog *dlg = new QDialog(main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("Camera Frame - Settings");
	dlg->resize(520, 0);
	struct wf_prefs *pr = wf_prefs_get();

	auto *style = new QComboBox();
	int sel = 0;
	for (int i = 0; i < STYLE_COUNT - 1; i++) {
		style->addItem(QString::fromUtf8(win_frame_style_table[i].display),
			       QString::fromUtf8(win_frame_style_table[i].id));
		if (QString::fromUtf8(win_frame_style_table[i].id) == QString::fromUtf8(pr->default_style))
			sel = i;
	}
	style->setCurrentIndex(sel);
	auto *update = new QLineEdit(QString::fromUtf8(pr->update_url));
	update->setPlaceholderText("https://.../update.json");
	auto *download = new QLineEdit(QString::fromUtf8(pr->download_url));
	download->setPlaceholderText("https://.../presets.json");
	auto *welcome = new QCheckBox("Show the welcome dialog the next time OBS starts");
	welcome->setChecked(!pr->welcome_shown);

	auto *form = new QFormLayout();
	form->addRow("Default style for new frames:", style);
	form->addRow("Update check URL:", update);
	form->addRow("Preset download URL:", download);

	auto *http = new QCheckBox("Allow other programs on this PC to send toasts / assistant messages");
	http->setChecked(pr->http_enabled);
	auto *port = new QSpinBox();
	port->setRange(1024, 65535);
	port->setValue(pr->http_port);
	auto *token = new QLineEdit(QString::fromUtf8(pr->http_token));
	token->setReadOnly(true);
	auto *copy_url = new QPushButton("Copy example address");
	auto *hint = new QLabel("Only this computer can connect (127.0.0.1), and every request must carry the secret "
				"token. Leave this off unless you use it. Toast source: /toast   Assistant: /assistant");
	hint->setWordWrap(true);
	form->addRow(http);
	form->addRow("Port:", port);
	form->addRow("Secret token:", token);
	form->addRow(copy_url);
	form->addRow(hint);
	QObject::connect(copy_url, &QPushButton::clicked, dlg, [=]() {
		QString u = QString("http://127.0.0.1:%1/toast?token=%2&title=Hello&text=It%20works")
				    .arg(port->value())
				    .arg(token->text());
		QApplication::clipboard()->setText(u);
	});
	auto *lay = new QVBoxLayout(dlg);
	lay->addLayout(form);
	lay->addWidget(welcome);
	auto *row = new QHBoxLayout();
	auto *ok = new QPushButton("Save");
	auto *cancel = new QPushButton("Cancel");
	row->addStretch(1);
	row->addWidget(ok);
	row->addWidget(cancel);
	lay->addLayout(row);

	QObject::connect(ok, &QPushButton::clicked, dlg, [=]() {
		struct wf_prefs *p = wf_prefs_get();
		strncpy(p->default_style, style->currentData().toString().toUtf8().constData(),
			sizeof(p->default_style) - 1);
		strncpy(p->update_url, update->text().trimmed().toUtf8().constData(), sizeof(p->update_url) - 1);
		strncpy(p->download_url, download->text().trimmed().toUtf8().constData(), sizeof(p->download_url) - 1);
		p->welcome_shown = !welcome->isChecked();
		p->http_enabled = http->isChecked();
		p->http_port = port->value();
		wf_prefs_save();
		wf_notify_http_apply_prefs();
		dlg->accept();
	});
	QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
	dlg->show();
}

/* --------------------------------------------------------------- download */

extern "C" void wf_ui_show_download_dialog(void)
{
	QDialog *dlg = new QDialog(main_window());
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->setWindowTitle("Camera Frame - Download Presets");
	dlg->resize(560, 0);

	auto *lay = new QVBoxLayout(dlg);
	auto *info = new QLabel("Enter the https:// address of a preset file, or of an index file listing several "
				"presets. Only preset data is imported; no code is downloaded or run.");
	info->setWordWrap(true);
	lay->addWidget(info);
	auto *url = new QLineEdit(QString::fromUtf8(wf_prefs_get()->download_url));
	url->setPlaceholderText("https://raw.githubusercontent.com/.../presets.json");
	lay->addWidget(url);
	auto *status = new QLabel();
	status->setWordWrap(true);
	lay->addWidget(status);
	auto *row = new QHBoxLayout();
	auto *go = new QPushButton("Download");
	auto *close = new QPushButton("Close");
	row->addStretch(1);
	row->addWidget(go);
	row->addWidget(close);
	lay->addLayout(row);

	QPointer<QLabel> pstatus(status);
	QPointer<QPushButton> pgo(go);
	QObject::connect(go, &QPushButton::clicked, dlg, [url, status, go, pstatus, pgo]() {
		std::string u = url->text().trimmed().toStdString();
		go->setEnabled(false);
		status->setText("Downloading...");
		std::thread([u, pstatus, pgo]() {
			char msg[256] = {0};
			int r = wf_download_presets(u.c_str(), msg, sizeof(msg));
			QString m = QString::fromUtf8(msg);
			QMetaObject::invokeMethod(
				qApp,
				[u, r, m, pstatus, pgo]() {
					if (pstatus)
						pstatus->setText(m);
					if (pgo)
						pgo->setEnabled(true);
					if (r > 0) {
						strncpy(wf_prefs_get()->download_url, u.c_str(),
							sizeof(wf_prefs_get()->download_url) - 1);
						wf_prefs_save();
					}
				},
				Qt::QueuedConnection);
		}).detach();
	});
	QObject::connect(close, &QPushButton::clicked, dlg, &QDialog::accept);
	dlg->show();
}

/* ---------------------------------------------------------------- updates */

extern "C" void wf_ui_check_updates(void)
{
	wf_ui_status("Checking for updates...");
	std::thread([]() {
		char msg[300] = {0};
		char url[512] = {0};
		int r = wf_check_update(msg, sizeof(msg), url, sizeof(url));
		QString m = QString::fromUtf8(msg);
		QString u = QString::fromUtf8(url);
		QMetaObject::invokeMethod(
			qApp,
			[r, m, u]() {
				QWidget *w = main_window();
				if (r == 1 && !u.isEmpty()) {
					if (QMessageBox::question(w, "Windows Camera Frame", m + "\n\nOpen the download page?") ==
					    QMessageBox::Yes)
						QDesktopServices::openUrl(QUrl(u));
				} else {
					QMessageBox::information(w, "Windows Camera Frame", m);
				}
			},
			Qt::QueuedConnection);
	}).detach();
}
