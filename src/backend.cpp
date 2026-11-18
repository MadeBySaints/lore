#include "backend.h"
#include "paths.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUrl>
#include <QColor>
#include <QDateTime>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QtConcurrent>

Backend::Backend(QObject *parent, FilePicker *picker, const QString &preferences, CapturePaths capturePaths)
  : QObject(parent), m_tools(this), m_picker(picker ? picker : new NativeFilePicker(this)), m_preferences(preferences), m_capturePaths(capturePaths) {
  connect(&m_tools, &Toolbox::notice, this, &Backend::message);
  connect(&m_tools, &Toolbox::settingsChanged, this, &Backend::savePreferences);
  connect(m_picker, &FilePicker::selected, this, [this](const QString &path) {
    const QString purpose = m_pickerPurpose; m_pickerPurpose.clear(); emit statusChanged();
    if (purpose == "project") selectProject(path);
    else if (!purpose.isEmpty()) m_tools.setExecutable(purpose, path);
  });
  connect(m_picker, &FilePicker::cancelled, this, [this] { m_pickerPurpose.clear(); emit statusChanged(); });
  connect(m_picker, &FilePicker::failed, this, [this](const QString &reason) { m_pickerPurpose.clear(); message(reason, true); });
  connect(&m_capture, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    m_capturing = false;
    const bool quiet = m_quietCapture; m_quietCapture = false; quietWorkEnded();
    QTimer::singleShot(0, this, &Backend::runDeferred);
    const auto result = m_capture.result();
    try { refresh(); } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
    if (result.contains("error")) { message(result.value("error").toString(), true); return; }
    if (m_autoLearning && result.value("added").toInt() > 0) m_auto.start();
    if((result.value("added").toInt()>0 || result.value("projectChanged").toBool()) && m_semantic.value("enabled").toBool()) QTimer::singleShot(1200,this,[this] { if(!busy() && !capturing()) work("semantic",{},true); });
    const auto warnings = result.value("warnings").toArray();
    if (!warnings.isEmpty()) message(tr("Some conversations could not be captured: %1").arg(QString::fromUtf8(QJsonDocument(warnings).toJson(QJsonDocument::Compact))), true);
    else if (!busy() && (!quiet || result.value("added").toInt() > 0)) message(m_stats.value("events").toInt() == 0 ? tr("Connected and watching. Start a new coding session in this project to capture your first conversation.") : tr("Capture is up to date. %1 new records saved.").arg(result.value("added").toInt()));
    emit statusChanged();
  });
  m_watch.setInterval(5000);
  connect(&m_watch, &QTimer::timeout, this, [this] { syncNow(true); });
  m_reveal.setSingleShot(true); m_reveal.setInterval(1500);
  connect(&m_reveal, &QTimer::timeout, this, [this] { m_revealed = true; emit statusChanged(); });
  m_auto.setSingleShot(true); m_auto.setInterval(60000);
  connect(&m_auto, &QTimer::timeout, this, [this] {
    if (!m_autoLearning || busy() || m_stats.value("pending").toInt() == 0) return;
    if (!m_tools.ready(m_learningProvider)) { message(tr("Automatic wiki updates are waiting for sign-in. Open Connections to check your tool.")); return; }
    learn(m_learningProvider);
  });
  connect(&m_tools, &Toolbox::changed, this, [this] { if (m_autoLearning && m_tools.ready(m_learningProvider) && m_stats.value("pending").toInt() > 0) m_auto.start(); });
  m_timeout.setSingleShot(true); m_timeout.setInterval(180000);
  connect(&m_timeout, &QTimer::timeout, this, [this] { m_cancelled = true; m_process.kill(); message(tr("Learning timed out. Your captured data is safe.")); });
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { m_output += m_process.readAllStandardOutput(); });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_error += m_process.readAllStandardError(); });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) { m_timeout.stop(); message(m_process.errorString(), true); emit learningFinished(false); }
  });
  connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
    m_timeout.stop(); bool ok = false;
    try {
      if (m_cancelled) { emit statusChanged(); emit learningFinished(false); return; }
      if (code != 0 || state != QProcess::NormalExit) throw std::runtime_error((tr("Learning failed: %1").arg(QString::fromUtf8(m_error).right(1800))).toStdString());
      QString answer;
      if (m_provider == "codex") {
        for (const auto &line : m_output.split('\n')) {
          const auto o = QJsonDocument::fromJson(line).object();
          const auto item = o.value("item").toObject();
          if (o.value("type").toString() == "item.completed" && item.value("type").toString() == "agent_message") answer = item.value("text").toString();
        }
      } else {
        const auto o = QJsonDocument::fromJson(m_output).object();
        if (o.value("is_error").toBool()) throw std::runtime_error(o.value("result").toString().toStdString());
        answer = o.value("result").toString();
      }
      answer = answer.trimmed();
      if (answer.startsWith("```")) { answer = answer.mid(answer.indexOf('\n') + 1); answer = answer.left(answer.lastIndexOf("```")); }
      QJsonParseError parseError; const auto result = QJsonDocument::fromJson(answer.toUtf8(), &parseError);
      if (parseError.error != QJsonParseError::NoError || !result.isObject()) throw std::runtime_error("Model did not return valid memory JSON. No messages were marked learned.");
      if(m_operation=="ask") { m_answer=m_store->saveAnswer(result.object(),m_answerEvidence,m_question).toVariantMap(); refresh(); message(tr("Answer ready. Open its citations to check the evidence.")); }
      else { m_store->learn(result.object(),m_batch,m_provider); refresh(); message(tr("Wiki updated with source references.")); }
      ok = true;
    } catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
    if (ok && m_operation!="ask" && m_autoLearning && m_stats.value("pending").toInt() > 0) m_auto.start();
    if(ok && m_operation!="ask" && m_semantic.value("enabled").toBool()) QTimer::singleShot(0,this,[this] { if(!busy() && !capturing()) work("semantic",{},true); });
    QTimer::singleShot(0, this, &Backend::runDeferred);
    emit statusChanged(); emit learningFinished(ok);
  });
  connect(&m_work, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    const auto result=m_work.result();
    if(m_quietWork) { m_quietWork=false; quietWorkEnded(); }
    QTimer::singleShot(0, this, &Backend::runDeferred);
    if(result.contains("error")) { message(result.value("error").toString(),true); if(m_workKind=="ask") emit learningFinished(false); return; }
    try {
      refresh();
      if(m_workKind=="ask") {
        m_question=result.value("question").toString(); m_answerEvidence=result.value("evidence").toArray();
        if(m_answerEvidence.isEmpty()) {
          m_answer=QVariantMap{{"question",m_question},{"answer",tr("I could not find supporting evidence. Capture conversations or index the project files, then ask a more specific question.")},{"insufficient",true},{"refs",QVariantList()}}; emit changed(); message(tr("No supporting evidence found. No model call was made.")); emit learningFinished(true); return;
        }
        m_operation="ask"; startModel(result.value("prompt").toString(),m_learningProvider);
      } else if(m_workKind=="preview") { m_preview=result.toVariantMap(); emit changed(); message(tr("Context preview ready. This preview has not been sent to an agent.")); }
      else if(m_workKind=="retrieve") { m_results=result.value("evidence").toArray().toVariantList(); emit resultsChanged(); message(tr("Evidence search complete: %1").arg(result.value("retrieval").toString())); if(m_query!=result.value("query").toString()) QTimer::singleShot(0,this,[this] { search(m_query); }); }
      else message(tr("Project intelligence updated."));
    } catch(const std::exception &e) { message(QString::fromUtf8(e.what()),true); if(m_workKind=="ask") emit learningFinished(false); }
    emit statusChanged();
  });
  m_notificationExpiry.setSingleShot(true);
  m_notificationExpiry.setInterval(qMax(100,qEnvironmentVariableIntValue("LORE_NOTIFICATION_TIMEOUT_MS") ? qEnvironmentVariableIntValue("LORE_NOTIFICATION_TIMEOUT_MS") : 12000));
  connect(&m_notificationExpiry,&QTimer::timeout,this,&Backend::clearError);
  restorePreferences();
  if (m_recent.isEmpty() && m_tools.available("claude")) m_learningProvider = "claude";
}
Backend::~Backend() { if(m_cancelWork) *m_cancelWork=true; m_work.waitForFinished(); m_capture.waitForFinished(); if (m_process.state()!=QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(2000); } }
void Backend::message(const QString &text, bool error) {
  m_status=text;
  if(error) {
    m_notices.prepend(QVariantMap{{"text",text},{"time",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
    while(m_notices.size()>30) m_notices.removeLast();
    const bool contention=text.contains("locked",Qt::CaseInsensitive) || text.contains("busy",Qt::CaseInsensitive) || text.contains("Another process",Qt::CaseInsensitive);
    if(contention) m_status=tr("Project memory is busy. Capture will retry automatically.");
    m_errorMessage=m_notificationsEnabled && !contention ? text : QString();
    if(!m_errorMessage.isEmpty()) m_notificationExpiry.start();
  } else { m_errorMessage.clear(); m_notificationExpiry.stop(); }
  emit statusChanged();
}
void Backend::setNotificationsEnabled(bool on) { m_notificationsEnabled=on; if(!on) clearError(); savePreferences(); emit statusChanged(); }
void Backend::clearError() { m_errorMessage.clear(); emit statusChanged(); }
void Backend::refresh() {
  if (!m_store) return;
  m_stats = m_store->stats().toVariantMap(); m_sessions = m_store->sessions().toVariantList();
  m_memories = m_store->memories().toVariantList(); m_brain = m_store->context();
  m_connected = connectionValid();
  m_files=m_store->projectFiles().toVariantList(); m_activity=m_store->activity().toVariantList();
  for(auto &value:m_activity) {
    auto item=value.toMap(); const QString kind=item.value("kind").toString(), body=item.value("body").toString();
    QStringList numbers; auto matches=QRegularExpression("[0-9]+").globalMatch(body); while(matches.hasNext()) numbers.append(matches.next().captured());
    if(kind=="capture" && numbers.size()>=1) item["body"]=tr("Saved %1 new conversation records.").arg(numbers[0]);
    else if(kind=="learn" && numbers.size()>=2) item["body"]=tr("Updated %1 wiki topics from %2 messages.").arg(numbers[0],numbers[1]);
    else if(kind=="index" && numbers.size()>=2) item["body"]=tr("Indexed %1 project files; skipped %2 excluded, binary, or oversized files.").arg(numbers[0],numbers[1]);
    else if(kind=="semantic" && numbers.size()>=2) item["body"]=tr("Updated %1 local embeddings; %2 evidence excerpts indexed.").arg(numbers[0],numbers[1]);
    else if(kind=="answer" && numbers.size()>=1) item["body"]=tr("Answered with %1 cited evidence excerpts.").arg(numbers[0]);
    else if(kind=="review" && numbers.size()>=1) item["body"]=tr("Reviewed memory %1.").arg(numbers[0]);
    value=item;
  }
  m_deliveries=m_store->deliveries().toVariantList(); m_semantic=m_store->semanticStatus().toVariantMap();
  emit changed();
}
bool Backend::openProject(const QString &path, bool create) {
  if (busy() || capturing()) { message(tr("Wait for capture to finish, or cancel learning before switching projects."), true); return false; }
  try {
    QString p = path.trimmed(); if (p.startsWith("~/")) p.replace(0, 1, QDir::homePath());
    auto store = std::make_unique<Store>(p, create); store->render(); m_store = std::move(store);
    m_answer.clear(); m_preview.clear(); m_results.clear();
    const QString savedAnswer=project()+"/.lore/last-answer.json";
    if(QFileInfo::exists(savedAnswer) && !QFileInfo(savedAnswer).isSymLink()) {
      const auto saved=QJsonDocument::fromJson(Store::read(savedAnswer).toUtf8()).object();
      QHash<QString,QString> live; for(const auto &v:m_store->evidence()) { const auto o=v.toObject(); live[o.value("id").toString()]=o.value("hash").toString(); }
      bool valid=true; for(const auto &v:saved.value("refs").toArray()) { const auto o=v.toObject(); if(live.value(o.value("id").toString())!=o.value("hash").toString()) valid=false; }
      if(valid) m_answer=saved.toVariantMap();
    }
    m_selectedProject = project();
    m_auto.stop(); m_autoLearning = false;
    refresh();
    const auto options = m_projectOptions.value(project()).toMap();
    m_learningProvider = options.value("provider", m_learningProvider).toString();
    if (m_connected && options.value("watching", true).toBool()) m_watch.start(); else m_watch.stop();
    if (m_connected && options.value("autoLearning", false).toBool()) { m_autoLearning = true; m_auto.start(); }
    clearError(); savePreferences(); message(tr("Project memory ready.")); return true;
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); return false; }
}
void Backend::setWatching(bool on) { if (on && m_store) m_watch.start(); else { m_watch.stop(); m_auto.stop(); m_autoLearning = false; } savePreferences(); emit statusChanged(); }
void Backend::setAutoLearning(bool on) {
  m_autoLearning = on;
  if (on) { setWatching(true); m_auto.start(); } else m_auto.stop();
  savePreferences(); emit statusChanged();
}
void Backend::sync() { syncNow(false); }
void Backend::syncNow(bool quiet) {
  if (!m_store) return;
  if (capturing() && !quiet && m_quietCapture) { m_quietCapture = false; quietWorkEnded(); message(tr("Capturing saved conversations…")); return; }
  if (capturing() || busy()) { if (!quiet) deferUntilIdle([this] { syncNow(false); }); return; }
  const QString root = project(); const auto paths = m_capturePaths;
  m_capturing = true; m_quietCapture = quiet;
  if (quiet) { quietWorkStarted(); emit statusChanged(); }
  m_capture.setFuture(QtConcurrent::run([root, paths] {
    try { Store store(root); return store.sync(paths.codex, paths.claude); }
    catch (const std::exception &e) { return QJsonObject{{"error", QString::fromUtf8(e.what())}}; }
  }));
  if (!quiet) message(tr("Capturing saved conversations…"));
}
void Backend::quietWorkStarted() { if (!m_revealed && !m_reveal.isActive()) m_reveal.start(); }
void Backend::quietWorkEnded() {
  if ((m_capturing && m_quietCapture) || (m_work.isRunning() && m_quietWork)) return;
  m_reveal.stop(); m_revealed = false;
}
bool Backend::deferUntilIdle(std::function<void()> action) {
  // A click that lands while only background work is running waits for it instead of being dropped.
  if (showBusy() || showCapturing()) return false;
  m_whenIdle = std::move(action); return true;
}
void Backend::runDeferred() {
  if (!m_whenIdle || busy() || capturing()) return;
  auto action = std::move(m_whenIdle); m_whenIdle = nullptr; action();
}
void Backend::search(const QString &text) {
  m_query=text;
  if(text.trimmed().isEmpty()) { m_results.clear(); emit resultsChanged(); return; }
  work("retrieve",text,true);
}

void Backend::connectAgents() {
  if (!m_store) return;
  try { m_store->connectAgents(QCoreApplication::applicationFilePath()); clearError(); refresh(); setWatching(true); savePreferences(); message(tr("Connected. Start a new coding session in this project.")); }
  catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}
QString Backend::readPage(const QString &relative) {
  if (!m_store) return {};
  const QString path = QFileInfo(project() + '/' + relative).canonicalFilePath();
  if (path.isEmpty() || !(Paths::contains(project() + "/.lore/wiki", path) || Paths::contains(project() + "/.lore/sources", path) || Paths::contains(project() + "/.lore/project", path))) { message(tr("This page is outside the project memory.")); return {}; }
  try { return Store::read(path); } catch(const std::exception &e) { message(QString::fromUtf8(e.what())); return {}; }
}
QString Backend::markdown(const QString &text, const QString &accent) const {
  QTextDocument doc;
  doc.setMarkdown(text);
  for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
    QTextCursor cursor(block);
    auto format = block.blockFormat();
    format.setTopMargin(format.headingLevel() > 1 ? 16 : 0);
    format.setBottomMargin(format.headingLevel() > 0 ? 9 : 7);
    format.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
    cursor.setBlockFormat(format);
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (fragment.isValid() && fragment.charFormat().isAnchor()) {
        cursor.setPosition(fragment.position());
        cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        QTextCharFormat link; link.setForeground(QColor(accent)); cursor.mergeCharFormat(link);
      }
    }
  }
  // Archived Markdown can mention remote images. Keep reading a conversation
  // local rather than fetching an image embedded in historical text.
  QString html = doc.toHtml();
  html.remove(QRegularExpression("<img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption));
  return html;
}
void Backend::reveal() { if (m_store) QDesktopServices::openUrl(QUrl::fromLocalFile(project())); }
void Backend::cancel() { m_whenIdle = nullptr; if(m_cancelWork) *m_cancelWork=true; setAutoLearning(false); if (busy()) { m_cancelled = true; m_process.kill(); message(tr("Learning cancelled. Your captured data is safe.")); } }
void Backend::learn(const QString &provider) {
  if (!m_store) return;
  if (busy()) { deferUntilIdle([this, provider] { learn(provider); }); return; }
  if (provider != "codex" && provider != "claude") { message(tr("Choose Codex or Claude.")); emit learningFinished(false); return; }
  const auto tool = m_tools.command(provider);
  if (!tool.available()) { message(tr("Set up %1 in Connections before learning.").arg(provider == "claude" ? "Claude Code" : "Codex"), true); emit learningFinished(false); return; }
  try {
    const auto batch = m_store->learningBatch(); m_batch = batch.value("events").toArray();
    if (m_batch.isEmpty()) { message(tr("Nothing new to learn. Capture a conversation first.")); emit learningFinished(true); return; }
    m_operation="learn"; startModel(batch.value("prompt").toString(),provider);
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); emit learningFinished(false); }
}

void Backend::setLearningProvider(const QString &provider) {
  if (provider != "claude" && provider != "codex") return;
  m_learningProvider = provider; savePreferences(); emit statusChanged();
}
void Backend::chooseProject() {
  if (selecting()) return;
  if (busy() || capturing()) { deferUntilIdle([this] { chooseProject(); }); return; }
  m_pickerPurpose = "project"; emit statusChanged(); m_picker->choose(true, tr("Choose the project folder Lore should remember"));
}
void Backend::locateTool(const QString &id) {
  if (selecting() || m_tools.checking() || (id != "claude" && id != "codex")) return;
  m_pickerPurpose = id; emit statusChanged(); m_picker->choose(false, tr("Locate %1").arg(id == "claude" ? "Claude Code" : "Codex"));
}
bool Backend::selectProject(const QString &path) {
  if ((busy() || capturing()) && deferUntilIdle([this, path] { selectProject(path); })) return true;
  if (busy() || capturing()) { message(tr("Wait for capture to finish, or cancel learning before switching projects."), true); return false; }
  QString folder = path.trimmed(); if (folder.startsWith("~/")) folder.replace(0, 1, QDir::homePath());
  folder = QFileInfo(folder).canonicalFilePath();
  if (folder.isEmpty() || !QFileInfo(folder).isDir()) { message(tr("Choose an existing project folder."), true); return false; }
  if (QFileInfo::exists(folder + "/.lore/brain.sqlite")) return openProject(folder);
  m_watch.stop(); m_auto.stop(); m_autoLearning = false; m_store.reset(); m_connected = false;
  m_selectedProject = folder; m_stats.clear(); m_sessions.clear(); m_memories.clear(); m_results.clear(); m_brain.clear();
  clearError(); emit changed(); emit resultsChanged(); message(tr("Folder selected. Connect this project to start its memory.")); return true;
}
bool Backend::connectProject() {
  if (m_selectedProject.isEmpty() || capturing() || busy()) return false;
  if (!m_store && !openProject(m_selectedProject, true)) return false;
  connectAgents(); if (!m_connected) return false;
  sync(); return true;
}
bool Backend::connectionValid() const {
  if (!m_store) return false;
  try {
    const auto hooks = QJsonDocument::fromJson(Store::read(project() + "/.claude/settings.local.json").toUtf8()).object().value("hooks").toObject();
    for (const auto &event : {"SessionStart", "UserPromptSubmit", "Stop", "PreCompact", "SessionEnd"}) {
      bool found = false;
      for (const auto &handler : hooks.value(event).toArray()) for (const auto &value : handler.toObject().value("hooks").toArray()) {
        const auto hook = value.toObject(); const auto args = hook.value("args").toArray();
        if (args.size() == 3 && args[0].toString() == "hook" && args[1].toString() == "--project"
            && Paths::contains(project(), args[2].toString()) && Paths::contains(args[2].toString(), project())
            && QFileInfo(hook.value("command").toString()).canonicalFilePath() == QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()) found = true;
      }
      if (!found) return false;
    }
    return Store::read(project() + "/CLAUDE.md").contains("Read BRAIN.md") && Store::read(project() + "/AGENTS.md").contains("Read BRAIN.md");
  } catch (...) { return false; }
}
QVariantList Backend::recentProjects() const {
  QVariantList result;
  for (const auto &path : m_recent) result.append(QVariantMap{{"name", QFileInfo(path).fileName()}, {"path", path}});
  return result;
}
void Backend::savePreferences() {
  if (m_preferences.isEmpty() || m_restoring) return;
  if (m_store) {
    m_recent.removeAll(project()); m_recent.prepend(project()); while (m_recent.size() > 8) m_recent.removeLast();
    m_projectOptions[project()] = QVariantMap{{"provider", m_learningProvider}, {"watching", watching()}, {"autoLearning", autoLearning()}};
  }
  try { Store::write(m_preferences, QJsonDocument(QJsonObject{{"notificationsEnabled",m_notificationsEnabled}, {"recent", QJsonArray::fromStringList(m_recent)}, {"projects", QJsonObject::fromVariantMap(m_projectOptions)}, {"tools", QJsonObject::fromVariantMap(m_tools.overrides())}}).toJson()); }
  catch (const std::exception &e) { message(tr("Your project is saved, but app preferences could not be remembered: %1").arg(QString::fromUtf8(e.what())), true); }
  emit changed();
}
void Backend::restorePreferences() {
  if (m_preferences.isEmpty() || !QFileInfo::exists(m_preferences)) return;
  m_restoring = true;
  try {
    const auto saved = QJsonDocument::fromJson(Store::read(m_preferences).toUtf8()).object();
    m_notificationsEnabled=saved.value("notificationsEnabled").toBool(true);
    for (const auto &path : saved.value("recent").toArray()) if (path.isString()) m_recent.append(path.toString());
    m_projectOptions = saved.value("projects").toObject().toVariantMap(); m_tools.setOverrides(saved.value("tools").toObject().toVariantMap());
    if (!m_recent.isEmpty()) openProject(m_recent.first());
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
  m_restoring = false;
}

void Backend::startModel(const QString &prompt,const QString &provider) {
  const auto tool=m_tools.command(provider);
  if(!tool.available()) throw std::runtime_error("Set up your selected coding tool in Connections.");
  m_provider=provider; m_output.clear(); m_error.clear(); m_cancelled=false;
  m_process.setWorkingDirectory(QDir::tempPath());
  QStringList args;
  if(provider=="codex") args={"exec","--ephemeral","--skip-git-repo-check","--sandbox","read-only","--json","-"};
  else args={"-p","--output-format","json","--tools","","--strict-mcp-config","--mcp-config","{\"mcpServers\":{}}","--no-session-persistence","--setting-sources",""};
  clearError(); m_process.start(tool.program,tool.arguments+args); m_process.write(prompt.toUtf8()); m_process.closeWriteChannel();
  m_timeout.start(); message(m_operation=="ask" ? tr("Answering from project evidence using %1…").arg(provider) : tr("Learning through %1 using your existing login…").arg(provider));
}
void Backend::work(const QString &kind,const QString &query,bool quiet) {
  if(!m_store) return;
  if(busy() || capturing()) { if(!quiet) deferUntilIdle([this,kind,query] { work(kind,query); }); return; }
  const QString root=project(); m_workKind=kind; m_quietWork=quiet; m_cancelWork=std::make_shared<std::atomic_bool>(false); const auto cancel=m_cancelWork;
  m_work.setFuture(QtConcurrent::run([root,kind,query,cancel] {
    try {
      Store store(root); store.setCancellation(cancel);
      if(kind=="index") return store.indexProject();
      if(kind=="semantic" || kind=="download") { if(kind=="download") store.downloadSemanticModel(); return store.buildSemanticIndex(); }
      if(kind=="ask" && !store.projectFiles().isEmpty()) store.indexProject();
      if(kind=="ask") { auto result=store.answerPrompt(query); result["question"]=query; return result; }
      return store.briefing(query);
    } catch(const std::exception &e) { return QJsonObject{{"error",QString::fromUtf8(e.what())}}; }
  }));
  if(quiet) { quietWorkStarted(); emit statusChanged(); return; }
  message(kind=="download" ? tr("Downloading the local embedding model (about 622 MB), then indexing. You can cancel and resume later.") : tr("Preparing project evidence…"));
}
void Backend::indexProject() { work("index"); }
void Backend::updateSemantic(bool download) { work(download?"download":"semantic"); }
void Backend::disableSemantic() { if(!m_store || busy()) return; try { m_store->setSemanticEnabled(false); refresh(); message(tr("Semantic search disabled. Keyword search remains available.")); } catch(const std::exception &e) { message(QString::fromUtf8(e.what()),true); } }
void Backend::installSemanticRuntime() { QDesktopServices::openUrl(QUrl("https://ollama.com/download")); message(tr("Install and open Ollama, then choose Download model & enable. Embeddings stay on this computer.")); }
void Backend::openReleases() {
  if (!QDesktopServices::openUrl(QUrl("https://github.com/MadeBySaints/lore/releases")))
    message(tr("Could not open your browser. Visit https://github.com/MadeBySaints/lore/releases to check for updates."),true);
}
void Backend::ask(const QString &question) { if(question.trimmed().isEmpty()) return; if(!m_tools.ready(m_learningProvider)) { message(tr("Sign in to your selected coding tool in Connections before asking."),true); return; } m_answer.clear(); emit changed(); work("ask",question.trimmed().left(4000)); }
void Backend::previewContext(const QString &query) { work("preview",query); }
void Backend::reviewMemory(int id,const QString &action,const QString &correction) { if(!m_store) return; if(busy() || capturing()) { deferUntilIdle([this,id,action,correction] { reviewMemory(id,action,correction); }); return; } try { m_store->reviewMemory(id,action,correction); m_answer.clear(); m_preview.clear(); refresh(); if(m_semantic.value("enabled").toBool()) QTimer::singleShot(0,this,[this] { work("semantic",{},true); }); message(tr("Memory review saved.")); } catch(const std::exception &e) { message(QString::fromUtf8(e.what()),true); } }
