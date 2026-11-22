
#include "backend.h"
#include "storeutil.h"

#include <QJsonDocument>
#include <QTimer>
#include <QtConcurrentRun>
#include <stdexcept>

QVariantMap Backend::knowledgeProgress() const {
  const auto item = m_knowledgePosition < m_knowledgeQueue.size() ? m_knowledgeQueue.at(m_knowledgePosition).toObject() : QJsonObject();
  return {{"active", !m_knowledgeJob.isEmpty()}, {"kind", m_knowledgeJob},
    {"done", m_knowledgePosition - m_failedFunctionFiles}, {"failed", m_failedFunctionFiles},
    {"total", m_knowledgeQueue.size()},
    {"current", item.value(m_knowledgeJob == "functions" ? "path" : "title").toString()}};
}

void Backend::refreshKnowledge() {
  try {
    const auto functions = m_store ? m_store->functions().toVariantList() : QVariantList();
    const auto findings = m_store ? m_store->transcriptFindings().toVariantList() : QVariantList();
    const int indexed = m_store ? m_store->indexedFunctionFiles() : 0;
    const int eligible = m_store ? m_store->functionPlan(true).size() : 0;
    if (functions != m_functions || findings != m_transcriptFindings || indexed != m_indexedFunctionFiles || eligible != m_eligibleFunctionFiles) {
      m_functions = functions;
      m_transcriptFindings = findings;
      m_indexedFunctionFiles = indexed;
      m_eligibleFunctionFiles = eligible;
      emit knowledgeChanged();
    }
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}

bool Backend::transcriptAutoPublish() const {
  return m_store && m_store->setting("transcriptAutoPublish") == "on";
}

int Backend::transcriptThreshold() const {
  const int stored = m_store ? m_store->setting("transcriptThreshold").toInt() : 0;
  return qBound(1, stored ? stored : 90, 100);
}

void Backend::setTranscriptPublishing(bool enabled, int threshold) {
  if (!m_store || busy() || personalWiki()) return;
  try {
    m_store->setSetting("transcriptThreshold", QString::number(qBound(1, threshold, 100)));
    m_store->setSetting("transcriptAutoPublish", enabled ? QStringLiteral("on") : QString());
    emit knowledgeChanged();
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}

QString Backend::functionScanProvider() const {
  const QString value = m_store ? m_store->setting("functionScanProvider") : QString();
  return value.isEmpty() ? learningProvider() : value;
}

QString Backend::transcriptAnalysisProvider() const {
  const QString value = m_store ? m_store->setting("transcriptAnalysisProvider") : QString();
  return value.isEmpty() ? learningProvider() : value;
}

void Backend::setKnowledgeProvider(const QString &task, const QString &provider) {
  if (!m_store || busy() || !QStringList{"claude", "codex", "local"}.contains(provider)) return;
  const QString key = task == "functions" ? QStringLiteral("functionScanProvider")
    : task == "transcript" ? QStringLiteral("transcriptAnalysisProvider") : QString();
  if (key.isEmpty()) return;
  try { m_store->setSetting(key, provider); }
  catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
  emit knowledgeChanged();
}

QString Backend::transcriptEvidence(const QString &id) {
  if (!m_store) return {};
  try { return m_store->transcriptFindingEvidence(id); }
  catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); return {}; }
}

bool Backend::projectFileGrid() const {
  return m_store && m_store->setting("projectFileGrid") == "on";
}

void Backend::setProjectFileGrid(bool enabled) {
  if (!m_store) return;
  try { m_store->setSetting("projectFileGrid", enabled ? QStringLiteral("on") : QString()); }
  catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
  emit knowledgeChanged();
}

void Backend::startFunctionIndex(const QString &provider, bool rerun) {
  startKnowledgeJob("functions", provider, {{"rerun", rerun}});
}

void Backend::startTranscriptAnalysis(const QString &source, const QString &provider, bool rerun) {
  startKnowledgeJob("transcript", provider, {{"source", source}, {"rerun", rerun}});
}

