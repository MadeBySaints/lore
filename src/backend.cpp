#include "backend.h"
#include <QNetworkProxy>
#include <QNetworkRequest>
#include "paths.h"
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QDir>
#include <QTime>
#include <QUuid>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUrl>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QtConcurrent>

namespace {
class TerminalHighlighter final : public QSyntaxHighlighter {
public:
  explicit TerminalHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
    setObjectName("terminalHighlighter");
  }

  void setColors(const QColor &accent, const QColor &warning, const QColor &caution) {
    m_accent = accent;
    m_warning = warning;
    m_caution = caution;
    rehighlight();
  }

private:
  void highlightBlock(const QString &text) override {
    if (text.startsWith("× ")) setFormat(0, text.size(), m_warning);
    else if (text.startsWith("! ") || text.startsWith("↻ ")) setFormat(0, text.size(), m_caution);
    else if (text.startsWith("── ")) setFormat(0, text.size(), m_accent);
  }
  QColor m_accent, m_warning, m_caution;
};
}

Backend::Backend(QObject *parent, FilePicker *picker, const QString &preferences, CapturePaths capturePaths)
  : QObject(parent), m_tools(this), m_picker(picker ? picker : new NativeFilePicker(this)), m_preferences(preferences), m_capturePaths(capturePaths) {
  connect(&m_tools, &Toolbox::notice, this, &Backend::message);
  connect(&m_tools, &Toolbox::settingsChanged, this, &Backend::savePreferences);
  connect(m_picker, &FilePicker::selected, this, [this](const QString &path) {
    const QString purpose = m_pickerPurpose; m_pickerPurpose.clear(); emit statusChanged();
    if (purpose == "project") selectProject(path);
    else if (purpose == "chatgpt") emit chatGptExportSelected(path);
    else if (purpose == "working") setWorkingDirectory(path);
    else if (purpose == "settings-source") emit folderProposed("source", QFileInfo(path).canonicalFilePath());
    else if (purpose == "settings-working") emit folderProposed("working", QFileInfo(path).canonicalFilePath());
    else if (!purpose.isEmpty()) m_tools.setExecutable(purpose, path);
  });
  connect(m_picker, &FilePicker::cancelled, this, [this] { m_pickerPurpose.clear(); emit statusChanged(); });
  connect(m_picker, &FilePicker::failed, this, [this](const QString &reason) { m_pickerPurpose.clear(); message(reason, true); });
  connect(&m_search, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    m_searchActive = false;
    if (m_runningSearch == m_searchRevision) {
      const auto result = m_search.result();
      m_results = result.value("evidence").toArray().toVariantList();
      m_searchError = result.value("error").toString();
      m_searchPending = false;
      emit resultsChanged();
    } else if (m_searchPending) startSearch();
  });
  connect(&m_scan, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    const auto result = m_scan.result();
    for (const auto &v : result.value("lore").toArray()) {
      const auto found = v.toObject(); const QString working = found.value("working").toString();
      if (working.isEmpty() || m_hiddenProjects.contains(working)) continue;
      auto options = m_projectOptions.value(working).toMap();
      for (const auto &key : {"source", "name", "conversations", "entries"}) options[key] = found.value(key).toVariant();
      m_projectOptions[working] = options;
    }
    m_suggested.clear();
    for (const auto &v : result.value("suggested").toArray()) if (!m_hiddenProjects.contains(v.toObject().value("path").toString())) m_suggested.append(v.toObject().toVariantMap());
    savePreferences(); emit projectsChanged();
  });
  connect(&m_capture, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    m_capturing = false;
    const bool quiet = m_quietCapture; m_quietCapture = false; quietWorkEnded();
    QTimer::singleShot(0, this, &Backend::runDeferred);
    const auto result = m_capture.result();
    // The worker read the project only when something changed since the last look.
    if (result.contains("snapshot")) { try { applySnapshot(result.value("snapshot").toObject()); } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); } }
    if (result.contains("error")) { message(result.value("error").toString(), true); return; }
    if (m_autoLearning && result.value("added").toInt() > 0) m_auto.start();
    // New conversation records mean someone is working: start the wait over.
    if (result.value("added").toInt() > 0 && !m_countdownKind.isEmpty()) { m_countdownLeft = m_countdownKind == "build" ? m_autoBuildDelay : m_autoVerifyDelay; emit countdownChanged(); }
    if((result.value("added").toInt()>0 || result.value("projectChanged").toBool()) && m_semantic.value("enabled").toBool()) QTimer::singleShot(1200,this,[this] { if(!busy() && !capturing()) work("semantic",{},true); });
    const auto warnings = result.value("warnings").toArray();
    if (!warnings.isEmpty()) message(tr("Some conversations could not be captured: %1").arg(QString::fromUtf8(QJsonDocument(warnings).toJson(QJsonDocument::Compact))), true);
    else if (!busy() && (!quiet || result.value("added").toInt() > 0)) {
      if (m_stats.value("events").toInt() > 0) message(tr("Capture is up to date. %1 new records saved.").arg(result.value("added").toInt()));
      else if (m_stats.value("captureExclusions").toInt() > 0) message(tr("No conversations are saved. Lore skips previously forgotten sessions. Start a new coding session or save a conversation in Sources."));
      else message(tr("No conversations are saved yet. Start a coding session in the project folder."));
    }
    emit statusChanged();
  });
  m_watch.setInterval(5000);
  connect(&m_watch, &QTimer::timeout, this, [this] { syncNow(true); });
  m_reveal.setSingleShot(true); m_reveal.setInterval(1500);
  connect(&m_reveal, &QTimer::timeout, this, [this] { m_revealed = true; emit statusChanged(); });
  // m_auto waits for a quiet moment after capture; m_schedule checks the
  // triggers regularly so an hourly build happens without new captures.
  m_auto.setSingleShot(true); m_auto.setInterval(qEnvironmentVariableIntValue("LORE_AUTO_QUIET_MS") > 0 ? qEnvironmentVariableIntValue("LORE_AUTO_QUIET_MS") : 60000);
  connect(&m_auto, &QTimer::timeout, this, &Backend::runAutomation);
  m_schedule.setInterval(qEnvironmentVariableIntValue("LORE_AUTO_CHECK_MS") > 0 ? qEnvironmentVariableIntValue("LORE_AUTO_CHECK_MS") : 60000);
  connect(&m_schedule, &QTimer::timeout, this, &Backend::runAutomation);
  // The idle countdown: while Lore is busy it holds at the full delay; once
  // idle it counts down, showing the time left, then starts the work.
  m_countdown.setInterval(1000);
  connect(&m_countdown, &QTimer::timeout, this, [this] {
    if (m_countdownKind.isEmpty()) { m_countdown.stop(); return; }
    const QString kind = m_countdownKind; const int delay = kind == "build" ? m_autoBuildDelay : m_autoVerifyDelay;
    if (kind == "verify" && automationDue()) { stopCountdown(); runAutomation(); return; }  // A build comes first.
    if (kind == "build" ? !automationDue() : !autoVerifyDue()) { stopCountdown(); return; }  // No longer due.
    if (busy() || capturing()) { if (m_countdownLeft != delay) { m_countdownLeft = delay; emit countdownChanged(); } return; }  // Not idle: wait again.
    if (--m_countdownLeft > 0) {
      quietMessage(kind == "build" ? tr("Automatic wiki build starts in %1 s if Lore stays idle.").arg(m_countdownLeft) : tr("Automatic verification starts in %1 s if Lore stays idle.").arg(m_countdownLeft));
      emit countdownChanged(); return;
    }
    stopCountdown(); startAutomatic(kind);
  });
  connect(&m_tools, &Toolbox::changed, this, [this] {
    emit statusChanged();
    if (m_autoLearning && modelReady("build", m_autoProvider) && automationDue()) m_auto.start();
  });
  m_timeout.setParent(this);
  m_timeout.setObjectName("modelCallTimeout");
  m_timeout.setSingleShot(true); m_timeout.setInterval(180000);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    m_functionTimedOut = m_knowledgeJob == "functions";
    if (!m_knowledgeJob.isEmpty() || m_operation == "skillBuild" || m_operation == "completion") { stopModel(); return; }
    stopModel(); message(tr("Learning timed out. Your captured data is safe."));
  });
  m_network.setProxy(QNetworkProxy::NoProxy);
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { const QByteArray chunk = m_process.readAllStandardOutput(); m_output += chunk; liveLines(chunk); });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] { const QByteArray chunk = m_process.readAllStandardError(); m_error += chunk; live(QString::fromUtf8(chunk)); });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      if (m_operation == "verify" || m_operation == "completion" || m_operation == "skillBuild" || !m_knowledgeJob.isEmpty()) finishModel({}, m_process.errorString());
      else { m_timeout.stop(); message(m_process.errorString(), true); emit learningFinished(false); }
    }
  });
  connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus state) {
    if (m_cancelled) { finishModel({}, {}); return; }
    if (code != 0 || state != QProcess::NormalExit) {
      const auto detail = QString::fromUtf8(m_error).right(1800);
      finishModel({}, m_operation == "skillBuild" ? tr("The skill builder failed: %1").arg(detail) : tr("Learning failed: %1").arg(detail));
      return;
    }
    QString answer;
    if (m_provider == "codex") {
      for (const auto &line : m_output.split('\n')) {
        const auto o = QJsonDocument::fromJson(line).object();
        const auto item = o.value("item").toObject();
        if (o.value("type").toString() == "item.completed" && item.value("type").toString() == "agent_message") answer = item.value("text").toString();
      }
    } else {
      // Streamed: one JSON event per line, ending with the result.
      QJsonObject o;
      for (const auto &line : m_output.split('\n')) { const auto event = QJsonDocument::fromJson(line).object(); if (event.contains("result")) o = event; }
      if (o.value("is_error").toBool()) { finishModel({}, o.value("result").toString()); return; }
      answer = o.value("result").toString();
    }
    finishModel(answer, {});
  });
  connect(&m_work, &QFutureWatcher<QJsonObject>::finished, this, [this] {
    const auto result=m_work.result();
    m_progressPoll.stop(); if(!m_semanticView.isEmpty()) { m_semanticView.clear(); emit progressChanged(); }
    if(m_quietWork) { m_quietWork=false; quietWorkEnded(); }
    QTimer::singleShot(0, this, &Backend::runDeferred);
    if (m_workKind == "knowledgeCommit") { finishKnowledgeCommit(result); return; }
    if (m_workKind == "functionPlan" || m_workKind == "transcriptPlan") {
      if (result.contains("error") || m_knowledgeCancelled) {
        finishKnowledge(result.value("error").toString());
      } else {
        m_knowledgeQueue = result.value("queue").toArray();
        nextKnowledgeChunk();
      }
      return;
    }
    if(result.contains("error")) { message(result.value("error").toString(),true); if(m_workKind=="ask" || m_workKind=="verify") emit learningFinished(false); if(m_workKind=="review") emit statusChanged(); if(m_workKind=="verify" && m_verifyRunning) { ++m_verifyFailed; m_verifyHadError = true; QTimer::singleShot(0,this,&Backend::continueVerify); } return; }
    try {
      if (m_workKind == "forgetConversations" || m_workKind == "deleteForgottenConversations" || m_workKind == "reviewTranscript") {
        if (m_workKind == "forgetConversations") resetModelOutput();
        m_answer.clear(); m_preview.clear(); search({}); m_rendered.clear();
        emit answerChanged(); emit resultsChanged();
        result.contains("snapshot") ? applySnapshot(result.value("snapshot").toObject()) : refresh();
        refreshKnowledge();
        if (m_workKind == "forgetConversations") message(tr("Saved conversation history removed from Lore."));
        else if (m_workKind == "deleteForgottenConversations") message(tr("Forgotten conversation records and their wiki entries were deleted."));
        else message(tr("Finding reviewed."));
        emit statusChanged();
        return;
      }
      if (m_workKind == "deleteWiki") {
        m_answer.clear(); m_preview.clear(); m_results.clear();
        emit answerChanged(); emit resultsChanged();
        result.contains("snapshot") ? applySnapshot(result.value("snapshot").toObject()) : refresh();
        message(tr("Compiled wiki deleted. Saved conversations and the project index are still available."));
        emit statusChanged();
        return;
      }
      if(m_workKind=="review") {
        m_answer.clear(); m_preview.clear(); emit answerChanged(); result.contains("snapshot") ? applySnapshot(result.value("snapshot").toObject()) : refresh();
        if(m_semantic.value("enabled").toBool()) m_semanticSoon.start();
        message(tr("Memory review saved.")); return;
      }
      if(m_workKind!="preview") { if(result.contains("snapshot")) applySnapshot(result.value("snapshot").toObject()); else refresh(); }
      if(m_workKind=="ask") {
        m_question=result.value("question").toString(); m_answerEvidence=result.value("evidence").toArray();
        if(m_answerEvidence.isEmpty()) {
          m_answer=QVariantMap{{"question",m_question},{"answer",tr("I could not find supporting evidence. Capture conversations or index the project files, then ask a more specific question.")},{"insufficient",true},{"refs",QVariantList()}}; emit answerChanged(); message(tr("No supporting evidence found. No model call was made.")); emit learningFinished(true); return;
        }
        m_askContext = result.value("contextTokens").toInt();
        m_operation="ask"; startModel(result.value("prompt").toString(),result.value("provider").toString());
      } else if(m_workKind=="verify" || m_workKind=="completion") {
        if (m_cancelWork && *m_cancelWork) return;
        if (result.value("skipped").toBool()) {
          if (m_verifyRunning) { ++m_verifyFailed; QTimer::singleShot(0, this, &Backend::continueVerify); }
          return;
        }
        m_verifyId=result.value("memory").toInt(); m_verifyEvidence=result.value("evidence").toArray();
        if (m_verifyEvidence.isEmpty()) {
          if (m_workKind == "completion") {
            const auto explanation = tr("Lore found no relevant project files to establish whether the work is complete.");
            m_store->saveVerification(m_verifyId, {{"verdict", "unclear"}, {"answer", explanation}}, {}, "lore", true);
            refresh(); message(explanation); return;
          }
          message(tr("No verification evidence was found. No model call was made. Lore will reconsider this memory when the evidence changes."));
          emit learningFinished(true);
          if (m_verifyRunning) { ++m_verifyFailed; QTimer::singleShot(0, this, &Backend::continueVerify); }
          return;
        }
        m_operation=m_workKind; startModel(result.value("prompt").toString(),m_workKind == "completion" ? m_verifyProvider : verifyModel());
      } else if (m_workKind == "retryLearning") {
        message(tr("Queued %n skipped message(s) for the next wiki build.", "", result.value("queued").toInt()));
      } else if (m_workKind == "chatgpt") {
        const int added = result.value("added").toInt();
        if (added && autoLearning()) m_auto.start();
        message(added ? tr("Imported %n conversation turn(s).", "", added) : tr("This export had no new turns."));
      } else if(m_workKind=="preview") { m_preview=result.toVariantMap(); emit answerChanged(); message(tr("Context preview ready. This preview has not been sent to an agent.")); }
      else message(tr("Project intelligence updated."));
    } catch(const std::exception &e) { if (m_workKind == "completion" && !modelRunning()) m_operation.clear(); message(QString::fromUtf8(e.what()),true); if(m_workKind=="ask" || m_workKind=="verify") emit learningFinished(false); if(m_workKind=="verify" && m_verifyRunning) { ++m_verifyFailed; m_verifyHadError = true; QTimer::singleShot(0,this,&Backend::continueVerify); } }
    emit statusChanged();
  });
  // Model output reaches the page in batches, so a fast stream stays smooth.
  m_liveFlush.setSingleShot(true); m_liveFlush.setInterval(100);
  connect(&m_liveFlush, &QTimer::timeout, this, [this] { const QString chunk = m_livePending; m_livePending.clear(); if (!chunk.isEmpty()) emit liveAppended(chunk); emit liveChanged(); });
  // Download and indexing progress, read from the worker twice a second.
  m_progressPoll.setInterval(500);
  connect(&m_progressPoll, &QTimer::timeout, this, [this] {
    if (!m_progress || m_progress->stage == 0) return;
    const int stage = m_progress->stage; const qint64 done = m_progress->done, total = m_progress->total;
    const QVariantMap view{{"stage", stage == 1 ? "download" : "index"}, {"done", done}, {"total", total}};
    if (view == m_semanticView) return;
    m_semanticView = view;
    if (!m_quietWork || m_revealed) {
      if (stage == 1) quietMessage(total > 0 ? tr("Downloading EmbeddingGemma: %1 of %2 MB (%3%)").arg(done / 1048576).arg(total / 1048576).arg(done * 100 / total) : tr("Downloading EmbeddingGemma: starting…"));
      else quietMessage(tr("Indexing for semantic search: %1 of %2 excerpts").arg(QLocale().toString(done)).arg(QLocale().toString(total)));
    }
    emit progressChanged();
  });
  m_semanticSoon.setSingleShot(true); m_semanticSoon.setInterval(3000);
  connect(&m_semanticSoon,&QTimer::timeout,this,[this] {
    if(!m_store || !m_semantic.value("enabled").toBool()) return;
    if(busy() || capturing() || !m_reviews.isEmpty()) { m_semanticSoon.start(); return; }
    work("semantic",{},true);
  });
  m_notificationExpiry.setSingleShot(true);
  m_notificationExpiry.setInterval(qMax(100,qEnvironmentVariableIntValue("LORE_NOTIFICATION_TIMEOUT_MS") ? qEnvironmentVariableIntValue("LORE_NOTIFICATION_TIMEOUT_MS") : 12000));
  connect(&m_notificationExpiry,&QTimer::timeout,this,&Backend::clearError);
  connect(&m_memoryModel, &MemoryModel::attentionOnlyChanged, this, &Backend::savePreferences);
  restorePreferences();
  if (m_recent.isEmpty() && m_tools.available("claude")) m_learningProvider = "claude";
}
Backend::~Backend() { if(m_reply) { m_reply->disconnect(this); m_reply->abort(); } if(m_cancelWork) *m_cancelWork=true; m_work.waitForFinished(); m_capture.waitForFinished(); m_scan.waitForFinished(); m_search.waitForFinished(); if (m_process.state()!=QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(2000); } }
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
  emit messageChanged(); emit statusChanged();
}
// A status line that changes often (a countdown, download progress) without
// telling every other binding that the app's state changed.
void Backend::quietMessage(const QString &text) { if (text == m_status && m_errorMessage.isEmpty()) return; m_status = text; m_errorMessage.clear(); m_notificationExpiry.stop(); emit messageChanged(); }
void Backend::emitAll() { emit changed(); emit statsChanged(); emit brainChanged(); emit activityChanged(); emit answerChanged(); emit presetsChanged(); }
void Backend::setNotificationsEnabled(bool on) { m_notificationsEnabled=on; if(!on) clearError(); savePreferences(); emit statusChanged(); }
void Backend::clearError() { m_errorMessage.clear(); emit statusChanged(); }
void Backend::refresh() {
  if (!m_store) return;
  applySnapshot(m_store->snapshot());
}
void Backend::applySnapshot(const QJsonObject &snapshot) {
  if (!m_store || snapshot.isEmpty()) return;
  m_fingerprint = snapshot.value("fingerprint").toString();
  const auto stats = snapshot.value("stats").toObject().toVariantMap();
  const QVariantMap semantic;
  const int reviewWaiting = snapshot.value("reviewWaiting").toInt();
  if (stats != m_stats || semantic != m_semantic || reviewWaiting != m_reviewWaiting) {
    m_stats = stats; m_semantic = semantic; m_reviewWaiting = reviewWaiting; emit statsChanged();
  }
  if (const QString brain = snapshot.value("brain").toString(); brain != m_brain) { m_brain = brain; emit brainChanged(); }
  static const QRegularExpression number("[0-9]+");
  QVariantList activity = snapshot.value("activity").toArray().toVariantList();
  for(auto &value:activity) {
    auto item=value.toMap(); const QString kind=item.value("kind").toString(), body=item.value("body").toString();
    QStringList numbers; auto matches=number.globalMatch(body); while(matches.hasNext()) numbers.append(matches.next().captured());
    if(kind=="capture" && numbers.size()>=1) item["body"]=tr("Saved %1 new conversation records.").arg(numbers[0]);
    else if(kind=="learn" && numbers.size()>=2) item["body"]=tr("Updated %1 wiki topics from %2 messages.").arg(numbers[0],numbers[1]);
    else if(kind=="index" && numbers.size()>=2) item["body"]=tr("Indexed %1 project files; skipped %2 excluded, binary, or oversized files.").arg(numbers[0],numbers[1]);
    else if(kind=="semantic" && numbers.size()>=2) item["body"]=tr("Updated %1 local embeddings; %2 evidence excerpts indexed.").arg(numbers[0],numbers[1]);
    else if(kind=="answer" && numbers.size()>=1) item["body"]=tr("Answered with %1 cited evidence excerpts.").arg(numbers[0]);
    else if(kind=="review" && numbers.size()>=1) item["body"]=tr("Reviewed memory %1.").arg(numbers[0]);
    else if(kind=="verify" && numbers.size()>=2) item["body"]=tr("Checked memory %1 against %2 cited file excerpts.").arg(numbers[0],numbers[1]);
    value=item;
  }
  const QVariantList deliveries = snapshot.value("deliveries").toArray().toVariantList();
  if (activity != m_activity || deliveries != m_deliveries) { m_activity = activity; m_deliveries = deliveries; emit activityChanged(); }
  m_sessions = snapshot.value("sessions").toArray().toVariantList();
  m_memories.clear();
  m_hiddenWikiEntries = 0;
  for (const auto &value : snapshot.value("memories").toArray()) {
    const auto item = value.toObject();
    if (item.value("forgottenSource").toBool()) ++m_hiddenWikiEntries;
    else m_memories.append(item.toVariantMap());
  }
  const auto projectFiles = snapshot.value("files").toArray().toVariantList();
  if (projectFiles != m_files) { m_files = projectFiles; refreshKnowledge(); }
  if (const bool connected = connectionValid(); connected != m_connected || project() != m_shownProject) { m_connected = connected; m_shownProject = project(); emit changed(); }
  emitLists();
}
void Backend::emitLists(bool force) {
  if(force || m_shownSessions!=m_sessions) { m_shownSessions=m_sessions; emit sessionsChanged(); }
  if(force || m_shownMemories!=m_memories || m_shownHiddenWikiEntries!=m_hiddenWikiEntries) {
    m_shownMemories=m_memories; m_shownHiddenWikiEntries=m_hiddenWikiEntries; m_memoryModel.setEntries(m_memories);
    m_nextSteps.clear();
    for(const auto &v:m_memories) {
      const auto e=v.toMap(); const QString review=e.value("review").toString();
      if(e.value("kind").toString()!="next" || e.value("supersededBy").toInt()!=0 || e.value("stale").toBool() || e.value("excluded").toBool() || !e.value("conflicts").toList().isEmpty() || review=="outdated" || review=="forgotten") continue;
      m_nextSteps.append(QVariantMap{{"id",e.value("id")},{"title",e.value("title")},{"kind",e.value("kind")}});
    }
    emit memoriesChanged();
  }
  if(force || m_shownFiles!=m_files) { m_shownFiles=m_files; emit filesChanged(); }
}
bool Backend::openProject(const QString &path, bool create) { return openProjectMode(path, create, false); }
bool Backend::openProjectMode(const QString &path, bool create, bool personal) {
  if (busy() || capturing()) { message(tr("Wait for capture to finish, or cancel learning before switching projects."), true); return false; }
  try {
    QString p = path.trimmed(); if (p.startsWith("~/")) p.replace(0, 1, QDir::homePath());
    // Exports are rebuilt only when out of date; captures keep them current.
    auto store = std::make_unique<Store>(p, create, personal);
    if (personal && !store->personalWiki()) throw std::runtime_error(tr("That folder already holds a project archive. Choose another folder for your personal wiki.").toStdString());
    if (create || store->exportsStale() || !QFileInfo::exists(store->root() + "/.lore/wiki/index.md")) store->render();
    if (!m_store || m_store->root() != store->root()) resetModelOutput();
    m_store = std::move(store);
    search({});
    m_personalSetup = false;
    m_personalCapture = personalWiki() && m_store->setting("personalHooksReady") == "on" && m_store->setting("personalCapturePaused") != "on";
    m_answer.clear(); m_preview.clear(); m_results.clear(); m_reviews.clear();
    const QString savedAnswer=project()+"/.lore/last-answer.json";
    if(QFileInfo::exists(savedAnswer) && !QFileInfo(savedAnswer).isSymLink()) {
      const auto saved=QJsonDocument::fromJson(Store::read(savedAnswer).toUtf8()).object();
      const auto live=m_store->evidenceHashes(saved.value("refs").toArray());
      bool valid=true; for(const auto &v:saved.value("refs").toArray()) { const auto o=v.toObject(); if(live.value(o.value("id").toString())!=o.value("hash").toString()) valid=false; }
      if(valid) m_answer=saved.toVariantMap();
    }
    m_selectedProject = m_store->source(); m_workingDirectory = project();
    m_auto.stop(); m_schedule.stop(); m_autoLearning = false; m_autoRun = false;
    refresh();
    refreshKnowledge(); emit knowledgeChanged();
    const auto options = m_projectOptions.value(project()).toMap();
    m_modelOptions = options.value("modelChoices").toMap();
    ++m_modelRevision; emit modelChoicesChanged();
    m_learningProvider = options.value("provider", m_learningProvider).toString();
    // Projects saved before Ask & Verify had their own choice used one provider for both.
    m_answerProvider = options.value("answerProvider", m_learningProvider == "local" ? m_answerProvider : m_learningProvider).toString();
    m_verifyProvider = options.value("verifyProvider", m_answerProvider).toString();
    emit presetsChanged();
    m_autoBuildDelay = options.value("autoBuildDelay", 30).toInt(); m_autoVerifyDelay = options.value("autoVerifyDelay", 60).toInt(); stopCountdown();
    m_autoVerify = false; m_autoVerifyProvider = options.value("autoVerifyProvider", m_verifyProvider).toString();
    m_autoVerifyAfter = options.value("autoVerifyAfter", 10).toInt(); m_autoVerifyEvery = options.value("autoVerifyEvery", 0).toInt(); m_lastAutoVerify = options.value("lastAutoVerify").toString();
    { auto record = m_projectOptions.value(project()).toMap(); record["lastOpened"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate); m_projectOptions[project()] = record; }
    m_hiddenProjects.removeAll(project()); m_hiddenProjects.removeAll(m_store->source());
    if (m_connected && options.value("watching", true).toBool()) m_watch.start(); else m_watch.stop();
    m_autoProvider = m_learningProvider;
    // Before triggers existed, automatic learning ran after any new message.
    m_autoAfter = options.value("autoAfter", options.value("autoLearning", false).toBool() ? 1 : 25).toInt();
    m_autoEvery = options.value("autoEvery", 0).toInt(); m_lastAutoBuild = options.value("lastAutoBuild").toString();
    if (m_connected && options.value("autoLearning", false).toBool()) { m_autoLearning = true; m_auto.start(); }
    updateSchedule();
    clearError(); savePreferences(); message(tr("Project memory ready.")); return true;
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); return false; }
}
void Backend::setWatching(bool on) { if (on && m_store) m_watch.start(); else { m_watch.stop(); m_auto.stop(); m_autoLearning = false; m_autoRun = false; updateSchedule(); } savePreferences(); emit statusChanged(); }
void Backend::setAutoLearning(bool on) {
  m_autoLearning = on;
  if (on) { setWatching(true); m_auto.start(); } else { m_auto.stop(); m_autoRun = false; if (m_countdownKind == "build") stopCountdown(); }
  updateSchedule(); savePreferences(); emit statusChanged();
}
// Check whether a wiki build or verification is due.
void Backend::updateSchedule() {
  if (m_store && (m_autoLearning || m_autoVerify)) {
    if (!m_schedule.isActive()) m_schedule.start();
  } else m_schedule.stop();
}
void Backend::setAutoVerify(bool on) {
  m_autoVerify = on;
  if (!on && m_verifyAuto && (!m_verifyQueue.isEmpty() || m_verifyRunning)) { m_verifyQueue.clear(); m_verifyTotal = 0; }
  if (!on && m_countdownKind == "verify") stopCountdown();
  updateSchedule(); savePreferences(); emit statusChanged();
  if (on) QTimer::singleShot(0, this, &Backend::runAutomation);
}
void Backend::setAutoVerifyProvider(const QString &provider) { if (provider != "claude" && provider != "codex" && provider != "local") return; m_autoVerifyProvider = provider; savePreferences(); emit statusChanged(); }
void Backend::setAutoBuildDelay(int seconds) { m_autoBuildDelay = qBound(0, seconds, 3600); savePreferences(); emit statusChanged(); }
void Backend::setAutoVerifyDelay(int seconds) { m_autoVerifyDelay = qBound(0, seconds, 3600); savePreferences(); emit statusChanged(); }
void Backend::setAutoVerifyAfter(int entries) { m_autoVerifyAfter = qBound(0, entries, 100000); savePreferences(); emit statusChanged(); }
void Backend::setAutoVerifyEvery(int hours) { m_autoVerifyEvery = qBound(0, hours, 168); savePreferences(); emit statusChanged(); }
void Backend::sync() { syncNow(false); }
bool Backend::saveConversation(const QString &text) {
  if (!m_store || busy() || capturing()) return false;
  try {
    const int added = m_store->importConversation(text);
    refresh();
    if (added && autoLearning()) m_auto.start();
    message(added ? tr("Saved %n conversation turn(s).", "", added) : tr("This conversation is already saved."));
    return true;
  } catch (const std::exception &error) {
    message(QString::fromUtf8(error.what()), true);
    return false;
  }
}
void Backend::syncNow(bool quiet) {
  if (!m_store) return;
  if (capturing() && !quiet && m_quietCapture) { m_quietCapture = false; quietWorkEnded(); message(tr("Capturing saved conversations…")); return; }
  if (capturing() || m_work.isRunning()) { if (!quiet) deferUntilIdle([this] { syncNow(false); }); return; }
  const QString root = project(); const auto paths = m_capturePaths;
  m_capturing = true; m_quietCapture = quiet;
  if (quiet) { quietWorkStarted(); emit statusChanged(); }
  const QString seen = m_fingerprint;
  m_capture.setFuture(QtConcurrent::run([root, paths, seen] {
    try {
      Store store(root); auto result = store.sync(paths.codex, paths.claude);
      // Most ticks find nothing new: the window then reads nothing at all.
      if (result.value("added").toInt() > 0 || result.value("projectChanged").toBool() || store.fingerprint() != seen) result["snapshot"] = store.snapshot();
      return result;
    }
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
  if (workBusy() || showCapturing()) return false;
  m_whenIdle = std::move(action); return true;
}
void Backend::runDeferred() {
  if (m_work.isRunning() || capturing()) return;
  if (!m_reviews.isEmpty()) { startReview(); return; }
  if (!m_whenIdle) return;
  auto action = std::move(m_whenIdle); m_whenIdle = nullptr; action();
}
void Backend::search(const QString &text) {
  searchProjects(text, false);
}
void Backend::searchProjects(const QString &text, bool allProjects) {
  ++m_searchRevision;
  m_query = text.trimmed().left(1000);
  m_results.clear();
  m_searchError.clear();
  m_searchAllProjects = allProjects;
  m_searchPending = m_store && !personalWiki() && !m_query.isEmpty();
  emit resultsChanged();
  if (m_searchPending && !m_searchActive) startSearch();
}
void Backend::startSearch() {
  m_searchActive = true;
  m_runningSearch = m_searchRevision;
  const QString root = project(), query = m_query;
  const bool all = m_searchAllProjects;
  QList<QPair<QString, QString>> archives{{root, m_stats.value("name").toString()}};
  if (all) for (const auto &value : knownProjects()) {
    const auto entry = value.toMap();
    if (!entry.value("current").toBool() && !entry.value("missing").toBool())
      archives.append({entry.value("working").toString(), entry.value("name").toString()});
  }
  m_search.setFuture(QtConcurrent::run([archives, query, all] {
    QJsonArray hits;
    QString error;
    for (const auto &entry : archives) {
      try {
        Store store(entry.first, false, false, 500, true);
        if (store.personalWiki()) continue;
        const auto matches = store.briefing(query, all ? 4 : 8, false).value("evidence").toArray();
        for (const auto &value : matches) {
          auto item = value.toObject();
          if (all) {
            item["archive"] = entry.first;
            item["projectName"] = entry.second;
          }
          hits.append(item);
        }
      } catch (const std::exception &failure) { if (error.isEmpty()) error = QString::fromUtf8(failure.what()); }
    }
    return QJsonObject{{"evidence", hits}, {"error", error}};
  }));
}

void Backend::connectAgents() {
  if (!m_store) return;
  try { m_store->connectAgents(QCoreApplication::applicationFilePath()); clearError(); refresh(); setWatching(true); savePreferences(); message(tr("Connected. Start a new coding session in this project.")); }
  catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}
void Backend::setPersonalSetup(bool enabled) {
  if (m_store || m_personalSetup == enabled) return;
  m_personalSetup = enabled;
  if (!m_selectedProject.isEmpty()) m_workingDirectory = enabled ? m_selectedProject : defaultWorkingDirectory(m_selectedProject);
  emit changed();
}
void Backend::setPersonalCapture(bool enabled) {
  if (!personalWiki() || m_personalCapture == enabled) return;
  try {
    m_store->setSetting("personalCapturePaused", enabled ? QString() : QStringLiteral("on"));
    m_personalCapture = enabled;
    message(enabled ? tr("Personal capture resumed.") : tr("Personal capture paused."));
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}
void Backend::reconnectPersonalHooks() {
  if (!personalWiki() || busy()) return;
  try {
    m_store->connectPersonalHooks(QCoreApplication::applicationFilePath());
    m_personalCapture = true;
    refresh();
    setWatching(true);
    message(tr("Personal capture hooks connected."));
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}
QString Backend::readPage(const QString &relative, const QString &archive) {
  if (!m_store) return {};
  const QString root = archive.isEmpty() ? project() : archive;
  if (root != project() && (!m_projectOptions.contains(root) || m_hiddenProjects.contains(root))) return {};
  const QString file = relative.section('#', 0, 0);
  const QString anchor = relative.section('#', 1, 1);
  const QString path = QFileInfo(root + '/' + file).canonicalFilePath();
  if (path.isEmpty() || !(Paths::contains(root + "/.lore/wiki", path) || Paths::contains(root + "/.lore/sources", path) || Paths::contains(root + "/.lore/project", path) || Paths::contains(root + "/.lore/functions", path))) { message(tr("This page is outside the project memory.")); return {}; }
  try {
    const QString document = Store::read(path);
    if (anchor.isEmpty()) return document;
    static const QRegularExpression event("^[a-f0-9]{24}$");
    if (file.startsWith(".lore/sources/") && event.match(anchor).hasMatch()) {
      const QString marker = "<a id=\"" + anchor + "\"></a>";
      const auto start = document.indexOf(marker);
      if (start < 0) return tr("This cited turn is unavailable.");
      const auto next = document.indexOf("<a id=\"", start + marker.size());
      const auto after = next < 0 ? -1 : document.indexOf("<a id=\"", next + 1);
      return tr("[Read the full conversation](%1)").arg(file) + "\n\n"
        + document.mid(start + marker.size(), after < 0 ? -1 : after - start - marker.size());
    }
    static const QRegularExpression line("^L([1-9][0-9]{0,6})$");
    const auto match = line.match(anchor);
    if (file.startsWith(".lore/project/") && match.hasMatch()) {
      const auto start = document.indexOf("\n\n    ");
      if (start < 0) return document;
      const int cited = match.captured(1).toInt();
      const auto lines = document.mid(start + 2).split('\n');
      if (cited > lines.size()) return tr("This cited line is unavailable.");
      const int first = qMax(1, cited - 3), last = qMin(int(lines.size()), cited + 16);
      QString excerpt = tr("Indexed snapshot · line %1\n\n[Read the full snapshot](%2)\n\n").arg(cited).arg(file);
      for (int number = first; number <= last; ++number)
        excerpt += QString("    %1 %2 %3\n").arg(number == cited ? ">" : " ").arg(number, 5).arg(lines.at(number - 1).mid(4));
      return excerpt;
    }
    return document;
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what())); return {}; }
}
QVariantMap Backend::readHandoff(const QString &session) const {
  if (!m_store || personalWiki()) return {};
  try { return m_store->handoff(session).toVariantMap(); }
  catch (const std::exception &) { return {}; }
}
void Backend::copyHandoff(const QString &text) {
  if (qobject_cast<QGuiApplication *>(QCoreApplication::instance()) && !text.isEmpty()) {
    QGuiApplication::clipboard()->setText(text);
    message(tr("Handoff copied."));
  }
}
bool Backend::saveHandoff(const QString &session, const QVariantMap &notes) {
  if (!m_store || personalWiki()) return false;
  try {
    m_store->saveHandoffNotes(session, QJsonObject::fromVariantMap(notes));
    message(tr("Handoff saved."));
    return true;
  } catch (const std::exception &error) {
    message(QString::fromUtf8(error.what()), true);
    return false;
  }
}
void Backend::chooseChatGptExport() {
  if (selecting() || busy() || capturing()) return;
  m_pickerPurpose = "chatgpt";
  emit statusChanged();
  m_picker->choose(false, tr("Choose conversations.json from a ChatGPT export"));
}
QVariantList Backend::previewChatGptExport(const QString &path) {
  if (!m_store || busy() || capturing()) return {};
  try { return m_store->importChatGpt(path, {}, true).value("conversations").toArray().toVariantList(); }
  catch (const std::exception &error) { message(QString::fromUtf8(error.what()), true); return {}; }
}
void Backend::importChatGptExport(const QString &path, const QString &conversation) {
  if (path.isEmpty() || conversation.isEmpty()) return;
  const QJsonObject input{{"path", path}, {"conversation", conversation}};
  work("chatgpt", QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)));
}
QString Backend::markdown(const QString &text, const QString &accent) const {
  // Reopening evidence or a long conversation reuses the converted page.
  const QByteArray key = QCryptographicHash::hash((accent + QChar(0) + text).toUtf8(), QCryptographicHash::Sha1);
  for (const auto &cached : m_rendered) if (cached.first == key) return cached.second;
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
  m_rendered.prepend({key, html}); while (m_rendered.size() > 12) m_rendered.removeLast();
  return html;
}
void Backend::reveal() { if (m_store) QDesktopServices::openUrl(QUrl::fromLocalFile(project())); }
void Backend::cancel() {
  if (!m_knowledgeJob.isEmpty()) {
    m_knowledgeCancelled = true;
    if (m_cancelWork) *m_cancelWork = true;
    if (modelRunning()) stopModel();
    else if (!m_work.isRunning()) finishKnowledge();
    return;
  }
  if (m_operation == "completion") { stopModel(); return; }
  if (m_operation == "skillBuild") { stopModel(); return; }
  m_whenIdle = nullptr;
  endBuild();
  if (!m_countdownKind.isEmpty()) {
    if (m_countdownKind == "build" && m_autoLearning) m_autoSnooze = QDateTime::currentDateTimeUtc().addSecs(1800);
    if (m_countdownKind == "verify") m_verifySnooze = QDateTime::currentDateTimeUtc().addSecs(1800);
    stopCountdown();
  }
  if (!m_verifyQueue.isEmpty() || m_verifyRunning) {
    if (m_verifyAuto) m_verifySnooze = QDateTime::currentDateTimeUtc().addSecs(1800);
    m_verifyAuto = false; m_verifyQueue.clear(); m_verifyRunning = false;
    message(tr("Verify all stopped: %1 of %2 memories checked.").arg(m_verifyDone).arg(m_verifyTotal));
    m_verifyTotal = 0;
  }
  if (m_autoLearning) { m_autoSnooze = QDateTime::currentDateTimeUtc().addSecs(1800); m_auto.stop(); }
  if (m_cancelWork) *m_cancelWork = true;
  if (busy()) { stopModel(); message(tr("Learning cancelled. Your captured data is safe.")); }
}
void Backend::learn(const QString &provider) {
  if (!m_store) return;
  if (modelRunning()) { deferUntilIdle([this, provider] { learn(provider); }); return; }
  if (!m_autoRun) { m_manualProvider = provider; m_buildDone = 0; m_buildRounds = 0; m_retried = false; }
  if (!startRound(provider)) m_manualProvider.clear();
  emit statusChanged();
}
bool Backend::startRound(const QString &provider) {
  if (provider != "codex" && provider != "claude" && provider != "local") { message(tr("Choose Codex, Claude, or a local model.")); emit learningFinished(false); return false; }
  if (provider == "local" && modelChoice("build", provider).value("model").toString().isEmpty()) { message(tr("Choose a local model before learning."), true); emit learningFinished(false); return false; }
  const auto tool = m_tools.command(provider);
  if (provider != "local" && !tool.available()) { message(tr("Set up %1 in Connections before learning.").arg(provider == "claude" ? "Claude Code" : "Codex"), true); emit learningFinished(false); return false; }
  try {
    const int reused = m_store->reusePublishedTranscriptLearning();
    if (reused > 0) {
      m_store->render();
      m_buildDone += reused;
      live(tr("Reused published transcript analysis for %n message(s).\n", "", reused));
    }
    const auto batch = m_store->learningBatch(provider == "local", m_batchLimit, m_tools.localInputScale()); m_batch = batch.value("events").toArray();
    if (m_batch.isEmpty()) {
      refresh();
      if (m_buildRounds > 0) message(tr("Wiki build finished: %1 messages learned in %2 rounds.").arg(m_buildDone).arg(m_buildRounds));
      else if (reused > 0) message(tr("Reused published transcript analysis for %n message(s).", "", reused));
      else message(tr("Nothing new to learn. Capture a conversation first."));
      emit learningFinished(true);
      return false;
    }
    m_operation="learn"; startModel(batch.value("prompt").toString(),provider);
    return true;
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what()), true); emit learningFinished(false); return false; }
}

