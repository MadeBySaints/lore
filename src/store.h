#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <atomic>
#include <memory>


// Each project owns its database. Raw records are append-only; all Markdown is
// reproducible from the database, including the citations used by the wiki.
class Store {
public:
  explicit Store(const QString &project, bool create = false, bool personal = false,
                 int busyTimeout = 5000, bool readOnly = false);
  ~Store();
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  // root() is Lore's working directory (database, wiki, BRAIN.md). source()
  // is the project being remembered; they are the same folder unless the
  // user keeps Lore's files elsewhere.
  QString root() const { return m_root; }
  QString source() const { return m_source; }
  bool separate() const { return m_source != m_root; }
  bool personalWiki() const { return m_personal; }
  void connectPersonalHooks(const QString &binary);
  void capturePersonalHook(const QJsonObject &hook, const QString &provider);
  static QString personalHookTarget();
  void setSource(const QString &path);
  int ingest(const QString &path, const QString &provider);
  int importConversation(const QString &text);
  QJsonObject importChatGpt(const QString &path, const QString &selection = {}, bool preview = false);
  void captureClaudeHook(const QJsonObject &hook);
  void setBusyTimeout(int milliseconds);
  QJsonObject sync(const QString &codexRoot = {}, const QString &claudeRoot = {});
  QJsonObject stats() const;
  QJsonArray search(const QString &query, int limit = 40, bool anyTerms = false) const;
  QJsonArray session(const QString &id, int offset = 0, int limit = 100) const;
  QJsonArray sessions() const;
  QJsonArray memories() const;
  QJsonObject indexProject();
  QJsonArray projectFiles() const;
  QJsonArray functionPlan(bool rerun = false) const;
  static QJsonObject functionScanSchema();
  QJsonObject functionChunk(const QJsonObject &file, int first, int retryLines = 0) const;
  QJsonArray checkedFunctions(const QJsonObject &result, const QJsonObject &chunk) const;
  void saveFunctions(const QJsonObject &file, const QJsonArray &functions, int revision = 2);
  QJsonObject indexPythonFunctions(const QString &interpreter);
  QJsonArray functions() const;
  int indexedFunctionFiles() const;
  void exportFunctions() const;
  QJsonArray transcriptPlan(const QString &source, bool rerun) const;
  QJsonObject transcriptChunk(const QJsonObject &source, qint64 cursor) const;
  void saveTranscriptFindings(const QJsonObject &chunk, const QJsonObject &result, const QString &provider);
  QJsonArray transcriptFindings() const;
  QString transcriptFindingEvidence(const QString &id) const;
  void reviewTranscriptFinding(const QString &id, bool accept);
  void forgetConversations(const QString &source);
  void deleteForgottenConversations();