void Backend::startKnowledgeJob(const QString &kind, const QString &provider, const QJsonObject &options) {
  if (!m_store || personalWiki() || busy() || capturing() || verifyProgress().value("active").toBool() || buildProgress().value("active").toBool()) return;
  if ((provider == "local" && !localReady(kind)) || (provider != "local" && !modelReady(kind, provider))) {
    message(tr("Set up the selected model in AI & models before starting."), true);
    return;
  }
  m_knowledgeJob = kind;
  m_knowledgeProvider = provider;
  m_knowledgePosition = 0;
  m_knowledgeCancelled = false;
  m_knowledgeQueue = {};
  m_knowledgeChunk = {};
  m_functionRecords = {};
  m_nextFunctionLine = 1;
  m_failedFunctionFiles = 0;
  m_functionRetryLines = 0;
  m_transcriptCursor = -1;
  work(kind == "functions" ? "functionPlan" : "transcriptPlan", compact(options));
  message(kind == "functions" ? tr("Preparing the function scan…") : tr("Preparing transcript analysis…"));
  emit statusChanged();
}

void Backend::nextKnowledgeChunk() {
  if (m_knowledgeJob.isEmpty()) return;
  if (m_knowledgeCancelled || m_knowledgePosition >= m_knowledgeQueue.size()) { finishKnowledge(); return; }
  try {
    const auto item = m_knowledgeQueue.at(m_knowledgePosition).toObject();
    if (m_knowledgeJob == "functions") {
      m_knowledgeChunk = m_store->functionChunk(item, m_nextFunctionLine, m_functionRetryLines);
    } else {
      if (m_transcriptCursor < 0) m_transcriptCursor = item.value("cursor").toInteger();
      m_knowledgeChunk = m_store->transcriptChunk(item, m_transcriptCursor);
      if (m_knowledgeChunk.isEmpty()) {
        m_transcriptCursor = -1;
        ++m_knowledgePosition;
        QTimer::singleShot(0, this, &Backend::nextKnowledgeChunk);
        return;
      }
    }
    m_operation = m_knowledgeJob;
    startModel(m_knowledgeChunk.value("prompt").toString(), m_knowledgeProvider);
    emit statusChanged();
  } catch (const std::exception &e) { finishKnowledge(QString::fromUtf8(e.what())); }
}

void Backend::acceptKnowledgeResult(const QJsonObject &result) {
  const auto chunk = m_knowledgeChunk;
  const auto root = project(), kind = m_knowledgeJob, provider = m_provider;
  const auto previous = m_functionRecords;
  m_workKind = "knowledgeCommit";
  m_work.setFuture(QtConcurrent::run([root, kind, provider, chunk, result, previous]() -> QJsonObject {
    try {
      Store store(root);
      if (kind == "transcript") {
        store.saveTranscriptFindings(chunk, result, provider);
        return {};
      }
      auto records = previous;
      for (const auto &record : store.checkedFunctions(result, chunk)) records.append(record);
      if (chunk.value("next").toInt()) return {{"records", records}};
      try { store.saveFunctions(chunk, records); }
      catch (const std::exception &e) { return {{"saveError", QString::fromUtf8(e.what())}}; }
      return {};
    } catch (const std::exception &e) { return {{"error", QString::fromUtf8(e.what())}}; }
  }));
  emit statusChanged();
}

void Backend::finishKnowledgeCommit(const QJsonObject &result) {
  if (m_knowledgeJob.isEmpty()) return;
  if (result.contains("error") || result.contains("saveError")) {
    if (m_knowledgeCancelled) finishKnowledge();
    else if (result.contains("saveError")) skipFunctionFile(result.value("saveError").toString());
    else if (m_knowledgeJob == "functions") retryFunctionChunk(result.value("error").toString());
    else finishKnowledge(result.value("error").toString());
    return;
  }
  if (m_knowledgeJob == "functions") {
    const int next = m_knowledgeChunk.value("next").toInt();
    if (next) {
      m_functionRecords = result.value("records").toArray();
      m_nextFunctionLine = next;
    } else {
      m_functionRecords = {};
      m_nextFunctionLine = 1;
      m_functionRetryLines = 0;
      ++m_knowledgePosition;
    }
  } else {
    m_transcriptCursor = m_knowledgeChunk.value("cursor").toInteger();
    if (m_transcriptCursor >= m_knowledgeChunk.value("end").toInteger()) {
      m_transcriptCursor = -1;
      ++m_knowledgePosition;
    }
  }
  emit statusChanged();
  QTimer::singleShot(0, this, &Backend::nextKnowledgeChunk);
}

