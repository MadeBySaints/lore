#include "toolbox.h"
#include <QRegularExpression>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>
#include <memory>
#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
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
  m_network.setProxy(QNetworkProxy::NoProxy);
  // Ollama may start after Lore: look again now and then.
  m_rescan.setInterval(qEnvironmentVariableIntValue("LORE_LLM_RESCAN_MS") > 0 ? qEnvironmentVariableIntValue("LORE_LLM_RESCAN_MS") : 180000);
  connect(&m_rescan, &QTimer::timeout, this, &Toolbox::rescanLocal);
  m_rescan.start();
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
    result.append(QVariantMap{{"id", id}, {"name", label(id)}, {"available", tool.available()}, {"state", state}, {"detail", detail}, {"path", tool.path}, {"agentModel", agentModel(id)}, {"agentEffort", agentEffort(id)}});
  }
  const QString detail = m_localState == "ready" ? tr("Running · %1 · %n model(s) found on this computer. Prompts stay here.", "", int(m_localModels.size())).arg(localModel())
    : m_localState == "nomodel" ? (m_localModels.isEmpty() ? tr("Ollama is running, but it has no chat model. Run: ollama pull qwen3:8b") : tr("%n model(s) found on this computer. Choose the default below.", "", int(m_localModels.size())))
    : m_localState == "checking" ? tr("Looking for Ollama…")
    : m_localState == "missing" ? tr("Ollama is not running. Start it, then click Find models.")
    : m_localState == "invalid" ? tr("Use a local address such as http://127.0.0.1:11434.")
    : tr("Free and private. Click Find models to look for Ollama.");
  QVariantList servers;
  for (const auto &server : m_servers) {
    QVariantList models;
    for (const auto &m : server.models) models.append(QVariantMap{{"name", m}});
    servers.append(QVariantMap{{"url", server.url}, {"kind", QStringLiteral("Ollama")}, {"models", models}});
  }
  result.append(QVariantMap{{"id", "local"}, {"name", tr("Local model")}, {"available", localReachable()}, {"state", m_localState}, {"detail", detail}, {"path", QString()},
    {"url", localUrl()}, {"model", localModel()}, {"models", m_localModels}, {"servers", servers}, {"backup", localBackup()}, {"backupProvider", backupProvider()}});
  return result;
}
void Toolbox::setOverrides(const QVariantMap &paths) {
  const int previousContext = localContextTokens();
  m_overrides = paths; m_states.clear(); if (!m_localChecking) m_localState = "unchecked"; emit changed();
  if (previousContext != localContextTokens()) emit localContextTokensChanged();
}

int Toolbox::localContextTokens() const {
  return qBound(0, m_overrides.value("local.contextTokens").toInt(), 131072);
}

void Toolbox::setLocalContextTokens(int tokens) {
  tokens = qBound(0, tokens, 131072);
  if (tokens == localContextTokens()) return;
  m_overrides["local.contextTokens"] = tokens;
  emit localContextTokensChanged();
  emit settingsChanged();
}
void Toolbox::setExecutable(const QString &id, const QString &path) {
  if (!ids.contains(id) || checking()) return;
  if (!resolve(id, path).available()) { emit notice(tr("Choose the %1 executable. For an npm install, choose its standard command wrapper.").arg(label(id)), true); return; }
  m_overrides[id] = path; m_states.remove(id); emit settingsChanged(); check();
}
void Toolbox::check() { if (checking()) return; m_checking = true; probeLocal(); m_index = 0; next(); }