  QJsonArray evidence() const;
  QHash<QString, QString> evidenceHashes(const QJsonArray &provided) const;
  QJsonObject briefing(const QString &query, int limit = 8, bool semantic = true, int inputScale = 1, int textBudget = 0) const;
  QJsonObject answerPrompt(const QString &question, int contextTokens = 0) const;
  static constexpr int localAnswerTokens = 2048;
  QJsonObject saveAnswer(const QJsonObject &answer, const QJsonArray &evidence, const QString &question);
  void reviewMemory(int id, const QString &action, const QString &correction = {});
  QJsonObject verifyPrompt(int id) const;
  QJsonObject prepareVerification(int id, bool queued, int inputScale = 1, bool completion = false);
  // Memories waiting for a check, oldest first. Entries without evidence
  // rejoin the queue when the verification evidence changes.
  QJsonArray reviewQueue() const;
  QJsonArray reviewQueue(const QJsonArray &memoryList) const;
  // The confidence (0 = off, 1-100) at which the model settles memories itself;
  // setting it also settles existing memories whose recorded confidence meets it.
  int autoReview() const;
  // The user's instructions sent with every wiki build (build) or check
  // (verify), as stored per project; empty means none.
  static QJsonArray promptPresets();
  QString promptPresetId(const QString &kind) const;
  void setPromptPreset(const QString &kind, const QString &id);
  void setAutoReview(int threshold);
  QJsonObject saveVerification(int id, const QJsonObject &result, const QJsonArray &evidence, const QString &provider, bool completion = false);
  QJsonArray activity() const;
  QJsonArray deliveries() const;
  void recordDelivery(const QString &event, const QString &session, const QString &query, const QJsonObject &briefing);
  QJsonObject semanticStatus() const;
  QJsonObject buildSemanticIndex();
  void setSemanticEnabled(bool enabled);
  void downloadSemanticModel();
  void setCancellation(const std::shared_ptr<std::atomic_bool> &flag) { m_cancel = flag; }
  // Progress of a long task, read by the interface while a worker runs it:
  // stage 1 is the model download (bytes), stage 2 semantic indexing (excerpts).
  struct Progress { std::atomic<qint64> done{0}, total{0}; std::atomic<int> stage{0}; };
  void setProgress(const std::shared_ptr<Progress> &progress) { m_progress = progress; }
  QString context() const;
  QJsonObject recentHandoff(const QString &excludeTranscript) const;
  QJsonObject handoff(const QString &source = {}) const;
  QJsonObject transcriptHandoff(const QString &path) const;
  void saveHandoffNotes(const QString &source, const QJsonObject &notes);
  QJsonObject openEvidence(const QString &id) const;
  void saveClearCheckpoint(const QJsonObject &hook);
  QJsonObject recoverClearCheckpoint(const QJsonObject &hook);
  bool exportsStale() const;
  // small: a batch sized for a local model's context window. Learning then
  // takes more, smaller rounds; no message is ever truncated.
  // Records with nothing said (tool calls and output, hook text) are marked
  // learned here instead of being sent.
  // limit (when above 0) caps how many messages the batch holds.
  QJsonObject learningBatch(bool small = false, int limit = 0, int inputScale = 1);
  // Marks messages learned without a memory: ones a model could not process
  // even alone, so a long unattended build keeps moving. They stay archived.
  void skipMessages(const QJsonArray &batch);
  QJsonArray skippedLearning() const;
  int retrySkippedLearning(const QString &source = {});
  int reusePublishedTranscriptLearning();
  void learn(const QJsonObject &result, const QJsonArray &batch, const QString &provider);
  void connectAgents(const QString &binary);
  // Removes Lore's hooks and managed blocks from a folder that is no longer
  // this memory's source. Unrelated hooks and text are preserved.
  static void disconnectAgents(const QString &folder);
  static void removeManaged(const QString &path);
  void checkpoint();
  // Migration between working directories. migrationTarget validates and
  // normalizes a destination; copyTo copies the archive there and checks the
  // copy has the same records; removeArchive deletes the old copy afterwards.
  static QString defaultWorkingDirectory(const QString &source);
  // The folder that holds default working directories (Documents, or
  // LORE_WORKSPACE_ROOT in tests).
  static QString workspaceRoot();
  QString migrationTarget(const QString &path) const;
  void copyTo(const QString &target);
  void copyConnectedTo(const QString &target, const QString &binary);
  void backupTo(const QString &target) const;
  static void restoreBackup(const QString &backup, const QString &target);
  static bool removeArchive(const QString &folder, const QString &source);
  void render();
  void deleteWiki();
  // Everything the app shows about the project, read in one pass so a worker
  // can prepare it and the window only swaps it in. fingerprint() is cheap
  // and changes whenever the snapshot could: a capture compares it first.
  QJsonObject snapshot() const;
  QString fingerprint() const;
  static void write(const QString &path, const QByteArray &data);
  static QString read(const QString &path);
  QString setting(const QString &key) const;
  void setSetting(const QString &key, const QString &value);
  static QJsonObject claudeHook(const QString &binary, const QString &project);
private:
  QSqlDatabase m_db;
  QString m_root, m_source, m_connection;
  bool m_personal = false;
  std::shared_ptr<std::atomic_bool> m_cancel;
  std::shared_ptr<Progress> m_progress;
  void sql(const QString &statement) const;
  QJsonObject stats(const QJsonArray &memoryList) const;
  QJsonArray evidence(const QJsonArray &memoryList, bool includeConversations) const;
  QJsonArray evidence(const QJsonArray &memoryList, bool includeConversations, const QSet<QString> *onlyEvents,
                      const QStringList *fileTerms = nullptr) const;
  QJsonArray functionEvidence() const;
  QJsonObject verifyPrompt(int id, const QJsonArray &memoryList, int inputScale = 1, bool completion = false) const;
  QString verificationEvidenceKey(const QJsonArray &memoryList) const;
  static QJsonObject reviewFeedback(const QJsonObject &memory);
  QJsonArray checkedCitations(const QString &text, const QJsonArray &refs, const QJsonArray &provided) const;
  void managed(const QString &path, const QString &text) const;
  void intelligenceSchema();
  void knowledgeSchema();
  void publishTranscriptFinding(const QString &id);
  bool conversationForgotten(const QString &source) const;
  void removeForgottenExports() const;
  void deleteWikiEntries(bool forgottenOnly);

  QString promptPresetText(const QString &kind) const;
  void logActivity(const QString &kind, const QString &body);
  void linkMemory(int id, const QJsonArray &files);
  // Records how sure the model is about a memory and, at or above the
  // project's threshold (setting autoReview, 0 = off), settles it: accepted
  // or outdated, marked as the model's decision. Never overrides the user.
  void settleByModel(int id, const QString &state, int confidence, const QString &reason, const QString &source);
};