void Backend::setLearningProvider(const QString &provider) {
  if (provider != "claude" && provider != "codex" && provider != "local") return;
  m_learningProvider = provider;
  m_autoProvider = provider; savePreferences(); emit statusChanged(); emit knowledgeChanged();
}
QVariantMap Backend::modelChoice(const QString &task, const QString &provider) const {
  const auto saved = m_modelOptions.value(task + "/" + provider).toMap();
  const bool local = provider == "local";
  const QString fallbackModel = local ? m_tools.localModel() : m_tools.agentModel(provider);
  const QString fallbackEffort = local ? QString() : m_tools.agentEffort(provider);
  const QString model = saved.value("model", fallbackModel).toString();
  const QString effort = saved.value("effort", fallbackEffort).toString();
  const QStringList levels = m_tools.effortLevels(provider, model);
  return {{"model", model}, {"effort", levels.contains(effort) ? effort : QString()},
    {"modelOverride", saved.value("model").toString()}, {"effortOverride", saved.value("effort").toString()},
    {"defaultModel", fallbackModel}, {"defaultEffort", fallbackEffort}, {"levels", levels}};
}
bool Backend::modelReady(const QString &task, const QString &provider) const {
  if (provider != "local") return m_tools.ready(provider);
  // A local model that is not running is covered by the backup, if one is
  // ready, once Lore has finished looking for local servers.
  return localReady(task) || (!m_tools.localChecking() && !m_tools.backupProvider().isEmpty());
}
// The chosen local model is running on Ollama.
bool Backend::localReady(const QString &task) const {
  const QString model = modelChoice(task, "local").value("model").toString();
  return m_tools.localModelReady(model);
}
void Backend::setModelChoice(const QString &task, const QString &provider, const QString &field, const QString &value) {
  static const QSet<QString> tasks{"build", "ask", "verify", "autoBuild", "autoVerify", "quick", "functions", "transcript"};
  if (!m_store || !tasks.contains(task) || !QStringList{"claude", "codex", "local"}.contains(provider)) return;
  const QString selected = value.trimmed(), key = task + "/" + provider;
  auto saved = m_modelOptions.value(key).toMap();
  if (field == "model") {
    if (!selected.isEmpty() && (provider == "local" ? !m_tools.localModelReady(selected) : !Toolbox::validAgentModel(selected))) {
      message(tr("Choose an available local model or enter a valid provider model name."), true);
      return;
    }
  } else if (field == "effort") {
    if (!selected.isEmpty() && !modelChoice(task, provider).value("levels").toStringList().contains(selected)) return;
  } else return;
  if (saved.value(field).toString() == selected) return;
  if (selected.isEmpty()) saved.remove(field); else saved[field] = selected;
  if (saved.isEmpty()) m_modelOptions.remove(key); else m_modelOptions[key] = saved;
  ++m_modelRevision; emit modelChoicesChanged();
  savePreferences(); emit statusChanged();
}
void Backend::setAnswerProvider(const QString &provider) {
  if (provider != "claude" && provider != "codex" && provider != "local") return;
  m_answerProvider = provider; savePreferences(); emit statusChanged();
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
  if (QFileInfo::exists(folder + "/.lore/brain.sqlite")) return openProjectMode(folder, false, m_personalSetup);
  const QString working = m_personalSetup ? folder : defaultWorkingDirectory(folder);
  if (QFileInfo::exists(working + "/.lore/brain.sqlite")) return openProject(working);
  m_workingDirectory = working;
  m_watch.stop(); m_auto.stop(); m_schedule.stop(); m_autoLearning = false; m_autoVerify = false; m_autoRun = false; m_verifyQueue.clear(); m_verifyAuto = false; m_store.reset(); m_connected = false; m_reviews.clear();
  m_selectedProject = folder; m_stats.clear(); m_sessions.clear(); m_memories.clear(); m_hiddenWikiEntries = 0; search({}); m_brain.clear();
  resetModelOutput();
  m_files.clear(); refreshKnowledge(); clearError(); emitAll(); emitLists(true); emit resultsChanged(); message(tr("Folder selected. Connect this project to start its memory.")); return true;
}
void Backend::chooseWorkingDirectory() {
  if (selecting() || m_store || m_selectedProject.isEmpty()) return;
  m_pickerPurpose = "working"; emit statusChanged(); m_picker->choose(true, tr("Choose where Lore keeps its files for this project"));
}
bool Backend::setWorkingDirectory(const QString &path) {
  if (m_store || m_selectedProject.isEmpty()) return false;
  QString folder = path.trimmed(); if (folder.startsWith("~/")) folder.replace(0, 1, QDir::homePath());
  folder = QFileInfo(folder).exists() ? QFileInfo(folder).canonicalFilePath() : QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
  if (folder.isEmpty()) return false;
  if (folder != m_selectedProject && Paths::contains(m_selectedProject, folder)) { message(tr("Choose a working directory outside the project folder, or use the project folder itself."), true); return false; }
  if (folder != m_selectedProject && Paths::contains(folder, m_selectedProject)) { message(tr("The project folder cannot be inside Lore's working directory."), true); return false; }
  m_workingDirectory = folder; clearError(); emitAll(); return true;
}
void Backend::chooseSettingsFolder(const QString &kind) {
  if (selecting() || !m_store || (kind != "source" && kind != "working")) return;
  if (busy() || capturing()) { message(tr("Wait for capture or learning to finish before changing folders."), true); return; }
  m_pickerPurpose = "settings-" + kind; emit statusChanged();
  m_picker->choose(true, kind == "source" ? tr("Choose the project folder Lore should remember") : tr("Choose where Lore keeps its files for this project"));
}
bool Backend::changeSource(const QString &path) {
  if (!m_store) return false;
  if (busy() || capturing()) { message(tr("Wait for capture or learning to finish before changing folders."), true); return false; }
  const QString folder = QFileInfo(path).canonicalFilePath(), previous = m_store->source();
  if (folder.isEmpty() || !QFileInfo(folder).isDir()) { message(tr("Choose an existing project folder."), true); return false; }
  if (folder == previous) return true;
  if (folder == project()) { message(tr("That is Lore's working directory. Choose your project folder."), true); return false; }
  if (QFileInfo::exists(folder + "/.lore/brain.sqlite")) { message(tr("That folder already has its own Lore memory. Use Switch project to open it."), true); return false; }
  try {
    m_store->setSource(folder);
    // The old folder must stop loading this memory into its sessions.
    if (previous != project()) Store::disconnectAgents(previous);
    else { Store::removeManaged(previous + "/AGENTS.md"); Store::removeManaged(previous + "/CLAUDE.md"); }
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); return false; }
  m_selectedProject = m_store->source(); connectAgents();
  if (!m_connected) return false;
  message(tr("Project folder changed. Lore is re-indexing its files; memories linked to old files are flagged for checking."));
  work("index", {}, true); return true;
}
bool Backend::changeWorkingDirectory(const QString &path) {
  if (!m_store) return false;
  if (busy() || capturing()) { message(tr("Wait for capture or learning to finish before changing folders."), true); return false; }
  const QString source = m_store->source(), previous = project();
  const bool watched = watching(); m_watch.stop(); m_auto.stop();
  // Copy, check the copy, then switch to it. The old copy is removed only
  // after the new one opens with the same records.
  QString target;
  try { target = m_store->migrationTarget(path); if (target == previous) { if (watched) m_watch.start(); return true; } m_store->copyConnectedTo(target, QCoreApplication::applicationFilePath()); }
  catch (const std::exception &e) { if (watched) m_watch.start(); message(QString::fromUtf8(e.what()), true); return false; }
  const QVariant options = m_projectOptions.take(previous); if (options.isValid()) m_projectOptions[target] = options;
  m_recent.removeAll(previous);
  m_store.reset();
  if (!openProject(target) || !m_connected) {
    openProject(previous); if (watched) m_watch.start();
    message(tr("The destination could not be opened as a connected project. Both archive copies were kept."), true);
    return false;
  }
  if (Store::removeArchive(previous, source)) message(tr("Lore's files moved to %1.").arg(QDir::toNativeSeparators(target)));
  else message(tr("Lore's files moved to %1. The old copy at %2 is still in use; close other Lore windows and delete it later.").arg(QDir::toNativeSeparators(target), QDir::toNativeSeparators(previous + "/.lore")), true);
  return true;
}
bool Backend::connectProject() {
  if (m_selectedProject.isEmpty() || capturing() || busy()) return false;
  if (!m_store) {
    if (m_personalSetup) {
      if (!openProjectMode(m_selectedProject, true, true)) return false;
      reconnectPersonalHooks();
      if (m_connected) sync();
      return m_connected;
    }
    const QString source = m_selectedProject, working = m_workingDirectory.isEmpty() ? source : m_workingDirectory;
    if (!setWorkingDirectory(working)) return false;
    if (!QDir().mkpath(working)) { message(tr("Cannot create the working directory %1.").arg(QDir::toNativeSeparators(working)), true); return false; }
    if (!openProject(working, true)) return false;
    try { m_store->setSource(source); m_selectedProject = m_store->source(); emitAll(); }
    catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); return false; }
  }
  if (personalWiki()) { reconnectPersonalHooks(); if (m_connected) sync(); return m_connected; }
  connectAgents(); if (!m_connected) return false;
  sync(); return true;
}
bool Backend::connectionValid() const {
  if (!m_store) return false;
  try {
    if (personalWiki()) return m_store->setting("personalHooksReady") == "on";
    const auto hooks = QJsonDocument::fromJson(Store::read(source() + "/.claude/settings.local.json").toUtf8()).object().value("hooks").toObject();
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
    return Store::read(source() + "/CLAUDE.md").contains("Read BRAIN.md") && Store::read(source() + "/AGENTS.md").contains("Read BRAIN.md");
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
    auto options = m_projectOptions.value(project()).toMap();
    options["modelChoices"] = m_modelOptions;
    options.insert(QVariantMap{{"provider", m_learningProvider}, {"answerProvider", m_answerProvider}, {"verifyProvider", m_verifyProvider}, {"autoBuildDelay", m_autoBuildDelay}, {"autoVerifyDelay", m_autoVerifyDelay}, {"autoVerify", m_autoVerify}, {"autoVerifyProvider", m_autoVerifyProvider}, {"autoVerifyAfter", m_autoVerifyAfter}, {"autoVerifyEvery", m_autoVerifyEvery}, {"lastAutoVerify", m_lastAutoVerify}, {"watching", watching()}, {"autoLearning", autoLearning()}, {"autoProvider", m_autoProvider}, {"autoAfter", m_autoAfter}, {"autoEvery", m_autoEvery}, {"lastAutoBuild", m_lastAutoBuild},
      {"source", m_store->source()}, {"name", QFileInfo(m_store->source()).fileName()}, {"conversations", m_stats.value("sources").toInt()}, {"entries", m_stats.value("memories").toInt()}});
    m_projectOptions[project()] = options;
  }
  try { Store::write(m_preferences, QJsonDocument(QJsonObject{{"notificationsEnabled",m_notificationsEnabled}, {"wikiAttentionOnly",m_memoryModel.attentionOnly()}, {"recent", QJsonArray::fromStringList(m_recent)}, {"projects", QJsonObject::fromVariantMap(m_projectOptions)}, {"hiddenProjects", QJsonArray::fromStringList(m_hiddenProjects)}, {"prompts", m_legacyPrompts}, {"quickCommands", QJsonArray::fromVariantList(m_customCommands)}, {"quickAgent", m_quickAgent}, {"usage", QJsonObject::fromVariantMap(m_totalUsage)}, {"tools", QJsonObject::fromVariantMap(m_tools.overrides())}}).toJson()); }
  catch (const std::exception &e) { message(tr("Your project is saved, but app preferences could not be remembered: %1").arg(QString::fromUtf8(e.what())), true); }
  emit changed(); emit projectsChanged();
}
void Backend::restorePreferences() {
  if (m_preferences.isEmpty() || !QFileInfo::exists(m_preferences)) return;
  m_restoring = true;
  try {
    const auto saved = QJsonDocument::fromJson(Store::read(m_preferences).toUtf8()).object();
    m_notificationsEnabled=saved.value("notificationsEnabled").toBool(true);
    m_memoryModel.setAttentionOnly(saved.value("wikiAttentionOnly").toBool(false));
    for (const auto &path : saved.value("recent").toArray()) if (path.isString()) m_recent.append(path.toString());
    m_projectOptions = saved.value("projects").toObject().toVariantMap();
    for (const auto &path : saved.value("hiddenProjects").toArray()) if (path.isString()) m_hiddenProjects.append(path.toString());
    // Older preferences only listed recent working directories.
    for (const auto &path : m_recent) if (!m_projectOptions.contains(path)) m_projectOptions[path] = QVariantMap();
    m_tools.setOverrides(saved.value("tools").toObject().toVariantMap());
    m_legacyPrompts = saved.value("prompts").toArray();
    m_totalUsage = saved.value("usage").toObject().toVariantMap();
    m_customCommands = saved.value("quickCommands").toArray().toVariantList(); m_quickAgent = saved.value("quickAgent").toString("claude");
    if (!m_recent.isEmpty()) openProject(m_recent.first());
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
  m_restoring = false;
}

void Backend::resetModelOutput() {
  m_liveFlush.stop();
  m_live.clear();
  m_livePending.clear();
  m_liveIssues.clear();
  m_output.clear(); m_error.clear(); m_liveLine.clear();
  emit liveChanged();
}

void Backend::styleTerminal(QQuickTextDocument *document, const QColor &accent,
                            const QColor &warning, const QColor &caution) {
  if (!document) return;
  auto *text = document->textDocument();
  auto *highlighter = static_cast<TerminalHighlighter *>(text->findChild<QSyntaxHighlighter *>("terminalHighlighter"));
  if (!highlighter) highlighter = new TerminalHighlighter(text);
  highlighter->setColors(accent, warning, caution);
}

QString Backend::modelTaskLabel() const {
  if (m_operation == "learn") return tr("Building the wiki");
  if (m_operation == "ask") return tr("Answering a question");
  if (m_operation == "skillBuild") return tr("Drafting a skill");
  if (m_operation == "verify") return tr("Verifying memory %1").arg(m_verifyId);
  if (m_operation == "completion") return tr("Checking completion of memory %1").arg(m_verifyId);
  if (m_operation == "functions") return tr("Scanning functions · %1").arg(m_knowledgeChunk.value("path").toString());
  if (m_operation == "transcript") return tr("Analyzing transcript · %1").arg(m_knowledgeChunk.value("title").toString());
  return tr("Running model");
}

void Backend::live(const QString &text) {
  if (text.isEmpty()) return;
  m_live += text; m_livePending += text;
  if (m_live.size() > 300000) { m_live.remove(0, m_live.size() - 250000); const int line = m_live.indexOf('\n'); if (line >= 0) m_live.remove(0, line + 1); }
  if (!m_liveFlush.isActive()) m_liveFlush.start();
}
void Backend::liveIssue(const QString &text, const QString &kind) {
  const QString prefix = kind == "error" ? QStringLiteral("× ") : QStringLiteral("! ");
  const QString marked = prefix + text;
  m_liveIssues.append(QVariantMap{{"kind", kind}, {"text", marked}});
  if (m_liveIssues.size() > 500) m_liveIssues.removeFirst();
  live('\n' + marked + '\n');
}
// The words the model writes, as they arrive: Claude Code's text and
// thinking deltas, Codex's finished items, and a local model's tokens.
void Backend::liveLines(const QByteArray &chunk) {
  m_liveLine += chunk;
  for (int end = m_liveLine.indexOf('\n'); end >= 0; end = m_liveLine.indexOf('\n')) {
    const QByteArray line = m_liveLine.left(end); m_liveLine.remove(0, end + 1);
    const auto o = QJsonDocument::fromJson(line).object(); if (o.isEmpty()) continue;
    if (o.value("message").isObject() && !o.contains("type")) {  // Ollama
      const auto m = o.value("message").toObject();
      live(m.value("thinking").toString()); live(m.value("content").toString());
      m_localContent += m.value("content").toString().toUtf8();
      // A model stuck in a loop repeats the same passage: stop it early
      // rather than let it run to the token limit or the timeout.
      const QString said = m.value("thinking").toString() + m.value("content").toString();
      m_loopWindow += said; m_loopSeen += said.size();
      if (m_loopWindow.size() > 8000) m_loopWindow.remove(0, m_loopWindow.size() - 8000);
      if (m_loopSeen - m_loopChecked >= 400 && m_loopWindow.size() >= 2000 && !m_loopStopped) {
        m_loopChecked = m_loopSeen;
        const QString tail = m_loopWindow.right(150);
        if (!tail.trimmed().isEmpty() && m_loopWindow.count(tail) >= 4) { m_loopStopped = true; live(tr("\n[Lore stopped the model: it kept repeating the same text.]\n")); if (m_reply) m_reply->abort(); }
      }
      continue;
    }
    const QString type = o.value("type").toString();
    if (type == "stream_event") {
      const auto delta = o.value("event").toObject().value("delta").toObject();
      const QString text = delta.value("text").toString();
      if (!text.isEmpty()) m_modelTextStreamed = true;
      live(text); live(delta.value("thinking").toString());
    } else if (type == "item.completed" || type == "item.started") {
      const auto item = o.value("item").toObject(); const QString kind = item.value("type").toString();
      if (type == "item.completed" && (kind == "agent_message" || kind == "reasoning")) live(item.value("text").toString() + "\n");
      else if (type == "item.started" && kind == "command_execution") live("$ " + item.value("command").toString() + "\n");
    } else if (type == "result" && o.value("is_error").toBool()) live("\n" + o.value("result").toString() + "\n");
  }
}
QString Backend::activeProvider() const {
  return m_provider.startsWith("local:") ? QStringLiteral("local") : m_provider;
}
QVariantMap Backend::activeModelCall() const {
  if (!modelRunning()) return {};
  return {{"provider", activeProvider()}, {"model", m_activeModel}, {"effort", m_activeEffort}, {"task", modelTaskLabel()}};
}
// What the call cost, as the agent reported it: Claude Code's result usage
// (input includes cache reads and writes), Codex's turn usage, Ollama's
// prompt and answer counts, or an OpenAI-style usage block.
void Backend::recordUsage() {
  qint64 input = 0, output = 0, window = 0;
  const QString agent = activeProvider();
  double generationNs = 0;
  for (const auto &line : m_output.split('\n')) {
    const auto o = QJsonDocument::fromJson(line).object(); if (o.isEmpty()) continue;
    if (agent == "claude" && o.contains("usage") && o.value("type").toString() == "result") {
      const auto u = o.value("usage").toObject();
      input = qint64(u.value("input_tokens").toDouble() + u.value("cache_creation_input_tokens").toDouble() + u.value("cache_read_input_tokens").toDouble()); output = qint64(u.value("output_tokens").toDouble());
      for (const auto &m : o.value("modelUsage").toObject()) if (m.toObject().value("contextWindow").toDouble() > 0) window = qint64(m.toObject().value("contextWindow").toDouble());
    } else if (m_provider == "codex" && o.value("type").toString() == "turn.completed") {
      const auto u = o.value("usage").toObject(); input += qint64(u.value("input_tokens").toDouble()); output += qint64(u.value("output_tokens").toDouble());
    } else if (agent == "local" && o.value("done").toBool()) {
      input = qint64(o.value("prompt_eval_count").toDouble()); output = qint64(o.value("eval_count").toDouble()); window = m_localContext;
      generationNs = o.value("eval_duration").toDouble();
    } else if (agent == "local" && o.contains("usage")) {
      input = qint64(o.value("usage").toObject().value("prompt_tokens").toDouble()); output = qint64(o.value("usage").toObject().value("completion_tokens").toDouble());
    }
  }
  if (input == 0 && output == 0) return;
  const double durationNs = generationNs > 0 ? generationNs : double(m_modelTime.nsecsElapsed());
  const auto add = [&](QVariantMap &book) {
    auto u = book.value(agent).toMap();
    u["calls"] = u.value("calls").toLongLong() + 1; u["input"] = u.value("input").toLongLong() + input; u["output"] = u.value("output").toLongLong() + output;
    u["lastInput"] = input; u["lastOutput"] = output; if (window > 0) u["window"] = window;
    if (durationNs > 0) u["lastTokensPerSecond"] = output * 1e9 / durationNs;
    else u.remove("lastTokensPerSecond");
    if (!u.contains("since")) u["since"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    book[agent] = u;
  };
  add(m_sessionUsage); add(m_totalUsage); emit usageChanged();
  live(tr("\ntokens: %1 in · %2 out").arg(QLocale().toString(input)).arg(QLocale().toString(output)) + (window > 0 ? tr(" · context %1% of %2").arg(qMin<qint64>(100, input * 100 / window)).arg(QLocale().toString(window)) : QString()));
  savePreferences();
}
void Backend::finishModel(QString answer, const QString &failure) {
  m_timeout.stop(); bool ok = false, retry = false;
  if (!m_liveLine.isEmpty()) liveLines("\n");
  if (m_functionTimedOut && m_knowledgeJob == "functions" && !m_knowledgeCancelled) {
    const QString reason = tr("The model timed out before returning a function list.");
    live(QStringLiteral("\n× %1 · %2 · %3\n").arg(terminalTime(),
      m_knowledgeChunk.value("path").toString(), reason));
    retryFunctionChunk(reason);
    return;
  }
  if (m_provider == "claude" && !m_modelTextStreamed && !m_cancelled && failure.isEmpty() && !answer.isEmpty())
    live(answer + '\n');
  if (failure.isEmpty() && !m_cancelled) recordUsage();
  if (!m_cancelled && !failure.isEmpty() && m_knowledgeJob.isEmpty())
    liveIssue(tr("%1 · failed: %2").arg(terminalTime(), failure.left(300)), "error");
  else
    live(QString("\n── %1 · %2 ──\n").arg(terminalTime(), m_cancelled ? tr("cancelled") : !failure.isEmpty() ? tr("failed: %1").arg(failure.left(300)) : tr("finished")));
  try {
    if (!m_knowledgeJob.isEmpty() && (m_knowledgeCancelled || m_cancelled)) {
      finishKnowledge(m_knowledgeCancelled ? QString() : tr("The analysis model timed out."));
      return;
    }
    if (m_cancelled && m_operation == "skillBuild") { finishSkill({}, tr("Skill builder stopped.")); return; }
    if (m_cancelled && m_operation == "completion") {
      m_operation.clear();
      message(tr("Completion check stopped."));
      QTimer::singleShot(0, this, &Backend::runDeferred);
      return;
    }
    if (m_cancelled) {
      endBuild();
      if (m_operation == "verify" && m_verifyRunning) { ++m_verifyFailed; m_verifyHadError = true; QTimer::singleShot(0, this, &Backend::continueVerify); }
      emit statusChanged(); emit learningFinished(false); return;
    }
    if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
    // Reasoning models (local ones especially) may think aloud or wrap the
    // JSON in prose or a code fence; keep only the JSON object.
    answer.remove(QRegularExpression("<think>.*?</think>", QRegularExpression::DotMatchesEverythingOption));
    answer = answer.trimmed();
    if (answer.startsWith("```")) { answer = answer.mid(answer.indexOf('\n') + 1); answer = answer.left(answer.lastIndexOf("```")); }
    answer = answer.trimmed();
    if (!answer.startsWith('{') && answer.indexOf('{') >= 0) answer = answer.mid(answer.indexOf('{'), answer.lastIndexOf('}') - answer.indexOf('{') + 1);
    QJsonParseError parseError; const auto result = QJsonDocument::fromJson(answer.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !result.isObject()) {
      const QString start = answer.isEmpty() ? QStringLiteral("an empty reply") : "“" + answer.left(200).simplified() + (answer.size() > 200 ? "…" : "") + "”";
      throw std::runtime_error((m_operation == "learn" ? tr("Model did not return valid memory JSON (it returned %1). No messages were marked learned.") : tr("The model did not return valid JSON (it returned %1).")).arg(start).toStdString());
    }
    if (!m_knowledgeJob.isEmpty()) { acceptKnowledgeResult(result.object()); return; }
    if (m_operation == "skillBuild") { finishSkill(result.object()); return; }
    if(m_operation=="ask") { m_answer=m_store->saveAnswer(result.object(),m_answerEvidence,m_question).toVariantMap(); refresh(); message(tr("Answer ready. Open its citations to check the evidence.")); }
    else if (m_operation == "completion") {
      m_store->saveVerification(m_verifyId, result.object(), m_verifyEvidence, m_provider, true);
      refresh();
      const auto verdict = result.object().value("verdict").toString();
      message(verdict == "complete" ? tr("Completion check: complete.") :
        verdict == "incomplete" ? tr("Completion check: incomplete.") : tr("Completion check: unclear."));
    }
    else if (m_operation == "verify") {
      m_store->saveVerification(m_verifyId, result.object(), m_verifyEvidence, m_provider);
      if (m_verifyRunning) ++m_verifyDone;
      refresh();
      if (m_semantic.value("enabled").toBool()) m_semanticSoon.start();
      message(m_verifyRunning ? tr("Checked %1 of %2 memories awaiting review…").arg(m_verifyDone + m_verifyFailed).arg(m_verifyTotal)
        : tr("Check saved. Lore can use it in future answers and wiki builds while its cited evidence is current."));
    }
    else { m_store->learn(result.object(),m_batch,m_provider); m_buildDone += int(m_batch.size()); ++m_buildRounds; m_retried = false; m_batchLimit = 0; refresh(); message(tr("Wiki updated with source references.")); }
    ok = true;
  } catch(const std::exception &e) {
    if (!m_knowledgeJob.isEmpty()) {
      const QString error = QString::fromUtf8(e.what());
      if (m_knowledgeJob == "functions" && failure.isEmpty()) retryFunctionChunk(error);
      else finishKnowledge(error);
      return;
    }
    if (failure.isEmpty()) liveIssue(QString::fromUtf8(e.what()), "error");
    if (m_operation == "skillBuild") { finishSkill({}, QString::fromUtf8(e.what())); return; }
    // One failed round in a build is retried before the build stops.
    const bool unattended = m_operation=="learn" && (m_autoRun || !m_manualProvider.isEmpty());
    if (unattended && !m_retried) { m_retried = true; retry = true; message(tr("A wiki round failed (%1). Retrying once…").arg(QString::fromUtf8(e.what()).left(300))); }
    // A local model is meant to work through everything on its own: a batch
    // that fails twice is halved, and a single message that still fails is
    // skipped (it stays in the archive), so the build keeps going.
    else if (unattended && m_provider.startsWith("local") && m_batch.size() > 1) {
      m_batchLimit = int(m_batch.size()) / 2; m_retried = false; retry = true;
      message(tr("A wiki round failed twice; trying a smaller batch of %1 messages.").arg(m_batchLimit));
    } else if (unattended && m_provider.startsWith("local") && m_batch.size() == 1) {
      try {
        m_store->skipMessages(m_batch);
        m_buildDone += 1;
        m_batchLimit = 0; m_retried = false; retry = true; refresh();
        message(tr("Skipped one message the local model could not process. You can queue it again from the wiki."));
      } catch (const std::exception &error) { message(QString::fromUtf8(error.what()), true); }
    }
    else message(QString::fromUtf8(e.what()), true);
  }
  // A build (a Build wiki press or an automatic one) keeps going until nothing
  // is waiting; Cancel, a second failure, or a changed model stops it.
  const bool learning = m_operation=="learn", building = m_autoRun || !m_manualProvider.isEmpty();
  if (m_operation == "completion") m_operation.clear();
  const bool more = ok && learning && m_stats.value("pending").toInt() > 0;
  if (learning && building && (more || retry)) QTimer::singleShot(retry ? 1500 : 0, this, &Backend::continueBuild);
  else if (learning && building) {
    if (ok) message(tr("Wiki build finished: %1 messages learned in %2 rounds.").arg(m_buildDone).arg(m_buildRounds));
    endBuild();
  }
  if(ok && learning && !more && m_semantic.value("enabled").toBool()) QTimer::singleShot(0,this,[this] { if(!busy() && !capturing()) work("semantic",{},true); });
  // Verify all goes on to the next memory, whether this check worked or not.
  if(m_operation=="verify" && m_verifyRunning) { if(!ok) { ++m_verifyFailed; m_verifyHadError = true; } QTimer::singleShot(200,this,&Backend::continueVerify); }
  QTimer::singleShot(0, this, &Backend::runDeferred);
  emit statusChanged(); emit learningFinished(ok);
}
void Backend::stopModel() {
  m_cancelled = true; m_manualProvider.clear();
  if (m_process.state() != QProcess::NotRunning) m_process.kill();
  if (m_reply) m_reply->abort();
}
// Local models run on Ollama (or a server with Ollama's API). Its chat API
// takes a context size (its default is too small for a learning batch) and
// constrains output to JSON.
void Backend::startLocalModel(const QString &prompt) {
  // The Ollama server that has this model.
  const QUrl base = Toolbox::loopback(m_tools.chatServer(m_activeModel));
  if (!base.isValid()) throw std::runtime_error(tr("No Ollama server has the model %1 right now. Start Ollama, or click Find models in Settings.").arg(m_activeModel).toStdString());
  const QJsonArray messages{QJsonObject{{"role","user"},{"content",prompt}}};
  // Tokens stream, so the Model output page shows them as they come.
  QJsonObject body{{"model",m_activeModel},{"messages",messages},{"stream",true}};
  // Learning output has a fixed shape; a schema keeps small models to it.
  const QJsonObject strings{{"type","array"},{"items",QJsonObject{{"type","string"}}}}, numbers{{"type","array"},{"items",QJsonObject{{"type","integer"}}}};
  const QJsonObject text{{"type","string"}};
  const QJsonObject fields{{"title",text},{"kind",QJsonObject{{"type","string"},{"enum",QJsonArray{"fact","decision","next","question"}}}},{"body",text},{"refs",strings},{"files",strings},{"supersedes",numbers},{"conflicts",numbers},{"confidence",QJsonObject{{"type","integer"},{"minimum",0},{"maximum",100}}}};
  const QJsonObject item{{"type","object"},{"required",QJsonArray{"title","kind","body","refs"}},{"properties",fields}};
  const QJsonObject schema{{"type","object"},{"required",QJsonArray{"entries"}},{"properties",QJsonObject{{"entries",QJsonObject{{"type","array"},{"items",item}}}}}};
  const bool learning = m_operation == "learn";
  const bool asking = m_operation == "ask";
  const bool scanningFunctions = m_operation == "functions";
  // Ask sizes the evidence to its selected window. Other jobs size the
  // window to their prompt and leave room for the answer.
  const int answerRoom = scanningFunctions ? 8192 : 4096;
  // A cap on the answer (reasoning included), so a model stuck in a loop
  // stops instead of running until the timeout.
  const int answerLimit = asking ? Store::localAnswerTokens : learning || scanningFunctions ? 8000 : 4000;
  const int estimated = qBound(8192, ((prompt.size() / 3 + answerRoom) / 4096 + 1) * 4096, 131072);
  const int context = asking ? m_askContext : qMax(estimated, m_tools.localContextTokens());
  m_localContext = context;
  if (scanningFunctions) body["format"] = Store::functionScanSchema();
  else body["format"] = learning ? QJsonValue(schema) : QJsonValue("json");
  body["options"] = QJsonObject{{"num_ctx",context},{"temperature",0.2 + 0.25 * m_loopRetries},{"num_predict",answerLimit},{"repeat_penalty",1.15},{"repeat_last_n",256}};
  // gpt-oss reasons before answering; keep that short so it does not wander.
  if (m_activeModel.startsWith("gpt-oss")) body["think"] = m_activeEffort.isEmpty() ? QString("low") : m_activeEffort;
  QNetworkRequest request(base.resolved(QUrl("/api/chat")));
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  clearError();
  QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact)); m_reply = reply;
  connect(reply, &QNetworkReply::readyRead, this, [this, reply] { if (reply->error() != QNetworkReply::NoError) return; const QByteArray chunk = reply->readAll(); m_output += chunk; liveLines(chunk); });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    reply->deleteLater(); m_reply = nullptr;
    QByteArray bytes = reply->isOpen() ? reply->readAll() : QByteArray();
    if (reply->error() == QNetworkReply::NoError) {
      m_output += bytes; liveLines(bytes + "\n");
      // Streamed lines were collected as they came; a reply sent whole (some
      // servers ignore stream) is read as one object.
      if (!m_localContent.isEmpty()) bytes = QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", QString::fromUtf8(m_localContent)}}}}).toJson();
      else { bytes = m_output; live(QJsonDocument::fromJson(bytes).object().value("message").toObject().value("content").toString() + "\n"); }
    }
    if (m_loopStopped && !m_cancelled && m_loopRetries < 3) {
      // Start over with a clean request that points the model back at the task.
      const int attempt = ++m_loopRetries;
      live(tr("[Starting again, attempt %1 of 3.]\n").arg(attempt));
      QTimer::singleShot(0, this, [this] {
        m_loopRetry = true;
        try { startModel(m_lastPrompt + "\n\nNote: an earlier attempt at this task got stuck repeating the same text. Start fresh: read the task and the supplied material above again, keep your reasoning short, and return only the required JSON.", "local"); }
        catch (const std::exception &e) { m_loopRetry = false; finishModel({}, QString::fromUtf8(e.what())); return; }
        m_loopRetry = false;
      });
      return;
    }
    if (m_loopStopped) { finishModel({}, tr("The local model got stuck repeating itself three times, so Lore stopped it. Try a larger local model, or Claude Code or Codex, for this.")); return; }
    const auto o = QJsonDocument::fromJson(bytes).object();
    if (m_cancelled) { finishModel({}, {}); return; }
    // The server went away mid-call, or is too busy to take the call (429,
    // 503): continue once with the backup.
    const auto lost = reply->error();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (m_knowledgeJob.isEmpty() && !m_cancelled && !m_backupTried && (lost == QNetworkReply::ConnectionRefusedError || lost == QNetworkReply::RemoteHostClosedError
        || lost == QNetworkReply::HostNotFoundError || lost == QNetworkReply::TemporaryNetworkFailureError || status == 429 || status == 503)
        && !m_tools.backupProvider().isEmpty()) {
      const QString backup = m_tools.backupProvider();
      m_backupTried = true; m_tools.rescanLocal();
      live(tr("[The local model stopped answering; continuing with %1.]").arg(backup == "claude" ? QStringLiteral("Claude Code") : QStringLiteral("Codex")) + QChar('\n'));
      QTimer::singleShot(0, this, [this, backup] {
        m_fallingBack = true;
        try { startModel(m_lastPrompt, backup); } catch (const std::exception &e) { m_fallingBack = false; finishModel({}, QString::fromUtf8(e.what())); return; }
        m_fallingBack = false;
      });
      return;
    }
    if (reply->error() != QNetworkReply::NoError && !m_loopStopped) {
      const QString detail = o.value("error").isObject() ? o.value("error").toObject().value("message").toString() : o.value("error").toString();
      finishModel({}, tr("The local model failed: %1. Check that Ollama is running. %2").arg(reply->errorString(), detail.left(400))); return;
    }
    finishModel(o.value("message").toObject().value("content").toString(), {});
  });
  // Local models on a CPU can take minutes for one batch, and a whole
  m_timeout.start(900000);
  if (!m_knowledgeJob.isEmpty()) {
    message(m_knowledgeJob == "functions" ? tr("Documenting source functions…") : tr("Analyzing saved conversation turns…"));
    return;
  }
  if (m_operation == "skillBuild") { message(tr("Working on your skill with %1…").arg(m_activeModel)); return; }
  if (m_operation == "completion") {
    message(tr("Checking completion with %1…").arg(m_activeModel));
    return;
  }
  message(m_operation=="ask" ? tr("Answering from project evidence with the local model %1…").arg(m_activeModel) : m_operation=="verify" ? tr("Checking this memory with the local model %1…").arg(m_activeModel) : tr("Learning with the local model %1 on this computer…").arg(m_activeModel));
}
void Backend::startModel(const QString &prompt,const QString &requested) {
  if (modelRunning()) throw std::runtime_error(tr("A model call is still running. Try again when it finishes.").toStdString());
  m_functionTimedOut = false;
  m_modelTime.start();
  m_modelTextStreamed = false;
  m_liveLine.clear(); m_localContent.clear(); m_loopWindow.clear(); m_loopSeen = 0; m_loopChecked = 0; m_loopStopped = false;
  const QString task = m_operation == "learn" ? QStringLiteral("build")
    : m_operation == "verify" && m_verifyAuto ? "autoVerify"
    : m_operation == "skillBuild" ? "ask"
    : m_operation == "completion" ? "verify"
    : m_operation;
  QString provider = requested;
  if (!m_fallingBack) m_backupTried = false;
  // Local first; Claude Code or Codex stand in when the local model is not running.
  if (provider == "local" && !m_loopRetry && m_tools.localScanned() && !localReady(task)) {
    const QString backup = m_knowledgeJob.isEmpty() ? m_tools.backupProvider() : QString();
    if (backup.isEmpty()) throw std::runtime_error(tr("The local model is not running. Start Ollama, or choose a backup in Settings › AI & models.").toStdString());
    live(tr("\n[The local model is not running; using %1 as the backup.]\n").arg(backup == "claude" ? QStringLiteral("Claude Code") : QStringLiteral("Codex")));
    provider = backup; m_backupTried = true;
    m_tools.rescanLocal();
  }
  if (!m_loopRetry) {
    m_lastPrompt = prompt; m_loopRetries = 0;
    const auto choice = modelChoice(task, provider);
    m_activeModel = choice.value("model").toString();
    m_activeEffort = choice.value("effort").toString();
  }
  const QString selected = provider + (m_activeModel.isEmpty() ? QString() : ":" + m_activeModel)
    + (m_activeEffort.isEmpty() ? QString() : " (" + tr("%1 effort").arg(m_activeEffort) + ")");
  live(QString("\n── %1 · %2 · %3 · %4 ──\n").arg(terminalTime(), modelTaskLabel(), selected, tr("prompt %1 characters").arg(prompt.size())));
  if (provider == "local") { m_provider="local:"+m_activeModel; m_output.clear(); m_error.clear(); m_cancelled=false; startLocalModel(prompt); return; }
  const QString runner = provider;
  const auto tool=m_tools.command(runner);
  if(!tool.available()) throw std::runtime_error("Set up your selected coding tool in Connections.");
  m_provider=runner; m_output.clear(); m_error.clear(); m_cancelled=false;
  m_process.setWorkingDirectory(QDir::tempPath());
  QStringList args;
  if(provider=="codex") {
    args={"exec","--ephemeral","--skip-git-repo-check","--sandbox","read-only","--json"};
    if (m_operation == "skillBuild" || m_operation == "completion" || !m_knowledgeJob.isEmpty())
      args << "--ignore-user-config" << "-c" << "approval_policy=\"never\"" << "-c" << "features.shell_tool=false" << "-c" << "web_search=\"disabled\"";
  } else args={"-p","--output-format","stream-json","--verbose","--include-partial-messages","--tools","","--strict-mcp-config","--mcp-config","{\"mcpServers\":{}}","--no-session-persistence","--setting-sources",""};
  args += Toolbox::agentArguments(runner, m_activeModel, m_activeEffort);
  if (runner == "codex") args << "-";
  clearError(); m_process.start(tool.program,tool.arguments+args); m_process.write(prompt.toUtf8()); m_process.closeWriteChannel();
  if (!m_knowledgeJob.isEmpty()) {
    m_timeout.start(m_knowledgeJob == "functions" ? 600000 : 180000);
    message(m_knowledgeJob == "functions" ? tr("Documenting source functions…") : tr("Analyzing saved conversation turns…"));
    return;
  }
  if (m_operation == "skillBuild") { m_timeout.start(180000); message(tr("Working on your skill…")); return; }
  if (m_operation == "completion") {
    m_timeout.start(180000);
    message(tr("Checking whether the work is complete…"));
    return;
  }
  m_timeout.start(180000); message(m_operation=="ask" ? tr("Answering from project evidence using %1…").arg(provider) : m_operation=="verify" ? tr("Checking this memory against project files using %1…").arg(provider) : tr("Learning through %1 using your existing login…").arg(provider));
}
void Backend::work(const QString &kind,const QString &query,bool quiet) {
  if(!m_store) return;
  if (!m_knowledgeJob.isEmpty() && kind != "functionPlan" && kind != "transcriptPlan") {
    if (!quiet) message(tr("Wait for the current analysis to finish, or stop it."));
    return;
  }
  if (personalWiki() && kind != "review" && kind != "deleteWiki" && kind != "forgetConversations" && kind != "deleteForgottenConversations" && kind != "retryLearning") {
    if (!quiet) message(tr("This action is available in project mode."), true);
    return;
  }
  if(m_work.isRunning() || capturing()) { if(!quiet) deferUntilIdle([this,kind,query] { work(kind,query); }); return; }
  const QString root=project(); m_workKind=kind; m_quietWork=quiet; m_cancelWork=std::make_shared<std::atomic_bool>(false); const auto cancel=m_cancelWork;
  m_progress=std::make_shared<Store::Progress>(); const auto progress=m_progress; m_semanticView.clear();
  if(kind=="download" || kind=="semantic") m_progressPoll.start();
  const bool queuedCheck = kind == "verify" && m_verifyRunning;
  const bool firstCheck = queuedCheck && m_verifyDone + m_verifyFailed == 0;
  const bool localEvidence = (kind == "verify" && verifyModel() == "local") || (kind == "completion" && m_verifyProvider == "local");
  const int inputScale = localEvidence ? m_tools.localInputScale() : 1;
  const QString answerProvider = m_answerProvider;
  const int askContext = kind == "ask" && answerProvider == "local" ? m_tools.localAskContextTokens() : 0;
  m_work.setFuture(QtConcurrent::run([root,kind,query,cancel,progress,queuedCheck,firstCheck,inputScale,askContext,answerProvider] {
    try {
      Store store(root); store.setCancellation(cancel); store.setProgress(progress);
      const auto job=[&]() -> QJsonObject {
      if (kind == "functionPlan") {
        store.indexProject();
        const auto args = QJsonDocument::fromJson(query.toUtf8()).object();
        return {{"queue", store.functionPlan(args.value("rerun").toBool())}};
      }
      if (kind == "transcriptPlan") {
        const auto args = QJsonDocument::fromJson(query.toUtf8()).object();
        return {{"queue", store.transcriptPlan(args.value("source").toString(), args.value("rerun").toBool())}};
      }
      if (kind == "forgetConversations") { store.forgetConversations(query); return {}; }
      if (kind == "deleteForgottenConversations") { store.deleteForgottenConversations(); return {}; }
      if (kind == "retryLearning") return {{"queued", store.retrySkippedLearning(query)}};
      if (kind == "chatgpt") {
        const auto args = QJsonDocument::fromJson(query.toUtf8()).object();
        return store.importChatGpt(args.value("path").toString(), args.value("conversation").toString());
      }
      if (kind == "reviewTranscript") {
        const auto args = QJsonDocument::fromJson(query.toUtf8()).object();
        store.reviewTranscriptFinding(args.value("id").toString(), args.value("accept").toBool());
        return {};
      }
      if(kind=="index") return store.indexProject();
      if (kind == "deleteWiki") { store.deleteWiki(); return {}; }
      if(kind=="review") { const auto args=QJsonDocument::fromJson(query.toUtf8()).object(); store.reviewMemory(args.value("id").toInt(),args.value("action").toString(),args.value("correction").toString()); return QJsonObject{{"reviewed",args.value("id").toInt()}}; }
      if(kind=="semantic" || kind=="download") { if(kind=="download") store.downloadSemanticModel(); return store.buildSemanticIndex(); }
      if ((kind == "ask" && !store.projectFiles().isEmpty()) || (kind == "verify" && (!queuedCheck || firstCheck))) store.indexProject();
      if(kind=="verify") return store.prepareVerification(query.toInt(), queuedCheck, inputScale);
      if(kind=="completion") { store.indexProject(); return store.prepareVerification(query.toInt(), false, inputScale, true); }
      if (kind == "ask") {
        auto result = store.answerPrompt(query, askContext);
        result["contextTokens"] = askContext;
        result["provider"] = answerProvider;
        return result;
      }
      return store.briefing(query, 8, false);
      };
      auto result=job();
      // Jobs that change the project come back with what the window shows, read
      // here instead of on the window's thread. Searches and previews change nothing.
      if(kind!="preview" && kind!="functionPlan" && kind!="transcriptPlan") { try { result["snapshot"]=store.snapshot(); } catch(const std::exception &) {} }  // The window reads it itself then.
      return result;
    } catch(const std::exception &e) { return QJsonObject{{"error",QString::fromUtf8(e.what())}}; }
  }));
  if(quiet) { quietWorkStarted(); emit statusChanged(); return; }
  message(kind=="download" ? tr("Downloading the local embedding model (about 622 MB), then indexing. You can cancel and resume later.") : tr("Preparing project evidence…"));
}
void Backend::indexProject() { work("index"); }
void Backend::retrySkippedLearning(const QString &source) {
  if (!busy() && !buildProgress().value("active").toBool()) work("retryLearning", source);
}
void Backend::updateSemantic(bool download) { work(download?"download":"semantic"); }
void Backend::disableSemantic() { if(!m_store || busy()) return; try { m_store->setSemanticEnabled(false); refresh(); message(tr("Semantic search disabled. Keyword search remains available.")); } catch(const std::exception &e) { message(QString::fromUtf8(e.what()),true); } }
void Backend::installSemanticRuntime() { QDesktopServices::openUrl(QUrl("https://ollama.com/download")); message(tr("Install and open Ollama, then choose Download model & enable. Embeddings stay on this computer.")); }
void Backend::openReleases() {
  if (!QDesktopServices::openUrl(QUrl("https://github.com/MadeBySaints/lore/releases")))
    message(tr("Could not open your browser. Visit https://github.com/MadeBySaints/lore/releases to check for updates."),true);
}
void Backend::ask(const QString &question) { if(question.trimmed().isEmpty()) return; if(!modelReady("ask", m_answerProvider)) { message(tr("Set up the provider chosen for Ask project in Connections before asking."),true); return; } m_answer.clear(); emit answerChanged(); work("ask",question.trimmed().left(4000)); }
void Backend::previewContext(const QString &query) { work("preview",query); }
void Backend::verifyMemory(int id) {
  if(!m_store) return;
  if(!modelReady(m_verifyAuto ? "autoVerify" : "verify", verifyModel())) { m_verifyAuto=false; message(tr("Set up the model chosen for Verify first: sign in to Claude Code or Codex, or choose a local model in Connections."),true); m_verifyQueue.clear(); m_verifyRunning=false; emit statusChanged(); return; }
  work("verify",QString::number(id));
}
void Backend::checkCompletion(int id) {
  if (!m_store || id <= 0 || busy() || m_verifyRunning) return;
  if (!modelReady("verify", m_verifyProvider)) {
    message(tr("Set up the model selected for Wiki verification before checking completion."), true);
    return;
  }
  work("completion", QString::number(id));
}

