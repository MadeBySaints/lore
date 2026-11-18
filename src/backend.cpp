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
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>

Backend::Backend(QObject *parent) : QObject(parent) {
  m_watch.setInterval(5000);
  connect(&m_watch, &QTimer::timeout, this, &Backend::sync);
  m_auto.setSingleShot(true); m_auto.setInterval(60000);
  connect(&m_auto, &QTimer::timeout, this, [this] { if (m_autoLearning && !busy() && m_stats.value("pending").toInt() > 0) learn(m_learningProvider); });
  m_timeout.setSingleShot(true); m_timeout.setInterval(180000);
  connect(&m_timeout, &QTimer::timeout, this, [this] { m_cancelled = true; m_process.kill(); message(tr("Learning timed out. Your captured data is safe.")); });
  connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { m_output += m_process.readAllStandardOutput(); });
  connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_error += m_process.readAllStandardError(); });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) { m_timeout.stop(); message(m_process.errorString()); emit learningFinished(false); }
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
      m_store->learn(result.object(), m_batch, m_provider); refresh();
      message(tr("Wiki updated with source references.")); ok = true;
    } catch(const std::exception &e) { message(QString::fromUtf8(e.what())); }
    if (ok && m_autoLearning && m_stats.value("pending").toInt() > 0) m_auto.start();
    emit statusChanged(); emit learningFinished(ok);
  });
}
Backend::~Backend() { if (busy()) { m_process.kill(); m_process.waitForFinished(2000); } }
void Backend::message(const QString &text) { m_status = text; emit statusChanged(); }
void Backend::refresh() {
  if (!m_store) return;
  m_stats = m_store->stats().toVariantMap(); m_sessions = m_store->sessions().toVariantList();
  m_memories = m_store->memories().toVariantList(); m_brain = m_store->context();
  search(m_query); emit changed();
}
bool Backend::openProject(const QString &path, bool create) {
  if (busy()) { message(tr("Finish or cancel learning before switching projects.")); return false; }
  try {
    QString p = path.trimmed(); if (p.startsWith("~/")) p.replace(0, 1, QDir::homePath());
    auto store = std::make_unique<Store>(p, create); store->render(); m_store = std::move(store);
    m_auto.stop(); m_autoLearning = false;
    refresh(); message(tr("Project memory ready.")); return true;
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what())); return false; }
}
void Backend::setWatching(bool on) { if (on) m_watch.start(); else { m_watch.stop(); m_auto.stop(); m_autoLearning = false; } emit statusChanged(); }
void Backend::setAutoLearning(bool on) {
  m_autoLearning = on;
  if (on) { setWatching(true); m_auto.start(); } else m_auto.stop();
  emit statusChanged();
}
void Backend::sync() {
  if (!m_store) return;
  try {
    const auto result = m_store->sync(); refresh();
    if (m_autoLearning && result.value("added").toInt() > 0) m_auto.start();
    const auto warnings = result.value("warnings").toArray();
    if (!busy()) message(warnings.isEmpty() ? tr("Capture complete. %1 new records.").arg(result.value("added").toInt()) : tr("Capture completed with warnings: %1").arg(QJsonDocument(warnings).toJson(QJsonDocument::Compact)));
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what())); }
}
void Backend::search(const QString &text) {
  m_query = text;
  try { m_results = m_store ? m_store->search(text).toVariantList() : QVariantList(); emit resultsChanged(); }
  catch(const std::exception &e) { message(QString::fromUtf8(e.what())); }
}
void Backend::connectAgents() {
  if (!m_store) return;
  try { m_store->connectAgents(QCoreApplication::applicationFilePath()); message(tr("Connected. Start a new agent session to load BRAIN.md. Keep capture running for Codex.")); refresh(); }
  catch(const std::exception &e) { message(QString::fromUtf8(e.what())); }
}
QString Backend::readPage(const QString &relative) {
  if (!m_store) return {};
  const QString path = QFileInfo(project() + '/' + relative).canonicalFilePath();
  if (path.isEmpty() || !(Paths::contains(project() + "/.lore/wiki", path) || Paths::contains(project() + "/.lore/sources", path))) { message(tr("This page is outside the project memory.")); return {}; }
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
void Backend::cancel() { setAutoLearning(false); if (busy()) { m_cancelled = true; m_process.kill(); message(tr("Learning cancelled. Your captured data is safe.")); } }
void Backend::learn(const QString &provider) {
  if (!m_store || busy()) return;
  if (provider != "codex" && provider != "claude") { message(tr("Choose Codex or Claude.")); emit learningFinished(false); return; }
  const QString executable = QStandardPaths::findExecutable(provider);
  if (executable.isEmpty()) { message(tr("%1 is not installed or is missing from PATH.").arg(provider)); emit learningFinished(false); return; }
  try {
    const auto batch = m_store->learningBatch(); m_batch = batch.value("events").toArray();
    if (m_batch.isEmpty()) { message(tr("Nothing new to learn. Capture a conversation first.")); emit learningFinished(true); return; }
    m_provider = provider; m_output.clear(); m_error.clear(); m_cancelled = false;
    // Run the summarizer outside the project so project hooks, instructions,
    // and transcript capture cannot create a recursive learning loop.
    m_process.setWorkingDirectory(QDir::tempPath());
    QStringList args;
    if (provider == "codex") args = {"exec", "--ephemeral", "--skip-git-repo-check", "--sandbox", "read-only", "--json", "-"};
    else args = {"-p", "--output-format", "json", "--tools", "", "--strict-mcp-config", "--mcp-config", "{\"mcpServers\":{}}", "--no-session-persistence", "--setting-sources", ""};
    m_process.start(executable, args);
    m_process.write(batch.value("prompt").toString().toUtf8()); m_process.closeWriteChannel();
    m_timeout.start(); message(tr("Learning through %1 using your existing login…").arg(provider));
  } catch(const std::exception &e) { message(QString::fromUtf8(e.what())); emit learningFinished(false); }
}
