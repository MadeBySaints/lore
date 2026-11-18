#include "toolbox.h"
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <QCoreApplication>
#include <QProcessEnvironment>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const QStringList ids{"claude", "codex"};
QString label(const QString &id) { return id == "claude" ? "Claude Code" : "Codex"; }
QString findTool(const QString &id) {
  QString path = QStandardPaths::findExecutable(id);
  if (!path.isEmpty()) return path;
  QStringList folders{QDir::homePath() + "/.local/bin", QDir::homePath() + "/.bun/bin"};
#ifdef Q_OS_WIN
  folders << qEnvironmentVariable("APPDATA") + "/npm";
#endif
  return QStandardPaths::findExecutable(id, folders);
}
}

ToolCommand Toolbox::resolve(const QString &id, const QString &path) {
  if (!ids.contains(id) || path.isEmpty() || !QFileInfo(path).isFile()) return {};
#ifdef Q_OS_WIN
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == "cmd" || suffix == "bat") {
    const QString script = QFileInfo(path).absolutePath() + (id == "codex" ? "/node_modules/@openai/codex/bin/codex.js" : "/node_modules/@anthropic-ai/claude-code/cli.js");
    QString node = QStandardPaths::findExecutable("node.exe");
    if (node.isEmpty()) node = QStandardPaths::findExecutable("node.exe", {qEnvironmentVariable("ProgramFiles") + "/nodejs", qEnvironmentVariable("LOCALAPPDATA") + "/Programs/nodejs", QDir::homePath() + "/.volta/bin"});
    if (node.isEmpty() || !QFileInfo(script).isFile()) return {};
    return {node, {script}, path};
  }
  if (suffix != "exe") return {};
#else
  if (!QFileInfo(path).isExecutable()) return {};