QString Backend::terminalTime() const { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

void Backend::setAutoReview(int threshold) {
  if (!m_store) return;
  try { m_store->setAutoReview(threshold); refresh(); if (threshold > 0) message(tr("The model now settles entries it is at least %1% sure of. Reopen any of them to review it yourself.").arg(qBound(1, threshold, 100))); }
  catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}
QVariantList Backend::promptPresets() const { return Store::promptPresets().toVariantList(); }
QString Backend::buildPreset() const { return m_store ? m_store->promptPresetId("build") : QString(); }
QString Backend::verifyPreset() const { return m_store ? m_store->promptPresetId("verify") : QString(); }
void Backend::setBuildPreset(const QString &id) { setPromptPreset("build", id); }
void Backend::setVerifyPreset(const QString &id) { setPromptPreset("verify", id); }
void Backend::setPromptPreset(const QString &kind, const QString &id) {
  if (!m_store) return;
  try { m_store->setPromptPreset(kind, id); }
  catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
  emit presetsChanged();
}
QVariantList Backend::quickCommands() const {
  static const QVariantList builtIn = [] { QVariantList out; for (const auto &v : QJsonDocument::fromJson(Store::read(":/quick-commands.json").toUtf8()).array()) { auto c = v.toObject().toVariantMap(); c["builtIn"] = true; out.append(c); } return out; }();
  QVariantList mine = m_customCommands; for (auto &c : mine) { auto m = c.toMap(); m["builtIn"] = false; c = m; }
  return builtIn + mine;
}
QString Backend::saveQuickCommand(const QString &id, const QString &name, const QString &text) {
  const QString title = name.simplified().left(60), body = text.trimmed().left(4000);
  if (title.isEmpty() || body.isEmpty()) { message(tr("Give the command a name and a prompt."), true); return {}; }
  QString key = id.startsWith("u-") ? id : QString(); bool found = false;
  for (auto &c : m_customCommands) { auto m = c.toMap(); if (!key.isEmpty() && m.value("id").toString() == key) { m["name"] = title; m["text"] = body; c = m; found = true; } }
  if (!found) { key = "u-" + QUuid::createUuid().toString(QUuid::Id128).left(10); m_customCommands.append(QVariantMap{{"id", key}, {"name", title}, {"text", body}}); }
  savePreferences(); emit promptsChanged(); return key;
}
void Backend::deleteQuickCommand(const QString &id) {
  for (int i = 0; i < m_customCommands.size(); ++i) if (m_customCommands[i].toMap().value("id").toString() == id) { m_customCommands.removeAt(i); break; }
  savePreferences(); emit promptsChanged();
}
void Backend::runQuickCommand(const QString &id) {
  if (!m_store) return;
  for (const auto &c : quickCommands()) if (c.toMap().value("id").toString() == id) {
    const auto choice = modelChoice("quick", m_quickAgent);
    m_tools.startSessionWith(m_quickAgent, source(), c.toMap().value("text").toString(),
      Toolbox::agentArguments(m_quickAgent, choice.value("model").toString(), choice.value("effort").toString()));
    message(tr("Opened %1 in %2 with “%3”.").arg(m_quickAgent == "codex" ? QStringLiteral("Codex") : QStringLiteral("Claude Code"), QDir::toNativeSeparators(source()), c.toMap().value("name").toString()));
    return;
  }
}
// Opens the quick-command agent in the project on one open next step. Its
// session is captured, so the work done becomes memory.
// for the step is ticked when the step is finished.
void Backend::workOnStep(int id) {
  if (!m_store) return;
  for (const auto &v : m_memories) {
    const auto m = v.toMap(); if (m.value("id").toInt() != id) continue;
    QString prompt = tr("Work on this next step from the project's Lore memory: “%1”. Its wiki page is %2. Read BRAIN.md and that page first, check the current code, make the change, run the tests, and say clearly when the step is done.")
      .arg(m.value("title").toString(), QDir::toNativeSeparators(project() + "/.lore/wiki/" + QString::number(id) + ".md"));
    const auto choice = modelChoice("quick", m_quickAgent);
    m_tools.startSessionWith(m_quickAgent, source(), prompt, Toolbox::agentArguments(m_quickAgent, choice.value("model").toString(), choice.value("effort").toString()));
    message(tr("Opened %1 on “%2”.").arg(m_quickAgent == "codex" ? QStringLiteral("Codex") : QStringLiteral("Claude Code"), m.value("title").toString()));
    return;
  }
}
void Backend::setVerifyProvider(const QString &provider) {
  if (provider != "claude" && provider != "codex" && provider != "local") return;
  m_verifyProvider = provider; savePreferences(); emit statusChanged();
}
void Backend::verifyAll() {
  if(!m_store || personalWiki() || !m_verifyQueue.isEmpty() || m_verifyRunning) return;
  try { for(const auto &v:m_store->reviewQueue()) m_verifyQueue << v.toInt(); } catch(const std::exception &e) { message(QString::fromUtf8(e.what()),true); return; }
  if(m_verifyQueue.isEmpty()) { message(tr("No memories are ready for verification.")); return; }
  m_verifyDone=0; m_verifyFailed=0; m_verifyTotal=m_verifyQueue.size(); m_verifyHadError = false;
  message(tr("Checking %1 memories awaiting review…").arg(m_verifyTotal)); continueVerify();
}
// One check at a time; each runs when the model and background work are free.
void Backend::continueVerify() {
  m_verifyRunning=false;
  // An automatic run gives way to a wiki build between checks; it picks up
  // the remaining entries the next time Lore is idle.
  if(m_verifyAuto && !m_verifyQueue.isEmpty() && (automationDue() || !m_manualProvider.isEmpty() || !m_autoVerify)) {
    message(tr("Automatic verification paused for a wiki build after %1 of %2 checks.").arg(m_verifyDone + m_verifyFailed).arg(m_verifyTotal));
    m_verifyQueue.clear(); m_verifyTotal=0; m_verifyAuto=false; emit statusChanged(); QTimer::singleShot(0,this,&Backend::runAutomation); return;
  }
  if(m_verifyQueue.isEmpty()) {
    if (m_verifyAuto && m_verifyHadError) m_verifySnooze = QDateTime::currentDateTimeUtc().addSecs(1800);
    if(m_verifyTotal>0) message((m_verifyAuto ? tr("Automatic verification finished: %1 of %2 memories checked%3.") : tr("Agent review finished: %1 of %2 memories checked%3.")).arg(m_verifyDone).arg(m_verifyTotal).arg(m_verifyFailed>0 ? tr(", %1 could not be checked").arg(m_verifyFailed) : QString()));
    m_verifyTotal=0; m_verifyAuto=false; emit statusChanged(); return;
  }
  if(modelRunning() || m_work.isRunning() || capturing()) { QTimer::singleShot(1000,this,&Backend::continueVerify); return; }
  m_verifyRunning=true; emit statusChanged();
  verifyMemory(m_verifyQueue.takeFirst());
}
void Backend::deleteWiki() {
  if (!m_store) return;
  if (busy() || capturing() || m_verifyRunning || !m_verifyQueue.isEmpty() || m_autoRun || !m_manualProvider.isEmpty()) {
    message(tr("Wait for the current capture, build, or verification to finish, then delete the wiki."), true);
    return;
  }
  work("deleteWiki");
}

void Backend::reviewMemory(int id,const QString &action,const QString &correction) {
  // Reviews run in the worker so the wiki stays responsive. Clicks queue in
  // order rather than replacing each other while earlier work finishes.
  if(!m_store) return;
  m_reviews.append(Review{id,action,correction});
  if(!busy() && !capturing()) startReview();
}
void Backend::startReview() {
  if(m_reviews.isEmpty() || !m_store || m_work.isRunning() || capturing()) return;
  const Review next=m_reviews.takeFirst();
  work("review",QString::fromUtf8(QJsonDocument(QJsonObject{{"id",next.id},{"action",next.action},{"correction",next.correction}}).toJson(QJsonDocument::Compact)),true);
}

QVariantList Backend::knownProjects() const {
  QList<QVariantMap> list;
  for (auto it = m_projectOptions.cbegin(); it != m_projectOptions.cend(); ++it) {
    QVariantMap o = it.value().toMap(); const QString working = it.key();
    if (m_hiddenProjects.contains(working)) continue;
    const QString source = o.value("source").toString().isEmpty() ? working : o.value("source").toString();
    o["working"] = working; o["source"] = source;
    if (o.value("name").toString().isEmpty()) o["name"] = QFileInfo(source).fileName();
    o["together"] = source == working; o["current"] = m_store && working == project();
    o["missing"] = !QFileInfo::exists(working + "/.lore/brain.sqlite");
    list.append(o);
  }
  std::sort(list.begin(), list.end(), [](const QVariantMap &a, const QVariantMap &b) {
    const QString x = a.value("lastOpened").toString(), y = b.value("lastOpened").toString();
    return x != y ? x > y : a.value("name").toString().compare(b.value("name").toString(), Qt::CaseInsensitive) < 0;
  });
  QVariantList result; for (const auto &o : list) result.append(o);
  return result;
}

bool Backend::openKnownProject(const QString &working) {
  if (!QFileInfo::exists(working + "/.lore/brain.sqlite")) { message(tr("Lore's files for this project were not found at %1. Remove it from the list, or browse to its new location.").arg(QDir::toNativeSeparators(working)), true); return false; }
  return selectProject(working);
}

void Backend::forgetProject(const QString &path) {
  if (m_store && (path == project() || path == source())) { message(tr("This project is open. Switch to another project before removing it from the list."), true); return; }
  m_projectOptions.remove(path); m_recent.removeAll(path);
  if (!m_hiddenProjects.contains(path)) m_hiddenProjects.append(path);
  for (int i = m_suggested.size() - 1; i >= 0; --i) if (m_suggested[i].toMap().value("path").toString() == path) m_suggested.removeAt(i);
  savePreferences(); emit projectsChanged();
  message(tr("Removed from the project list. Its files were not changed."));
}

// Finds Lore memories in the workspace folder and folders used with Claude
// Code or Codex. Runs in a worker; each Store is opened inside it.
void Backend::scanProjects() {
  if (m_scan.isRunning()) return;
  QStringList known = m_projectOptions.keys();
  const QString workspace = Store::workspaceRoot();
  const QString claude = m_capturePaths.claude.isEmpty() ? qEnvironmentVariable("CLAUDE_CONFIG_DIR", QDir::homePath() + "/.claude") + "/projects" : m_capturePaths.claude;
  const QString codex = m_capturePaths.codex.isEmpty() ? qEnvironmentVariable("CODEX_HOME", QDir::homePath() + "/.codex") + "/sessions" : m_capturePaths.codex;
  m_scan.setFuture(QtConcurrent::run([known, workspace, claude, codex] {
    QSet<QString> candidates(known.begin(), known.end());
    for (const auto &name : QDir(workspace).entryList({"Lore-*"}, QDir::Dirs | QDir::NoDotAndDotDot)) candidates.insert(QFileInfo(workspace + "/" + name).canonicalFilePath());
    // Folders the coding tools ran in, newest use first.
    QHash<QString, QPair<QString, QSet<QString>>> used;
    const auto note = [&used](const QString &cwd, const QDateTime &when, const QString &tool) {
      const QString path = QFileInfo(cwd).canonicalFilePath();
      if (path.isEmpty() || !QFileInfo(path).isDir() || path == QFileInfo(QDir::homePath()).canonicalFilePath() || QDir(path).isRoot()) return;
      auto &entry = used[path]; const QString stamp = when.toUTC().toString(Qt::ISODate);
      if (stamp > entry.first) entry.first = stamp; entry.second.insert(tool);
    };
    const auto cwdIn = [](const QString &file, int lines) {
      QFile f(file); if (!f.open(QIODevice::ReadOnly)) return QString();
      for (int i = 0; i < lines && !f.atEnd(); ++i) {
        const auto o = QJsonDocument::fromJson(f.readLine(1 << 20)).object();
        const QString cwd = o.value("cwd").isString() ? o.value("cwd").toString() : o.value("payload").toObject().value("cwd").toString();
        if (!cwd.isEmpty()) return cwd;
      }
      return QString();
    };
    for (const auto &dir : QDir(claude).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
      const auto files = QDir(dir.filePath()).entryInfoList({"*.jsonl"}, QDir::Files, QDir::Time);
      if (!files.isEmpty()) note(cwdIn(files.first().filePath(), 40), files.first().lastModified(), "Claude Code");
    }
    QList<QFileInfo> rollouts; QDirIterator it(codex, {"*.jsonl"}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); rollouts.append(it.fileInfo()); }
    std::sort(rollouts.begin(), rollouts.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
    for (const auto &file : rollouts.mid(0, 300)) note(cwdIn(file.filePath(), 3), file.lastModified(), "Codex");
    for (auto u = used.cbegin(); u != used.cend(); ++u) {
      if (QFileInfo::exists(u.key() + "/.lore/brain.sqlite")) candidates.insert(u.key());
      const QString beside = QFileInfo(Store::defaultWorkingDirectory(u.key())).canonicalFilePath();
      if (!beside.isEmpty() && QFileInfo::exists(beside + "/.lore/brain.sqlite")) candidates.insert(beside);
    }
    QJsonArray lore; QStringList remembered;
    for (const auto &working : candidates) {
      if (working.isEmpty() || !QFileInfo::exists(working + "/.lore/brain.sqlite")) continue;
      try {
        Store store(working); const auto stats = store.stats();
        lore.append(QJsonObject{{"working", store.root()}, {"source", store.source()}, {"name", QFileInfo(store.source()).fileName()}, {"conversations", stats.value("sources").toInt()}, {"entries", stats.value("memories").toInt()}});
        remembered << store.source() << store.root();
      } catch (...) {}
    }
    QList<QJsonObject> suggested;
    for (auto u = used.cbegin(); u != used.cend(); ++u) {
      bool covered = false; for (const auto &r : remembered) if (Paths::contains(r, u.key())) covered = true;
      if (covered) continue;
      QStringList tools(u.value().second.begin(), u.value().second.end()); tools.sort();
      suggested.append(QJsonObject{{"path", u.key()}, {"name", QFileInfo(u.key()).fileName()}, {"tools", tools.join(" · ")}, {"lastUsed", u.value().first}});
    }
    std::sort(suggested.begin(), suggested.end(), [](const QJsonObject &a, const QJsonObject &b) { return a.value("lastUsed").toString() > b.value("lastUsed").toString(); });
    QJsonArray suggestions; for (const auto &o : suggested.mid(0, 40)) suggestions.append(o);
    return QJsonObject{{"lore", lore}, {"suggested", suggestions}};
  }));
  emit projectsChanged();
}

void Backend::setAutoProvider(const QString &provider) {
  if (provider != "claude" && provider != "codex" && provider != "local") return;
  m_autoProvider = provider; savePreferences(); emit statusChanged();
  if (m_autoLearning) m_auto.start();
}
void Backend::setAutoAfter(int messages) { m_autoAfter = qBound(0, messages, 100000); savePreferences(); emit statusChanged(); if (m_autoLearning) m_auto.start(); }
void Backend::setAutoEvery(int hours) { m_autoEvery = qBound(0, hours, 168); savePreferences(); emit statusChanged(); if (m_autoLearning) m_auto.start(); }

bool Backend::automationDue() const {
  const int pending = m_stats.value("pending").toInt();
  if (!m_autoLearning || !m_store || pending == 0) return false;
  if (m_autoRun) return true;
  if (paused()) return false;
  if (m_autoSnooze.isValid() && QDateTime::currentDateTimeUtc() < m_autoSnooze) return false;
  const int after = m_autoAfter == 0 && m_autoEvery == 0 ? 1 : m_autoAfter;
  if (after > 0 && pending >= after) return true;
  if (m_autoEvery > 0) {
    const auto last = QDateTime::fromString(m_lastAutoBuild, Qt::ISODate);
    if (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) >= qint64(m_autoEvery) * 3600) return true;
  }
  return false;
}