QUrl Toolbox::loopback(const QString &url) {
  const QUrl base(url.trimmed());
  if (base.scheme() != "http" || !QStringList{"127.0.0.1", "localhost", "::1"}.contains(base.host()) || !base.userInfo().isEmpty()) return {};
  return base;
}
QString Toolbox::localUrl() const {
  const QString saved = m_overrides.value("local.url").toString();
  return !saved.isEmpty() ? saved : qEnvironmentVariable("LORE_LLM_URL", "http://127.0.0.1:11434");
}
QString Toolbox::localModel() const {
  const QString saved = m_overrides.value("local.model").toString();
  return !saved.isEmpty() ? saved : qEnvironmentVariable("LORE_LLM_MODEL");
}
bool Toolbox::validAgentModel(const QString &model) {
  static const QRegularExpression safe("\\A(?:[A-Za-z0-9][A-Za-z0-9._:/\\[\\]-]{0,159})?\\z");
  return safe.match(model).hasMatch();
}
QStringList Toolbox::agentModels(const QString &id) const {
  if (id == "claude") return {"sonnet", "opus", "haiku"};
  if (id != "codex") return {};

  // Use the CLI's last model list without starting Codex or making a request.
  const QString codexHome = qEnvironmentVariable("CODEX_HOME", QDir::homePath() + "/.codex");
  QFile cache(QDir(codexHome).filePath("models_cache.json"));
  QStringList models;
  constexpr qint64 maxCacheBytes = 8 * 1024 * 1024;
  if (cache.open(QIODevice::ReadOnly) && cache.size() <= maxCacheBytes) {
    const auto entries = QJsonDocument::fromJson(cache.read(maxCacheBytes)).object().value("models").toArray();
    for (const auto &entry : entries) {
      const auto model = entry.toObject();
      const QString slug = model.value("slug").toString();
      if (model.value("visibility").toString() == "list" && !slug.isEmpty() && validAgentModel(slug))
        models.append(slug);
    }
  }
  models.removeDuplicates();
  if (models.isEmpty()) return {"gpt-6.2-sol", "gpt-6.2-astra", "gpt-6.1-terra"};
  return models;
}
QStringList Toolbox::effortLevels(const QString &id, const QString &model) const {
  if (id == "claude") return {"low", "medium", "high", "xhigh", "max", "ultracode"};
  if (id == "codex") return {"none", "minimal", "low", "medium", "high", "xhigh", "max", "ultra"};
  if (id == "local" && (model.isEmpty() ? localModel() : model).startsWith("gpt-oss")) return {"low", "medium", "high"};
  return {};
}
void Toolbox::setAgentModel(const QString &id, const QString &model) {
  if (id != "claude" && id != "codex") return;
  const QString value = model.trimmed(); if (!validAgentModel(value) || value == agentModel(id)) return;
  if (value.isEmpty()) m_overrides.remove(id + ".model"); else m_overrides[id + ".model"] = value;
  emit settingsChanged(); emit changed();
}
void Toolbox::setAgentEffort(const QString &id, const QString &effort) {
  if ((id != "claude" && id != "codex") || effort == agentEffort(id) || (!effort.isEmpty() && !effortLevels(id).contains(effort))) return;
  if (effort.isEmpty()) m_overrides.remove(id + ".effort"); else m_overrides[id + ".effort"] = effort;
  emit settingsChanged(); emit changed();
}
QStringList Toolbox::agentArguments(const QString &id) const {
  const QString effort = agentEffort(id);
  return agentArguments(id, agentModel(id), effortLevels(id).contains(effort) ? effort : QString());
}
QStringList Toolbox::agentArguments(const QString &id, const QString &model, const QString &effort) {
  QStringList args;
  if (id == "claude") { if (!model.isEmpty()) args << "--model" << model; if (!effort.isEmpty()) args << "--effort" << effort; }
  if (id == "codex") { if (!model.isEmpty()) args << "-m" << model; if (!effort.isEmpty()) args << "-c" << "model_reasoning_effort=\"" + effort + "\""; }
  return args;
}
void Toolbox::setLocalUrl(const QString &url) {
  QString value = url.trimmed(); while (value.endsWith('/')) value.chop(1);
  if (value == localUrl() || checking()) return;
  if (!loopback(value).isValid()) { m_localState = "invalid"; emit changed(); emit notice(tr("Lore only sends prompts to a model on this computer. Use an address like http://127.0.0.1:11434."), true); return; }
  m_overrides["local.url"] = value; emit settingsChanged(); m_localState = "unchecked"; m_localChecking = true; probeLocal();
}
void Toolbox::setLocalModel(const QString &model) {
  if (model == localModel()) return;
  m_overrides["local.model"] = model; emit settingsChanged();
  if (localReachable()) m_localState = m_localModels.contains(model) ? "ready" : "nomodel";
  emit changed();
}
// The addresses Lore looks at: the one the user added, any in LORE_LLM_URLS,
// and Ollama's own port.
QStringList Toolbox::localCandidates() const {
  QStringList out;
  const auto add = [&out](QString url) { url = url.trimmed(); while (url.endsWith('/')) url.chop(1); if (loopback(url).isValid() && !out.contains(url)) out << url; };
  add(localUrl());
  for (const auto &url : qEnvironmentVariable("LORE_LLM_URLS").split(',', Qt::SkipEmptyParts)) add(url);
  if (qEnvironmentVariable("LORE_LLM_DISCOVER") != "0") add("http://127.0.0.1:11434");
  return out;
}
QString Toolbox::chatServer(const QString &model) const {
  for (const auto &s : m_servers) if (s.models.contains(model)) return s.url;
  return localScanned() ? QString() : localUrl();
}
void Toolbox::setLocalBackup(const QString &backup) {
  if (!QStringList{"auto", "claude", "codex", "none"}.contains(backup) || backup == localBackup()) return;
  m_overrides["local.backup"] = backup; emit settingsChanged(); emit changed();
}
QString Toolbox::backupProvider() const {
  const QString choice = localBackup();
  if (choice == "none") return {};
  if (choice == "claude" || choice == "codex") return ready(choice) ? choice : QString();
  return ready("claude") ? QStringLiteral("claude") : ready("codex") ? QStringLiteral("codex") : QString();
}
void Toolbox::rescanLocal() { if (checking()) return; m_quietScan = true; probeLocal(); }
// Every candidate is asked at once for Ollama's model list (/api/tags); a
// server that does not answer it is not used.
void Toolbox::probeLocal() {
  const QStringList candidates = localCandidates();
  if (candidates.isEmpty()) { m_localState = "invalid"; m_localChecking = false; m_quietScan = false; m_servers.clear(); m_localModels.clear(); emit changed(); return; }
  m_localChecking = true;
  if (!m_quietScan) { m_localState = "checking"; emit changed(); }
  struct Scan { QList<LocalServer> found; QList<bool> answered; int pending = 0; };
  auto scan = std::make_shared<Scan>();
  scan->found.resize(candidates.size()); scan->answered.fill(false, candidates.size()); scan->pending = int(candidates.size());
  const auto request = [](const QUrl &url) {
    QNetworkRequest r(url); r.setTransferTimeout(3000); r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return r;
  };
  const auto done = [this, scan] {
    if (--scan->pending > 0) return;
    QList<LocalServer> servers; QStringList models;
    for (int i = 0; i < scan->found.size(); ++i) if (scan->answered[i]) { servers << scan->found[i]; for (const auto &m : scan->found[i].models) if (!models.contains(m)) models << m; }
    m_servers = servers; m_localModels = models; m_localChecking = false; m_quietScan = false;
    m_localState = servers.isEmpty() ? "missing" : models.contains(localModel()) ? "ready" : "nomodel";
    emit changed();
  };
  for (int i = 0; i < candidates.size(); ++i) {
    const QUrl base(candidates[i]);
    scan->found[i].url = candidates[i];
    QNetworkReply *tags = m_network.get(request(base.resolved(QUrl("/api/tags"))));
    connect(tags, &QNetworkReply::finished, this, [tags, scan, i, done] {
      tags->deleteLater();
      const auto o = QJsonDocument::fromJson(tags->readAll()).object();
      if (tags->error() == QNetworkReply::NoError && o.value("models").isArray()) {
        auto &server = scan->found[i]; scan->answered[i] = true;
        for (const auto &m : o.value("models").toArray()) { const QString name = m.toObject().value("name").toString(); if (!name.isEmpty() && !name.contains("embed", Qt::CaseInsensitive)) server.models << name; }
      }
      done();
    });
  }
}
void Toolbox::next() {
  while (m_index < ids.size() && !command(ids[m_index]).available()) ++m_index;
  if (m_index == ids.size()) { m_checking = false; emit changed(); return; }
  const QString id = ids[m_index]; const auto tool = command(id);
  m_states[id] = "checking"; emit changed();
  m_probe.start(tool.program, tool.arguments + (id == "claude" ? QStringList{"auth", "status", "--json"} : QStringList{"login", "status"}));
  m_probe.closeWriteChannel(); m_timeout.start();
}
void Toolbox::install(const QString &id) {
  if (id == "local") { QDesktopServices::openUrl(QUrl("https://ollama.com/download")); emit notice(tr("Install and open Ollama, run: ollama pull qwen3:8b, then click Check tools."), false); return; }
  if (!ids.contains(id)) return;
  QDesktopServices::openUrl(QUrl(id == "claude" ? "https://code.claude.com/docs/en/setup" : "https://developers.openai.com/codex/cli/"));
  emit notice(tr("The official setup page opened. Install %1, then click Check tools.").arg(label(id)), false);
}
void Toolbox::signIn(const QString &id) { terminal(id, id == "claude" ? QStringList{"auth", "login"} : QStringList{"login"}); }
void Toolbox::startSession(const QString &id, const QString &project) { if (!project.isEmpty()) terminal(id, {}, project); }
void Toolbox::startSessionWith(const QString &id, const QString &project, const QString &prompt, const QStringList &options) {
  if (!project.isEmpty() && (id == "claude" || id == "codex"))
    terminal(id, options + (prompt.trimmed().isEmpty() ? QStringList() : QStringList{"--", prompt.trimmed()}), project);
}
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