#endif
  return {path, {}, path};
}
Toolbox::Toolbox(QObject *parent) : QObject(parent) {
  m_timeout.setSingleShot(true); m_timeout.setInterval(8000);
  connect(&m_timeout, &QTimer::timeout, &m_probe, &QProcess::kill);
  connect(&m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || !m_checking) return;
    m_timeout.stop(); m_states[ids[m_index++]] = "error"; next();
  });
  connect(&m_probe, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
    m_timeout.stop(); if (!m_checking) return;
    const QString id = ids[m_index];
    bool ready = code == 0 && status == QProcess::NormalExit;
    if (ready && id == "claude") ready = QJsonDocument::fromJson(m_probe.readAllStandardOutput()).object().value("loggedIn").toBool();
    m_states[id] = ready ? "ready" : status == QProcess::NormalExit ? "signin" : "error";
    ++m_index; next();
  });
}
Toolbox::~Toolbox() { m_checking = false; m_timeout.stop(); disconnect(&m_probe, nullptr, this, nullptr); if (m_probe.state() != QProcess::NotRunning) { m_probe.kill(); m_probe.waitForFinished(2000); } }
ToolCommand Toolbox::command(const QString &id) const { return resolve(id, m_overrides.value(id).toString().isEmpty() ? findTool(id) : m_overrides.value(id).toString()); }
QVariantList Toolbox::entries() const {
  QVariantList result;
  for (const auto &id : ids) {
    const auto tool = command(id); const QString state = tool.available() ? m_states.value(id, "unchecked") : "missing";
    const QString detail = state == "ready" ? tr("Installed and signed in") : state == "checking" ? tr("Checking sign-in…") : state == "signin" ? tr("Sign in to use this tool") : state == "missing" ? tr("Install this tool, or locate an existing copy") : state == "error" ? tr("Check failed. Try again or locate the program.") : tr("Installed. Check sign-in before learning.");
    result.append(QVariantMap{{"id", id}, {"name", label(id)}, {"available", tool.available()}, {"state", state}, {"detail", detail}, {"path", tool.path}});
  }
  return result;
}
void Toolbox::setOverrides(const QVariantMap &paths) { m_overrides = paths; m_states.clear(); emit changed(); }
void Toolbox::setExecutable(const QString &id, const QString &path) {
  if (!ids.contains(id) || checking()) return;
  if (!resolve(id, path).available()) { emit notice(tr("Choose the %1 executable. For an npm install, choose its standard command wrapper.").arg(label(id)), true); return; }
  m_overrides[id] = path; m_states.remove(id); emit settingsChanged(); check();
}
void Toolbox::check() { if (m_checking) return; m_checking = true; m_index = 0; next(); }
void Toolbox::next() {
  while (m_index < ids.size() && !command(ids[m_index]).available()) ++m_index;
  if (m_index == ids.size()) { m_checking = false; emit changed(); return; }
  const QString id = ids[m_index]; const auto tool = command(id);
  m_states[id] = "checking"; emit changed();
  m_probe.start(tool.program, tool.arguments + (id == "claude" ? QStringList{"auth", "status", "--json"} : QStringList{"login", "status"}));
  m_probe.closeWriteChannel(); m_timeout.start();
}
void Toolbox::install(const QString &id) {
  if (!ids.contains(id)) return;
  QDesktopServices::openUrl(QUrl(id == "claude" ? "https://code.claude.com/docs/en/setup" : "https://developers.openai.com/codex/cli/"));
  emit notice(tr("The official setup page opened. Install %1, then click Check tools.").arg(label(id)), false);
}
void Toolbox::signIn(const QString &id) { terminal(id, id == "claude" ? QStringList{"auth", "login"} : QStringList{"login"}); }
void Toolbox::startSession(const QString &id, const QString &project) { if (!project.isEmpty()) terminal(id, {}, project); }
void Toolbox::terminal(const QString &id, const QStringList &arguments, const QString &project) {
  const auto tool = command(id);
  if (!tool.available()) { emit notice(tr("Install %1 or locate its executable first.").arg(label(id)), true); return; }
  QProcess process;
  process.setWorkingDirectory(project.isEmpty() ? QDir::homePath() : project);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("PATH", QCoreApplication::applicationDirPath() + QDir::listSeparator() + environment.value("PATH"));
  process.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
  const auto quoted = [](QString s) { s.replace("'", "''"); return "'" + s + "'"; };
  QString script = "& " + quoted(tool.program);
  for (const auto &arg : tool.arguments + arguments) script += ' ' + quoted(arg);
  const QByteArray encoded(reinterpret_cast<const char *>(script.utf16()), script.size() * 2);
  process.setProgram(QStandardPaths::findExecutable("powershell.exe"));
  process.setArguments({"-NoLogo", "-NoProfile", "-NoExit", "-EncodedCommand", QString::fromLatin1(encoded.toBase64())});
  process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
    args->flags |= CREATE_NEW_CONSOLE; args->flags &= ~DETACHED_PROCESS;
    args->startupInfo->dwFlags &= ~STARTF_USESTDHANDLES;
  });
#else
  QString terminal;
  for (const auto &candidate : {"ghostty", "kitty", "alacritty", "foot", "gnome-terminal", "x-terminal-emulator"}) {
    terminal = QStandardPaths::findExecutable(candidate); if (!terminal.isEmpty()) break;
  }
  if (terminal.isEmpty()) { emit notice(tr("No terminal app was found. Install a terminal, then try again."), true); return; }
  process.setProgram(terminal);
  process.setArguments(QStringList{QFileInfo(terminal).fileName() == "gnome-terminal" ? "--" : "-e", tool.program} + tool.arguments + arguments);
#endif
  if (!process.startDetached()) { emit notice(tr("The coding window could not be opened. Check that your terminal is installed."), true); return; }
  emit notice(arguments.isEmpty() ? tr("%1 opened in this project. Its conversations will be captured automatically.").arg(label(id)) : tr("Finish signing in in the opened window, then click Check tools again."), false);
}