// Decides what runs next: a due wiki build first, then idle verification.
// Either starts after a countdown of Lore staying idle; a build already
// under way goes straight on to its next round.
void Backend::runAutomation() {
  if (personalWiki()) { stopCountdown(); return; }
  if (!m_store || !m_manualProvider.isEmpty()) { stopCountdown(); return; }  // A Build wiki press is already working through the backlog.
  if (automationDue()) {
    if (!modelReady("build", m_autoProvider)) {
      message(tr("Automatic wiki builds are waiting for %1. Open Connections to set it up.").arg(m_autoProvider == "local" ? tr("the local model") : m_autoProvider == "claude" ? QString("Claude Code") : QString("Codex")));
      m_autoRun = false; stopCountdown(); return;
    }
    if (m_autoRun) { if (!busy()) startAutomatic("build"); return; }
    startCountdown("build"); return;
  }
  if (autoVerifyDue() && m_verifyQueue.isEmpty() && !m_verifyRunning && modelReady("autoVerify", m_autoVerifyProvider)) { startCountdown("verify"); return; }
  stopCountdown();
}
void Backend::startAutomatic(const QString &kind) {
  if (busy()) return;
  if (kind == "verify") {
    m_verifyAuto = true; m_lastAutoVerify = QDateTime::currentDateTimeUtc().toString(Qt::ISODate); savePreferences(); verifyAll();
    if (m_verifyQueue.isEmpty() && !m_verifyRunning) m_verifyAuto = false;
    return;
  }
  if (!m_autoRun) { m_lastAutoBuild = QDateTime::currentDateTimeUtc().toString(Qt::ISODate); m_buildDone = 0; m_buildRounds = 0; m_retried = false; savePreferences(); }
  m_autoRun = true;
  if (!startRound(m_autoProvider)) m_autoRun = false;
  emit statusChanged();
}
void Backend::startCountdown(const QString &kind) {
  if (m_countdownKind == kind) return;
  const int delay = kind == "build" ? m_autoBuildDelay : m_autoVerifyDelay;
  if (delay <= 0) { stopCountdown(); startAutomatic(kind); return; }
  m_countdownKind = kind; m_countdownLeft = delay; m_countdown.start();
  message(kind == "build" ? tr("Automatic wiki build starts in %1 s if Lore stays idle.").arg(delay) : tr("Automatic verification starts in %1 s if Lore stays idle.").arg(delay));
  emit statusChanged();
}
void Backend::stopCountdown() { if (m_countdownKind.isEmpty()) return; m_countdown.stop(); m_countdownKind.clear(); m_countdownLeft = 0; emit statusChanged(); }