void Backend::retryFunctionChunk(const QString &error) {
  const int lines = m_knowledgeChunk.value("last").toInt() - m_nextFunctionLine + 1;
  if (m_functionRetryLines || lines <= 8) {
    skipFunctionFile(error);
    return;
  }
  // A timeout needs less work per call. Keep the smaller validation retry
  // for replies that could not be parsed or matched to the source.
  m_functionRetryLines = m_functionTimedOut ? qBound(8, lines / 2, 32) : 8;
  live(QStringLiteral("\n↻ ") + tr("Retrying %1 at line %2 with a smaller excerpt.\n")
    .arg(m_knowledgeChunk.value("path").toString()).arg(m_nextFunctionLine));
  QTimer::singleShot(0, this, &Backend::nextKnowledgeChunk);
}

void Backend::skipFunctionFile(const QString &error) {
  liveIssue(tr("Skipped %1: %2").arg(m_knowledgeChunk.value("path").toString(), error), "skippedFile");
  ++m_failedFunctionFiles;
  ++m_knowledgePosition;
  m_functionRecords = {};
  m_nextFunctionLine = 1;
  m_functionRetryLines = 0;
  emit statusChanged();
  QTimer::singleShot(0, this, &Backend::nextKnowledgeChunk);
}

void Backend::finishKnowledge(const QString &error) {
  const bool functions = m_knowledgeJob == "functions";
  const bool cancelled = m_knowledgeCancelled;
  if (!cancelled && !error.isEmpty()) liveIssue(error, "error");
  const int failed = m_failedFunctionFiles, done = m_knowledgePosition - failed;
  m_knowledgeJob.clear();
  m_knowledgeProvider.clear();
  m_knowledgeQueue = {};
  m_knowledgeChunk = {};
  m_functionRecords = {};
  m_knowledgePosition = 0;
  m_failedFunctionFiles = 0;
  m_knowledgeCancelled = false;
  m_operation.clear();
  try {
    if (functions && (done > 0 || (!cancelled && error.isEmpty()))) m_store->exportFunctions();
    if (m_store->exportsStale() || functions) m_store->render();
    refresh();
    refreshKnowledge();
    if (cancelled) message(tr("Stopped. Completed results were saved."));
    else if (!error.isEmpty()) message(error, true);
    else if (functions && failed) message(tr("Scanned %1 files and skipped %2. See Terminal for details.").arg(done).arg(failed), true);
    else if (functions) message(tr("Function index updated for %n file(s).", "", done));
    else message(tr("Analyzed %n conversation(s). Open Transcript analysis to review findings.", "", done));
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
  emit statusChanged();
  QTimer::singleShot(0, this, &Backend::runDeferred);
}

void Backend::reviewTranscriptFinding(const QString &id, bool accept) {
  if (!m_store || id.isEmpty() || busy() || capturing()) return;
  work("reviewTranscript", compact(QJsonObject{{"id", id}, {"accept", accept}}));
}

void Backend::forgetConversations(const QString &source) {
  if (!m_store || busy() || capturing() || verifyProgress().value("active").toBool() || buildProgress().value("active").toBool()) {
    message(tr("Wait for the current job to finish before forgetting conversations."), true);
    return;
  }
  work("forgetConversations", source);
}

void Backend::deleteForgottenConversations() {
  if (!m_store || busy() || capturing() || verifyProgress().value("active").toBool() || buildProgress().value("active").toBool()) return;
  work("deleteForgottenConversations");
}
