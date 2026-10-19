/*
 * ScriptConsole.cpp - Python scripting window (editor, output and interactive prompt)
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "ScriptConsole.h"

#include <climits>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>

#include "ConfigManager.h"
#include "GuiApplication.h"
#include "MainWindow.h"
#include "ScriptApi.h"
#include "ScriptRunner.h"
#include "ScriptServer.h"
#include "SubWindow.h"
#include "embed.h"

namespace lmms::gui
{

namespace
{

const char* const DefaultScript = R"(# LMMS Python console - Ctrl+Enter runs this script (or the selection).
# Everything runs in one interpreter, so variables survive between runs.
# Type help(lmms) or lmms.help() at the prompt below for the API.
import lmms

song = lmms.song
song.tempo = 120

lead = song.add_instrument_track("tripleoscillator", name="Script lead")
clip = lead.add_midi_clip(start=0, length=8)
for i, pitch in enumerate(lmms.scale("C4", "minor_pentatonic", count=8)):
    clip.add_note(pitch, start=i * 0.5, length=0.5, velocity=90)

print(lead, clip, len(clip.notes), "notes")
)";

class PythonHighlighter : public QSyntaxHighlighter
{
public:
	explicit PythonHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc)
	{
		auto format = [](const QColor& color, bool bold = false, bool italic = false) {
			QTextCharFormat f;
			f.setForeground(color);
			if (bold) { f.setFontWeight(QFont::Bold); }
			f.setFontItalic(italic);
			return f;
		};
		const QStringList keywords{"and", "as", "assert", "async", "await", "break", "class", "continue", "def",
			"del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
			"lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with", "yield"};
		m_rules.push_back({QRegularExpression("\\b(" + keywords.join('|') + ")\\b"), format(QColor(0xc6, 0x92, 0xff), true)});
		m_rules.push_back({QRegularExpression("\\b(True|False|None|self)\\b"), format(QColor(0xff, 0xa6, 0x57))});
		m_rules.push_back({QRegularExpression("\\b(print|len|range|enumerate|zip|list|dict|int|float|str|min|max|"
			"abs|round|sorted|help|dir|isinstance)\\b"), format(QColor(0x6c, 0xc7, 0xff))});
		m_rules.push_back({QRegularExpression("\\blmms\\b"), format(QColor(0x5f, 0xd3, 0x8d), true)});
		m_rules.push_back({QRegularExpression("\\b[0-9]+(\\.[0-9]*)?\\b"), format(QColor(0xff, 0xcf, 0x6b))});
		m_rules.push_back({QRegularExpression("@[A-Za-z_][A-Za-z0-9_.]*"), format(QColor(0x6c, 0xc7, 0xff))});
		m_rules.push_back({QRegularExpression(R"("[^"\\]*(\\.[^"\\]*)*"|'[^'\\]*(\\.[^'\\]*)*')"), format(QColor(0x9a, 0xd8, 0x7a))});
		m_rules.push_back({QRegularExpression("#[^\\n]*"), format(QColor(0x8a, 0x93, 0x9e), false, true)});
	}

protected:
	void highlightBlock(const QString& text) override
	{
		for (const auto& rule : m_rules)
		{
			auto it = rule.pattern.globalMatch(text);
			while (it.hasNext())
			{
				const auto m = it.next();
				setFormat(m.capturedStart(), m.capturedLength(), rule.format);
			}
		}
	}

private:
	struct Rule
	{
		QRegularExpression pattern;
		QTextCharFormat format;
	};
	std::vector<Rule> m_rules;
};

QFont monospaceFont()
{
	QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	if (QFontInfo(font).fixedPitch()) { return font; }
	for (const auto& family : {"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono", "Consolas", "Menlo", "Courier New"})
	{
		font = QFont(family);
		if (QFontInfo(font).fixedPitch()) { return font; }
	}
	font = QFont("monospace");
	font.setStyleHint(QFont::TypeWriter);
	font.setFixedPitch(true);
	return font;
}

QToolButton* makeButton(const QString& text, const QString& toolTip, QWidget* parent)
{
	auto button = new QToolButton(parent);
	button->setText(text);
	button->setToolTip(toolTip);
	button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	button->setAutoRaise(true);
	button->setFocusPolicy(Qt::NoFocus);
	return button;
}

} // namespace




ScriptConsole::ScriptConsole(QWidget* parent) :
	QWidget(parent),
	m_runner(nullptr)
{
	setWindowTitle(tr("Script Console"));
	setWindowIcon(embed::getIconPixmap("script"));
	setObjectName("scriptConsole");

	const QFont mono = monospaceFont();

	// toolbar
	auto toolbar = new QHBoxLayout;
	toolbar->setContentsMargins(4, 4, 4, 0);
	toolbar->setSpacing(2);
	m_runButton = makeButton(tr("▶ Run"), tr("Run the script, or the selected lines (Ctrl+Enter)"), this);
	m_stopButton = makeButton(tr("■ Stop"), tr("Stop the running code (this also resets the interpreter)"), this);
	auto restartButton = makeButton(tr("Reset"), tr("Start a fresh Python interpreter"), this);
	m_undoRunButton = makeButton(tr("↶ Undo run"), tr("Put the project back to how it was before the last run"), this);
	auto newButton = makeButton(tr("New"), tr("Start a new script"), this);
	auto openButton = makeButton(tr("Open…"), tr("Open a Python script"), this);
	auto saveButton = makeButton(tr("Save…"), tr("Save the script (Ctrl+S)"), this);
	auto examplesButton = makeButton(tr("Examples ▾"), tr("Load an example script"), this);
	auto clearButton = makeButton(tr("Clear"), tr("Clear the output"), this);
	auto connectButton = makeButton(tr("Agent link"), tr("Copy Python code that connects an external program "
		"(e.g. an AI agent) to this LMMS instance"), this);
	for (auto b : {m_runButton, m_stopButton, restartButton, m_undoRunButton}) { toolbar->addWidget(b); }
	toolbar->addSpacing(10);
	for (auto b : {newButton, openButton, saveButton, examplesButton}) { toolbar->addWidget(b); }
	toolbar->addStretch(1);
	toolbar->addWidget(connectButton);
	toolbar->addWidget(clearButton);

	connect(m_runButton, &QToolButton::clicked, this, &ScriptConsole::run);
	connect(m_stopButton, &QToolButton::clicked, this, &ScriptConsole::stop);
	connect(restartButton, &QToolButton::clicked, this, &ScriptConsole::restart);
	connect(m_undoRunButton, &QToolButton::clicked, this, &ScriptConsole::undoRun);
	connect(newButton, &QToolButton::clicked, this, [this] {
		m_fileName.clear();
		setScript("import lmms\n\n");
		updateTitle();
	});
	connect(openButton, &QToolButton::clicked, this, &ScriptConsole::openScript);
	connect(saveButton, &QToolButton::clicked, this, [this] { saveScript(true); });
	connect(examplesButton, &QToolButton::clicked, this, &ScriptConsole::showExamples);
	connect(clearButton, &QToolButton::clicked, this, &ScriptConsole::clearOutput);
	connect(connectButton, &QToolButton::clicked, this, &ScriptConsole::copyConnectionSnippet);

	// editor
	m_editor = new QPlainTextEdit(this);
	m_editor->setFont(mono);
	m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_editor->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(' ') * 4);
	m_editor->installEventFilter(this);
	m_editor->setPlaceholderText(tr("Python code using the lmms module…"));
	new PythonHighlighter(m_editor->document());

	// output + prompt
	m_output = new QPlainTextEdit(this);
	m_output->setFont(mono);
	m_output->setReadOnly(true);
	m_output->setMaximumBlockCount(5000);
	m_output->setObjectName("scriptConsoleOutput");

	auto promptRow = new QWidget(this);
	auto promptLayout = new QHBoxLayout(promptRow);
	promptLayout->setContentsMargins(0, 0, 0, 0);
	promptLayout->setSpacing(4);
	auto promptLabel = new QLabel(">>>", promptRow);
	promptLabel->setFont(mono);
	m_prompt = new QLineEdit(promptRow);
	m_prompt->setFont(mono);
	m_prompt->setPlaceholderText(tr("Type Python and press Enter, e.g. lmms.song.tempo = 140"));
	m_prompt->installEventFilter(this);
	promptLayout->addWidget(promptLabel);
	promptLayout->addWidget(m_prompt, 1);

	auto bottom = new QWidget(this);
	auto bottomLayout = new QVBoxLayout(bottom);
	bottomLayout->setContentsMargins(0, 0, 0, 0);
	bottomLayout->setSpacing(2);
	bottomLayout->addWidget(m_output, 1);
	bottomLayout->addWidget(promptRow);

	auto splitter = new QSplitter(Qt::Vertical, this);
	splitter->addWidget(m_editor);
	splitter->addWidget(bottom);
	splitter->setStretchFactor(0, 3);
	splitter->setStretchFactor(1, 2);

	m_status = new QLabel(this);
	m_status->setContentsMargins(6, 0, 6, 2);

	auto layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	layout->addLayout(toolbar);
	layout->addWidget(splitter, 1);
	layout->addWidget(m_status);

	connect(m_prompt, &QLineEdit::returnPressed, this, [this] {
		const QString line = m_prompt->text();
		if (line.trimmed().isEmpty()) { return; }
		if (m_history.isEmpty() || m_history.last() != line) { m_history << line; }
		m_historyPos = m_history.size();
		m_prompt->clear();
		runCode(line, true);
	});

	createRunner();
	connect(scripting::ScriptApi::instance(), &scripting::ScriptApi::messagePosted, this, [this](const QString& text) {
		appendOutput(text + "\n", Style::Info);
	});
	connect(scripting::ScriptServer::instance(), &scripting::ScriptServer::clientCountChanged,
		this, &ScriptConsole::updateState);

	const QString lastFile = ConfigManager::inst()->value("scripting", "lastfile");
	if (lastFile.isEmpty() || !openFile(lastFile)) { setScript(DefaultScript); }

	auto subWindow = getGUI()->mainWindow()->addWindowedWidget(this);
	subWindow->setAttribute(Qt::WA_DeleteOnClose, false);
	subWindow->resize(760, 600);
	subWindow->hide();

	appendOutput(tr("LMMS Python console. Ctrl+Enter runs the editor; the prompt below runs single lines.\n"
		"API reference: doc/SCRIPTING.md, or lmms.help() in the prompt.\n"), Style::Info);
	updateState();
	updateTitle();
}




ScriptConsole::~ScriptConsole() = default;




void ScriptConsole::attachRunner(scripting::ScriptRunner* runner)
{
	++m_externalScripts;
	appendOutput(tr("[running a script from the command line]") + "\n", Style::Info);
	connect(runner, &scripting::ScriptRunner::output, this, [this](const QString& text, bool isError) {
		appendOutput(text, isError ? Style::Error : Style::Normal);
	});
	connect(runner, &scripting::ScriptRunner::finished, this, [this](int exitCode) {
		--m_externalScripts;
		appendOutput(tr("[command-line script finished with exit code %1]").arg(exitCode) + "\n", Style::Info);
		updateState();
	});
	updateState();
}




void ScriptConsole::setScript(const QString& code)
{
	m_editor->setPlainText(code);
}




QString ScriptConsole::script() const
{
	return m_editor->toPlainText();
}




bool ScriptConsole::openFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { return false; }
	setScript(QString::fromUtf8(file.readAll()));
	m_fileName = path;
	updateTitle();
	return true;
}




void ScriptConsole::run()
{
	QString code = m_editor->textCursor().selectedText();
	if (!code.isEmpty())
	{
		// QTextCursor uses the paragraph separator for line breaks
		code.replace(QChar::ParagraphSeparator, '\n');
		// run partial selections that start indented
		const auto lines = code.split('\n');
		static const QRegularExpression nonSpace("\\S");
		int indent = INT_MAX;
		for (const auto& line : lines)
		{
			const int first = static_cast<int>(line.indexOf(nonSpace));
			if (first >= 0) { indent = std::min(indent, first); }
		}
		if (indent > 0 && indent != INT_MAX)
		{
			QStringList dedented;
			for (const auto& line : lines) { dedented << line.mid(indent); }
			code = dedented.join('\n');
		}
	}
	else { code = m_editor->toPlainText(); }

	const QString name = m_fileName.isEmpty() ? tr("script") : QFileInfo(m_fileName).fileName();
	// A script can change a lot at once; keep a copy of the project so the whole run can be undone
	if (!m_runner->isBusy())
	{
		try { m_runCheckpoint = scripting::ScriptApi::instance()->createCheckpoint(); }
		catch (const std::exception& e)
		{
			m_runCheckpoint = 0;
			appendOutput(tr("(this run can't be undone: %1)").arg(QString::fromUtf8(e.what())) + "\n", Style::Error);
		}
	}
	appendOutput(tr("▶ running %1").arg(name) + "\n", Style::Info);
	runCode(code, false);
	updateState();
}




void ScriptConsole::undoRun()
{
	auto api = scripting::ScriptApi::instance();
	if (m_runner->isBusy() || m_runCheckpoint == 0 || !api->hasCheckpoint(m_runCheckpoint)) { return; }
	try
	{
		api->restoreCheckpoint(m_runCheckpoint);
		appendOutput(tr("↶ project restored to before the last run (objects held in Python variables are "
			"no longer valid)") + "\n", Style::Info);
	}
	catch (const std::exception& e)
	{
		appendOutput(tr("could not undo the run: %1").arg(QString::fromUtf8(e.what())) + "\n", Style::Error);
	}
	m_runCheckpoint = 0;
	updateState();
}




void ScriptConsole::runCode(const QString& code, bool echo)
{
	if (echo)
	{
		const auto lines = code.split('\n');
		for (int i = 0; i < lines.size(); ++i) { appendOutput((i == 0 ? ">>> " : "... ") + lines[i] + "\n", Style::Echo); }
	}
	m_runner->execute(code);
	updateState();
}




void ScriptConsole::createRunner()
{
	m_runner = new scripting::ScriptRunner(this);
	connect(m_runner, &scripting::ScriptRunner::output, this, [this](const QString& text, bool isError) {
		appendOutput(text, isError ? Style::Error : Style::Normal);
	});
	connect(m_runner, &scripting::ScriptRunner::executionFinished, this, [this](bool) { updateState(); });
	connect(m_runner, &scripting::ScriptRunner::started, this, &ScriptConsole::updateState);
	connect(m_runner, &scripting::ScriptRunner::finished, this, [this](int) {
		appendOutput(tr("[interpreter stopped]") + "\n", Style::Info);
		updateState();
	});
}




void ScriptConsole::stop()
{
	if (m_runner->isRunning()) { m_runner->stop(); }
}




void ScriptConsole::restart()
{
	if (m_runner->isRunning())
	{
		// a new runner, so late signals from the dying process can't confuse the new one
		m_runner->disconnect(this);
		m_runner->stop();
		m_runner->deleteLater();
		createRunner();
	}
	appendOutput(tr("[fresh interpreter]") + "\n", Style::Info);
	m_runner->startConsole();
	updateState();
}




void ScriptConsole::clearOutput()
{
	m_output->clear();
}




void ScriptConsole::appendOutput(const QString& text, Style style)
{
	if (text.isEmpty()) { return; }
	QTextCharFormat format;
	switch (style)
	{
	case Style::Error: format.setForeground(QColor(0xff, 0x7b, 0x72)); break;
	case Style::Echo: format.setForeground(QColor(0x8a, 0xb4, 0xf8)); break;
	case Style::Info: format.setForeground(QColor(0x8a, 0x93, 0x9e)); format.setFontItalic(true); break;
	case Style::Normal: format.setForeground(m_output->palette().color(QPalette::Text)); break;
	}
	auto bar = m_output->verticalScrollBar();
	const bool atBottom = bar->value() >= bar->maximum() - 4;
	QTextCursor cursor(m_output->document());
	cursor.movePosition(QTextCursor::End);
	cursor.insertText(text, format);
	if (atBottom) { bar->setValue(bar->maximum()); }
}




bool ScriptConsole::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() != QEvent::KeyPress) { return QWidget::eventFilter(watched, event); }
	auto ke = static_cast<QKeyEvent*>(event);
	const bool ctrl = ke->modifiers() & Qt::ControlModifier;

	if (watched == m_editor)
	{
		if (ctrl && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)) { run(); return true; }
		if (ctrl && ke->key() == Qt::Key_S) { saveScript(m_fileName.isEmpty()); return true; }
		if (ke->key() == Qt::Key_Tab && !m_editor->textCursor().hasSelection())
		{
			m_editor->insertPlainText("    ");
			return true;
		}
		if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && ke->modifiers() == Qt::NoModifier)
		{
			// keep the indentation of the current line, one level deeper after ':'
			const QString line = m_editor->textCursor().block().text().left(m_editor->textCursor().positionInBlock());
			QString indent = line.left(line.indexOf(QRegularExpression("\\S")) < 0 ? line.size()
				: line.indexOf(QRegularExpression("\\S")));
			if (line.trimmed().endsWith(':')) { indent += "    "; }
			m_editor->insertPlainText("\n" + indent);
			m_editor->ensureCursorVisible();
			return true;
		}
	}
	else if (watched == m_prompt)
	{
		if (ke->key() == Qt::Key_Up && !m_history.isEmpty())
		{
			m_historyPos = std::max(0, m_historyPos - 1);
			m_prompt->setText(m_history.value(m_historyPos));
			return true;
		}
		if (ke->key() == Qt::Key_Down && !m_history.isEmpty())
		{
			m_historyPos = std::min(static_cast<int>(m_history.size()), m_historyPos + 1);
			m_prompt->setText(m_history.value(m_historyPos));
			return true;
		}
	}
	return QWidget::eventFilter(watched, event);
}




void ScriptConsole::openScript()
{
	const QString dir = m_fileName.isEmpty() ? ConfigManager::inst()->userProjectsDir() : QFileInfo(m_fileName).absolutePath();
	const QString path = QFileDialog::getOpenFileName(this, tr("Open script"), dir, tr("Python scripts (*.py);;All files (*)"));
	if (path.isEmpty()) { return; }
	if (!openFile(path)) { QMessageBox::warning(this, tr("Open script"), tr("Could not read %1").arg(path)); return; }
	ConfigManager::inst()->setValue("scripting", "lastfile", path);
}




bool ScriptConsole::saveScript(bool askForName)
{
	QString path = m_fileName;
	if (askForName || path.isEmpty())
	{
		const QString dir = path.isEmpty() ? ConfigManager::inst()->userProjectsDir() : QFileInfo(path).absolutePath();
		path = QFileDialog::getSaveFileName(this, tr("Save script"), dir, tr("Python scripts (*.py)"));
		if (path.isEmpty()) { return false; }
		if (!path.endsWith(".py")) { path += ".py"; }
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
	{
		QMessageBox::warning(this, tr("Save script"), tr("Could not write %1").arg(path));
		return false;
	}
	file.write(m_editor->toPlainText().toUtf8());
	m_fileName = path;
	ConfigManager::inst()->setValue("scripting", "lastfile", path);
	updateTitle();
	appendOutput(tr("[saved %1]").arg(path) + "\n", Style::Info);
	return true;
}




void ScriptConsole::updateState()
{
	const bool busy = m_runner->isBusy();
	m_runButton->setEnabled(!busy);
	m_stopButton->setEnabled(m_runner->isRunning());
	m_undoRunButton->setEnabled(!busy && m_runCheckpoint != 0
		&& scripting::ScriptApi::instance()->hasCheckpoint(m_runCheckpoint));

	QStringList parts;
	if (busy) { parts << tr("running…"); }
	else if (m_runner->isRunning()) { parts << tr("Python ready"); }
	else { parts << tr("Python starts on first run (%1)").arg(scripting::ScriptRunner::pythonExecutable()); }
	if (m_externalScripts > 0) { parts << tr("command-line script running"); }
	auto server = scripting::ScriptServer::instance();
	if (server->isListening())
	{
		parts << tr("server 127.0.0.1:%1").arg(server->port());
		parts << tr("%n client(s)", "", server->clientCount());
	}
	m_status->setText(parts.join("  ·  "));
}




void ScriptConsole::updateTitle()
{
	setWindowTitle(m_fileName.isEmpty() ? tr("Script Console")
		: tr("Script Console - %1").arg(QFileInfo(m_fileName).fileName()));
}




void ScriptConsole::showExamples()
{
	QMenu menu(this);
	const QDir dir(scripting::ScriptRunner::examplesDir());
	for (const auto& info : dir.entryInfoList({"*.py"}, QDir::Files, QDir::Name))
	{
		const QString path = info.absoluteFilePath();
		menu.addAction(info.completeBaseName().replace('_', ' '), this, [this, path] {
			openFile(path);
			// examples are read-only templates: saving asks for a new name
			m_fileName.clear();
			updateTitle();
		});
	}
	if (menu.isEmpty()) { menu.addAction(tr("No examples found"))->setEnabled(false); }
	menu.exec(QCursor::pos());
}




void ScriptConsole::copyConnectionSnippet()
{
	auto server = scripting::ScriptServer::instance();
	if (!server->start())
	{
		appendOutput(tr("Could not start the script server.") + "\n", Style::Error);
		return;
	}
	const QString snippet = QString(
		"import sys\n"
		"sys.path.insert(0, r\"%1\")\n"
		"import lmms\n"
		"lmms.connect(port=%2, token=\"%3\")\n")
		.arg(scripting::ScriptRunner::pythonModuleDir()).arg(server->port()).arg(server->token());
	QApplication::clipboard()->setText(snippet);
	appendOutput(tr("Copied to the clipboard - paste this into any Python process to control this LMMS:") + "\n", Style::Info);
	appendOutput(snippet, Style::Echo);
	appendOutput(tr("(Programs can also call lmms.connect() with no arguments: it reads %1)")
		.arg(scripting::ScriptServer::connectionFile()) + "\n", Style::Info);
	updateState();
}


} // namespace lmms::gui