bool Backend::autoVerifyDue() const {
  if (!m_autoVerify || !m_store || m_reviewWaiting == 0 || !m_manualProvider.isEmpty() || m_autoRun || paused()) return false;
  if (m_verifySnooze.isValid() && QDateTime::currentDateTimeUtc() < m_verifySnooze) return false;
  const int after = m_autoVerifyAfter == 0 && m_autoVerifyEvery == 0 ? 1 : m_autoVerifyAfter;
  if (after > 0 && m_reviewWaiting >= after) return true;
  if (m_autoVerifyEvery > 0) {
    const auto last = QDateTime::fromString(m_lastAutoVerify, Qt::ISODate);
    if (!last.isValid() || last.secsTo(QDateTime::currentDateTimeUtc()) >= qint64(m_autoVerifyEvery) * 3600) return true;
  }
  return false;
}
QString Backend::autoVerifyStatus() const {
  if (!m_autoVerify) return {};
  if (m_verifyAuto && (!m_verifyQueue.isEmpty() || m_verifyRunning)) return tr("Verifying automatically: %1 of %2 checked.").arg(m_verifyDone + m_verifyFailed).arg(m_verifyTotal);
  if (m_verifySnooze.isValid() && QDateTime::currentDateTimeUtc() < m_verifySnooze) return tr("Automatic verification paused until %1.").arg(QLocale().toString(m_verifySnooze.toLocalTime().time(), QLocale::ShortFormat));
  QStringList when;
  const int after = m_autoVerifyAfter == 0 && m_autoVerifyEvery == 0 ? 1 : m_autoVerifyAfter;
  if (after > 0) when << tr("when %1 entries await review (%2 now)").arg(after).arg(m_reviewWaiting);
  if (m_autoVerifyEvery > 0) {
    const auto last = QDateTime::fromString(m_lastAutoVerify, Qt::ISODate);
    when << (last.isValid() ? tr("every %1 hours (next after %2)").arg(m_autoVerifyEvery).arg(QLocale().toString(last.addSecs(qint64(m_autoVerifyEvery) * 3600).toLocalTime(), QLocale::ShortFormat)) : tr("every %1 hours").arg(m_autoVerifyEvery));
  }
  return tr("Next automatic check, while no wiki build is running or due: %1.").arg(when.join(tr(" or ")));
}
QString Backend::autoStatus() const {
  if (!m_autoLearning) return {};
  if (!m_autoRun && paused()) return tr("Paused until %1.").arg(QLocale().toString(m_pauseAll.toLocalTime().time(), QLocale::ShortFormat));
  const int pending = m_stats.value("pending").toInt();
  if (!m_autoRun && m_autoSnooze.isValid() && QDateTime::currentDateTimeUtc() < m_autoSnooze) return tr("Paused after Cancel until %1.").arg(QLocale().toString(m_autoSnooze.toLocalTime().time(), QLocale::ShortFormat));
  if (m_autoRun) return tr("Building the wiki automatically. %1 messages waiting.").arg(pending);
  QStringList when;
  const int after = m_autoAfter == 0 && m_autoEvery == 0 ? 1 : m_autoAfter;
  if (after > 0) when << tr("when %1 messages are waiting (%2 now)").arg(after).arg(pending);
  if (m_autoEvery > 0) {
    const auto last = QDateTime::fromString(m_lastAutoBuild, Qt::ISODate);
    when << (last.isValid() ? tr("every %1 hours (next after %2)").arg(m_autoEvery).arg(QLocale().toString(last.addSecs(qint64(m_autoEvery) * 3600).toLocalTime(), QLocale::ShortFormat))
                            : tr("every %1 hours").arg(m_autoEvery));
  }
  const auto last = QDateTime::fromString(m_lastAutoBuild, Qt::ISODate);
  return tr("Next automatic build: %1.").arg(when.join(tr(" or "))) + (last.isValid() ? " " + tr("Last one started %1.").arg(QLocale().toString(last.toLocalTime(), QLocale::ShortFormat)) : QString());
}

// What Connect project will create or change, for the setup guide to show
// first: each file with create/update and what Lore puts there.
QVariantList Backend::connectionPreview() const {
  const QString source = this->source(), working = workingDirectory();
  if (source.isEmpty()) return {};
  const bool together = working.isEmpty() || QDir::cleanPath(working) == QDir::cleanPath(source);
  const auto item = [](const QString &path, bool exists, const QString &what) { return QVariantMap{{"path", QDir::toNativeSeparators(path)}, {"action", exists ? "update" : "create"}, {"what", what}}; };
  QVariantList out;
  const QString root = together ? source : working;
  out << item(root + "/.lore", QFileInfo::exists(root + "/.lore"), tr("Lore's private database, wiki pages, and conversation exports. Never committed: it holds a .gitignore of its own."));
  if (personalSetup()) {
    const QString claude = qEnvironmentVariable("CLAUDE_CONFIG_DIR", QDir::homePath() + "/.claude") + "/settings.json";
    const QString codex = qEnvironmentVariable("CODEX_HOME", QDir::homePath() + "/.codex");
    out << item(source + "/BRAIN.md", QFileInfo::exists(source + "/BRAIN.md"), tr("An index of your personal wiki and conversation exports."));
    out << item(claude, QFileInfo::exists(claude), tr("Claude Code user-level capture hooks."));
    out << item(codex + "/hooks.json", QFileInfo::exists(codex + "/hooks.json"), tr("Codex user-level capture hooks."));
    out << item(codex + "/lore-personal.json", QFileInfo::exists(codex + "/lore-personal.json"), tr("The folder where Codex's capture hook writes."));
    return out;
  }
  out << item(root + "/BRAIN.md", QFileInfo::exists(root + "/BRAIN.md"), tr("The short project map with links to the wiki and indexes."));
  out << item(source + "/AGENTS.md", QFileInfo::exists(source + "/AGENTS.md"), QFileInfo::exists(source + "/AGENTS.md") ? tr("A short Lore block is added at the top, pointing Codex to BRAIN.md. Your text stays as it is.") : tr("A short file pointing Codex to BRAIN.md."));
  out << item(source + "/CLAUDE.md", QFileInfo::exists(source + "/CLAUDE.md"), QFileInfo::exists(source + "/CLAUDE.md") ? tr("A short Lore block is added at the top, pointing Claude Code to BRAIN.md. Your text stays as it is.") : tr("A short file pointing Claude Code to BRAIN.md."));
  out << item(source + "/.claude/settings.local.json", QFileInfo::exists(source + "/.claude/settings.local.json"), tr("Local Claude Code hooks: at session start, on each prompt, before compaction, and at the end, Claude Code saves the conversation to Lore and gets relevant context back. Other hooks are kept."));
  out << item(source + "/.gitignore", QFileInfo::exists(source + "/.gitignore"), together ? tr("A Lore block that keeps .lore/ and the machine-specific hook file out of Git.") : tr("A Lore block that keeps the machine-specific hook file out of Git."));
  return out;
}
// Quick facts about a folder for the setup guide; reads no file contents.
QVariantMap Backend::folderFacts(const QString &path) const {
  if (path.isEmpty() || !QFileInfo(path).isDir()) return {};
  const QDir dir(path); bool used = false;
  for (const auto &v : m_suggested) if (QDir::cleanPath(v.toMap().value("path").toString()) == QDir::cleanPath(path)) used = true;
  for (const auto &v : knownProjects()) if (QDir::cleanPath(v.toMap().value("source").toString()) == QDir::cleanPath(path)) used = true;
  return {{"git", dir.exists(".git")}, {"agents", dir.exists("AGENTS.md")}, {"claude", dir.exists("CLAUDE.md")}, {"hooks", dir.exists(".claude/settings.local.json")},
    {"memory", dir.exists(".lore/brain.sqlite")}, {"usedWithAgents", used}, {"name", dir.dirName()}};
}
void Backend::pauseAutomations(int minutes) {
  m_pauseAll = QDateTime::currentDateTimeUtc().addSecs(qint64(qBound(1, minutes, 24 * 60)) * 60);
  stopCountdown(); message(tr("Automations paused until %1.").arg(QLocale().toString(m_pauseAll.toLocalTime().time(), QLocale::ShortFormat)));
}
void Backend::resumeAutomations() {
  m_pauseAll = {}; m_autoSnooze = {}; m_verifySnooze = {};
  message(tr("Automations resumed.")); QTimer::singleShot(0, this, &Backend::runAutomation);
}

// Between rounds: wait for background work, then run the next round with the
// build's model. A Build wiki press stops if the Build wiki model changed.
void Backend::continueBuild() {
  if (!m_store || (!m_autoRun && m_manualProvider.isEmpty())) { endBuild(); return; }
  if (modelRunning() || m_work.isRunning()) { QTimer::singleShot(400, this, &Backend::continueBuild); return; }
  if (m_autoRun) { if (!startRound(m_autoProvider)) endBuild(); emit statusChanged(); return; }
  if (m_manualProvider != m_learningProvider) { message(tr("Wiki build stopped: the Build wiki model changed. %1 messages learned in %2 rounds.").arg(m_buildDone).arg(m_buildRounds)); endBuild(); return; }
  message(tr("Building the wiki: %1 messages still waiting…").arg(m_stats.value("pending").toInt()));
  if (!startRound(m_manualProvider)) endBuild();
  emit statusChanged();
}
void Backend::endBuild() { m_manualProvider.clear(); m_autoRun = false; emit statusChanged(); }
QVariantMap Backend::buildProgress() const {
  const bool active = m_autoRun || !m_manualProvider.isEmpty();
  if (!active) return {{"active", false}};
  const QString provider = m_autoRun ? m_autoProvider : m_manualProvider;
  const int pending = m_stats.value("pending").toInt();
  return {{"active", true}, {"automatic", m_autoRun}, {"provider", provider == "claude" ? QString("Claude Code") : provider == "codex" ? QString("Codex") : tr("the local model")},
          {"done", m_buildDone}, {"pending", pending}, {"total", m_buildDone + pending}, {"rounds", m_buildRounds}, {"running", modelRunning()}};
}
