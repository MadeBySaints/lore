#pragma once
#include <QLockFile>
#include "store.h"
#include "filepicker.h"
#include "toolbox.h"
#include "memorymodel.h"
#include <QCoreApplication>
#include <QColor>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QStringDecoder>
#include <QTemporaryFile>
#include <QTimer>
#include <QVariantList>
#include <functional>
#include <memory>

class QQuickTextDocument;
Q_MOC_INCLUDE(<QQuickTextDocument>)

class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString project READ project NOTIFY changed)
  Q_PROPERTY(QString version READ version CONSTANT)
  Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged)
  Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
  Q_PROPERTY(QVariantList memories READ memories NOTIFY memoriesChanged)
  Q_PROPERTY(int hiddenWikiEntries READ hiddenWikiEntries NOTIFY memoriesChanged)
  // Open next steps, newest first: id, title, kind. Computed with the wiki
  // list so QML never filters every memory itself.
  Q_PROPERTY(QVariantList nextSteps READ nextSteps NOTIFY memoriesChanged)
  Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
  Q_PROPERTY(bool searching READ searching NOTIFY resultsChanged)
  Q_PROPERTY(QString searchError READ searchError NOTIFY resultsChanged)
  Q_PROPERTY(QString brain READ brain NOTIFY brainChanged)
  Q_PROPERTY(QString status READ status NOTIFY messageChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
  Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY statusChanged)
  Q_PROPERTY(bool autoLearning READ autoLearning WRITE setAutoLearning NOTIFY statusChanged)
  Q_PROPERTY(QString learningProvider READ learningProvider WRITE setLearningProvider NOTIFY statusChanged)
  Q_PROPERTY(QString answerProvider READ answerProvider WRITE setAnswerProvider NOTIFY statusChanged)
  Q_PROPERTY(int modelRevision READ modelRevision NOTIFY modelChoicesChanged)
  // The model for Verify and Verify all, per project, and the progress of a
  // Verify all run: active, done, failed, total; reviewWaiting counts the
  // memories it would check.
  Q_PROPERTY(QString verifyProvider READ verifyProvider WRITE setVerifyProvider NOTIFY statusChanged)
  Q_PROPERTY(QVariantMap verifyProgress READ verifyProgress NOTIFY statusChanged)
  Q_PROPERTY(int reviewWaiting READ reviewWaiting NOTIFY statsChanged)
  // The model settles memories it is at least this sure of (0 = off).
  Q_PROPERTY(int autoReview READ autoReview WRITE setAutoReview NOTIFY statsChanged)
  Q_PROPERTY(QString autoProvider READ autoProvider WRITE setAutoProvider NOTIFY statusChanged)
  Q_PROPERTY(int autoAfter READ autoAfter WRITE setAutoAfter NOTIFY statusChanged)
  Q_PROPERTY(int autoEvery READ autoEvery WRITE setAutoEvery NOTIFY statusChanged)
  Q_PROPERTY(QString autoStatus READ autoStatus NOTIFY statusChanged)
  // Automatic verification while idle: Verify all with its own model and
  // triggers (entries waiting, every N hours), only when no wiki build is
  // running or due. A build that becomes due takes over between checks.
  Q_PROPERTY(bool autoVerify READ autoVerify WRITE setAutoVerify NOTIFY statusChanged)
  Q_PROPERTY(QString autoVerifyProvider READ autoVerifyProvider WRITE setAutoVerifyProvider NOTIFY statusChanged)
  Q_PROPERTY(int autoVerifyAfter READ autoVerifyAfter WRITE setAutoVerifyAfter NOTIFY statusChanged)
  Q_PROPERTY(int autoVerifyEvery READ autoVerifyEvery WRITE setAutoVerifyEvery NOTIFY statusChanged)
  Q_PROPERTY(QString autoVerifyStatus READ autoVerifyStatus NOTIFY statusChanged)
  // Seconds Lore must stay idle (no model call, no new conversation records)
  // before an automatic build or check starts; a countdown shows meanwhile.
  Q_PROPERTY(int autoBuildDelay READ autoBuildDelay WRITE setAutoBuildDelay NOTIFY statusChanged)
  Q_PROPERTY(int autoVerifyDelay READ autoVerifyDelay WRITE setAutoVerifyDelay NOTIFY statusChanged)
  // The running countdown: kind (build or verify) and seconds left; empty when none.
  Q_PROPERTY(QVariantMap countdown READ countdown NOTIFY countdownChanged)
  // Every automation waits until then (Pause all); empty when not paused.
  Q_PROPERTY(QString pausedUntil READ pausedUntil NOTIFY statusChanged)
  Q_PROPERTY(QString selectedProject READ selectedProject NOTIFY changed)
  Q_PROPERTY(bool personalWiki READ personalWiki NOTIFY changed)
  Q_PROPERTY(bool personalSetup READ personalSetup WRITE setPersonalSetup NOTIFY changed)
  Q_PROPERTY(bool personalCapture READ personalCapture WRITE setPersonalCapture NOTIFY statusChanged)
  Q_PROPERTY(QString source READ source NOTIFY changed)
  Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY changed)
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY statusChanged)
  Q_PROPERTY(bool selecting READ selecting NOTIFY statusChanged)
  Q_PROPERTY(bool capturing READ capturing NOTIFY statusChanged)
  Q_PROPERTY(bool showBusy READ showBusy NOTIFY statusChanged)
  // A model call (Build wiki, Ask, Verify) and background work (capture,
  // reviews, indexing, search) run side by side; only model calls exclude
  // each other.
  Q_PROPERTY(bool modelBusy READ modelBusy NOTIFY statusChanged)
  Q_PROPERTY(bool workBusy READ workBusy NOTIFY statusChanged)
  // A wiki build in progress: active, automatic, provider, done, pending,
  // total, rounds, running.
  Q_PROPERTY(QVariantMap buildProgress READ buildProgress NOTIFY statusChanged)
  Q_PROPERTY(bool showCapturing READ showCapturing NOTIFY statusChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY changed)
  Q_PROPERTY(QVariantList knownProjects READ knownProjects NOTIFY projectsChanged)
  Q_PROPERTY(QVariantList suggestedProjects READ suggestedProjects NOTIFY projectsChanged)
  Q_PROPERTY(bool scanningProjects READ scanningProjects NOTIFY projectsChanged)
  Q_PROPERTY(Toolbox *tools READ tools CONSTANT)
  Q_PROPERTY(MemoryModel *memoryModel READ memoryModel CONSTANT)
  Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)
  Q_PROPERTY(QVariantList activity READ activity NOTIFY activityChanged)
  Q_PROPERTY(QVariantList deliveries READ deliveries NOTIFY activityChanged)
  Q_PROPERTY(QVariantMap semantic READ semantic NOTIFY statsChanged)
  // A running model download or semantic indexing: stage (download/index), done, total.
  Q_PROPERTY(QVariantMap semanticProgress READ semanticProgress NOTIFY progressChanged)
  Q_PROPERTY(QVariantMap answer READ answer NOTIFY answerChanged)
  Q_PROPERTY(QVariantMap preview READ preview NOTIFY answerChanged)
  Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY statusChanged)
  Q_PROPERTY(QVariantList notices READ notices NOTIFY statusChanged)
  // What the model is writing as it works (Claude Code, Codex, or a local
  // model), for the read-only Model output page. Capped; newest last.
  Q_PROPERTY(QString liveOutput READ liveOutput NOTIFY liveChanged)
  Q_PROPERTY(QVariantList liveIssues READ liveIssues NOTIFY liveChanged)
  Q_PROPERTY(QVariantMap activeModelCall READ activeModelCall NOTIFY statusChanged)
  // Tokens used per agent (claude, codex, local): this session and in total,
  // each with calls, input, output, and the last call's context (lastInput)
  // and window when the agent reports it.
  Q_PROPERTY(QVariantMap usage READ usage NOTIFY usageChanged)
  // Instructions the user adds to what Lore sends: built-in templates plus
  // their own prompts (kept app-wide), and the one chosen per project for
  // wiki builds and for checks. Each item: id, name, text, purpose, builtIn.
  Q_PROPERTY(QString functionScanProvider READ functionScanProvider NOTIFY knowledgeChanged)
  Q_PROPERTY(QString transcriptAnalysisProvider READ transcriptAnalysisProvider NOTIFY knowledgeChanged)
  Q_PROPERTY(QVariantList functions READ functions NOTIFY knowledgeChanged)
  Q_PROPERTY(QVariantList transcriptFindings READ transcriptFindings NOTIFY knowledgeChanged)
  Q_PROPERTY(bool projectFileGrid READ projectFileGrid WRITE setProjectFileGrid NOTIFY knowledgeChanged)
  Q_PROPERTY(int indexedFunctionFiles READ indexedFunctionFiles NOTIFY knowledgeChanged)
  Q_PROPERTY(int eligibleFunctionFiles READ eligibleFunctionFiles NOTIFY knowledgeChanged)
  Q_PROPERTY(QVariantMap knowledgeProgress READ knowledgeProgress NOTIFY statusChanged)
  Q_PROPERTY(bool transcriptAutoPublish READ transcriptAutoPublish NOTIFY knowledgeChanged)
  Q_PROPERTY(int transcriptThreshold READ transcriptThreshold NOTIFY knowledgeChanged)
  Q_PROPERTY(QVariantList promptPresets READ promptPresets CONSTANT)
  // One-click prompts that open Claude Code or Codex in the project: built-in
  // ones plus the user's own (app-wide), and the agent they run with.
  Q_PROPERTY(QVariantList quickCommands READ quickCommands NOTIFY promptsChanged)
  Q_PROPERTY(QString quickAgent READ quickAgent WRITE setQuickAgent NOTIFY promptsChanged)
  Q_PROPERTY(QString buildPreset READ buildPreset WRITE setBuildPreset NOTIFY presetsChanged)
  Q_PROPERTY(QString verifyPreset READ verifyPreset WRITE setVerifyPreset NOTIFY presetsChanged)
public:
  struct CapturePaths { QString codex, claude; };
  explicit Backend(QObject *parent = nullptr, FilePicker *picker = nullptr, const QString &preferences = {}, CapturePaths capturePaths = {});
  ~Backend() override;
  QString project() const { return m_store ? m_store->root() : QString(); }
  QString version() const { return QCoreApplication::applicationVersion(); }
  QVariantMap stats() const { return m_stats; }
  QVariantList sessions() const { return m_sessions; }
  QVariantList memories() const { return m_memories; }
  QVariantList nextSteps() const { return m_nextSteps; }
  QVariantList results() const { return m_results; }
  bool searching() const { return m_searchPending; }
  QString searchError() const { return m_searchError; }
  QString brain() const { return m_brain; }
  QString status() const { return m_status; }
  bool busy() const { return modelRunning() || m_work.isRunning() || !m_knowledgeJob.isEmpty(); }
  int modelRevision() const { return m_modelRevision; }
  Q_INVOKABLE QVariantMap modelChoice(const QString &task, const QString &provider) const;
  Q_INVOKABLE bool modelReady(const QString &task, const QString &provider) const;
  // Whether the local model chosen for a task is running (not its backup).
  Q_INVOKABLE bool localReady(const QString &task) const;
  Q_INVOKABLE void setModelChoice(const QString &task, const QString &provider, const QString &field, const QString &value);
  bool watching() const { return m_watch.isActive(); }
  void setWatching(bool on);
  bool autoLearning() const { return m_autoLearning; }
  void setAutoLearning(bool on);
  QString learningProvider() const { return m_learningProvider; }
  void setLearningProvider(const QString &provider);
  // Build wiki classifies messages and suits a small local model; Ask and
  // Verify reason over evidence and can use a different provider.
  QString answerProvider() const { return m_answerProvider; }
  void setAnswerProvider(const QString &provider);
  QString verifyProvider() const { return m_verifyProvider; }
  void setVerifyProvider(const QString &provider);
  QVariantMap verifyProgress() const { return {{"active", !m_verifyQueue.isEmpty() || m_verifyRunning}, {"done", m_verifyDone}, {"failed", m_verifyFailed}, {"total", m_verifyTotal}}; }
  int reviewWaiting() const { return m_reviewWaiting; }
  int autoReview() const { return m_stats.value("autoReview").toInt(); }
  void setAutoReview(int threshold);
  // Automatic wiki builds, per project: their own model, and triggers for
  // "this many messages are waiting" and/or "every this many hours". A
  // triggered build continues round by round until nothing is waiting.
  // A trigger of 0 is off; with both off, any waiting message counts.
  QString autoProvider() const { return m_autoProvider; }
  void setAutoProvider(const QString &provider);
  int autoAfter() const { return m_autoAfter; }
  void setAutoAfter(int messages);
  int autoEvery() const { return m_autoEvery; }
  void setAutoEvery(int hours);
  QString autoStatus() const;
  bool autoVerify() const { return m_autoVerify; }
  void setAutoVerify(bool on);
  QString autoVerifyProvider() const { return m_autoVerifyProvider; }
  void setAutoVerifyProvider(const QString &provider);
  int autoVerifyAfter() const { return m_autoVerifyAfter; }
  void setAutoVerifyAfter(int entries);
  int autoVerifyEvery() const { return m_autoVerifyEvery; }
  void setAutoVerifyEvery(int hours);
  QString autoVerifyStatus() const;
  int autoBuildDelay() const { return m_autoBuildDelay; }
  void setAutoBuildDelay(int seconds);
  int autoVerifyDelay() const { return m_autoVerifyDelay; }
  void setAutoVerifyDelay(int seconds);
  QString pausedUntil() const { return paused() ? m_pauseAll.toString(Qt::ISODate) : QString(); }
  Q_INVOKABLE void pauseAutomations(int minutes);
  Q_INVOKABLE QVariantList connectionPreview() const;
  Q_INVOKABLE QVariantMap folderFacts(const QString &path) const;
  Q_INVOKABLE void resumeAutomations();
  QVariantMap countdown() const { return m_countdownKind.isEmpty() ? QVariantMap() : QVariantMap{{"kind", m_countdownKind}, {"seconds", m_countdownLeft}}; }
  QString selectedProject() const { return m_selectedProject; }
  bool personalWiki() const { return m_store && m_store->personalWiki(); }
  bool personalSetup() const { return personalWiki() || (!m_store && m_personalSetup); }
  void setPersonalSetup(bool enabled);
  bool personalCapture() const { return personalWiki() && m_personalCapture; }
  void setPersonalCapture(bool enabled);
  Q_INVOKABLE void reconnectPersonalHooks();
  // The project being remembered, and where Lore keeps its files for it.
  QString source() const { return m_store ? m_store->source() : m_selectedProject; }
  QString workingDirectory() const { return m_store ? m_store->root() : m_workingDirectory; }
  static QString defaultWorkingDirectory(const QString &source) { return Store::defaultWorkingDirectory(source); }
  Q_INVOKABLE QString suggestedWorkingDirectory() const { return m_store ? Store::defaultWorkingDirectory(source()) : QString(); }
  bool connected() const { return m_connected; }
  QString error() const { return m_errorMessage; }
  bool selecting() const { return !m_pickerPurpose.isEmpty(); }
  bool capturing() const { return m_capturing; }
  // What the interface shows. Background work (automatic capture, index refreshes, search as you type)
  // stays invisible unless it runs longer than a moment, so controls don't flicker every few seconds.
  bool showBusy() const { return modelRunning() || (m_work.isRunning() && (!m_quietWork || m_revealed)); }
  bool modelBusy() const { return modelRunning(); }
  bool workBusy() const { return m_work.isRunning() && (!m_quietWork || m_revealed); }
  QVariantMap buildProgress() const;
  bool showCapturing() const { return m_capturing && (!m_quietCapture || m_revealed); }
  QVariantList recentProjects() const;
  // Every project Lore has opened or found, with its own source and working
  // directory, newest first. Suggestions are folders used with Claude Code or
  // Codex that have no Lore memory yet.
  QVariantList knownProjects() const;
  QVariantList suggestedProjects() const { return m_suggested; }
  bool scanningProjects() const { return m_scan.isRunning(); }
  Toolbox *tools() { return &m_tools; }
  MemoryModel *memoryModel() { return &m_memoryModel; }
  QVariantList files() const { return m_files; }
  QVariantList activity() const { return m_activity; }
  QVariantList deliveries() const { return m_deliveries; }
  QVariantMap semantic() const { return m_semantic; }
  QVariantMap semanticProgress() const { return m_semanticView; }
  QVariantMap answer() const { return m_answer; }
  QVariantMap preview() const { return m_preview; }
  bool notificationsEnabled() const { return m_notificationsEnabled; }
  void setNotificationsEnabled(bool on);
  QVariantList notices() const { return m_notices; }
  QString liveOutput() const { return m_live; }
  QVariantList liveIssues() const { return m_liveIssues; }
  Q_INVOKABLE void styleTerminal(QQuickTextDocument *document, const QColor &accent,
                                 const QColor &warning, const QColor &caution);
  QVariantMap activeModelCall() const;
  QVariantMap usage() const { return {{"session", m_sessionUsage}, {"total", m_totalUsage}}; }
  QVariantList promptPresets() const;
  QString buildPreset() const;
  QString verifyPreset() const;
  void setBuildPreset(const QString &id);
  void setVerifyPreset(const QString &id);

  QVariantList quickCommands() const;
  QString quickAgent() const { return m_quickAgent; }
  void setQuickAgent(const QString &agent) { if (agent != "claude" && agent != "codex") return; m_quickAgent = agent; savePreferences(); emit promptsChanged(); }
  Q_INVOKABLE QString saveQuickCommand(const QString &id, const QString &name, const QString &text);
  Q_INVOKABLE void deleteQuickCommand(const QString &id);
  Q_INVOKABLE void runQuickCommand(const QString &id);
  Q_INVOKABLE void workOnStep(int id);
  Q_INVOKABLE void indexProject();
  Q_INVOKABLE void updateSemantic(bool download = false);
  Q_INVOKABLE void disableSemantic();
  Q_INVOKABLE void installSemanticRuntime();
  Q_INVOKABLE void openReleases();
  Q_INVOKABLE void ask(const QString &question);
  Q_INVOKABLE void previewContext(const QString &query);
  QVariantList functions() const { return m_functions; }
  QVariantList transcriptFindings() const { return m_transcriptFindings; }
  int indexedFunctionFiles() const { return m_indexedFunctionFiles; }
  int eligibleFunctionFiles() const { return m_eligibleFunctionFiles; }
  int hiddenWikiEntries() const { return m_hiddenWikiEntries; }
  QVariantMap knowledgeProgress() const;
  bool transcriptAutoPublish() const;
  int transcriptThreshold() const;
  QString functionScanProvider() const;
  QString transcriptAnalysisProvider() const;
  Q_INVOKABLE void setKnowledgeProvider(const QString &task, const QString &provider);
  Q_INVOKABLE QString transcriptEvidence(const QString &id);
  bool projectFileGrid() const;
  void setProjectFileGrid(bool enabled);
  Q_INVOKABLE void startFunctionIndex(const QString &provider, bool rerun);
  Q_INVOKABLE void startTranscriptAnalysis(const QString &source, const QString &provider, bool rerun);
  Q_INVOKABLE void setTranscriptPublishing(bool enabled, int threshold);
  Q_INVOKABLE void reviewTranscriptFinding(const QString &id, bool accept);
  Q_INVOKABLE void forgetConversations(const QString &source);
  Q_INVOKABLE void deleteForgottenConversations();
  Q_INVOKABLE void deleteWiki();
  Q_INVOKABLE void retrySkippedLearning(const QString &source);
  Q_INVOKABLE void reviewMemory(int id, const QString &action, const QString &correction = {});
  Q_INVOKABLE void verifyMemory(int id);
  // Checks every memory awaiting review, one model call each, until done or
  // Cancel. A failed check is counted and skipped.
  Q_INVOKABLE void verifyAll();
  Q_INVOKABLE void checkCompletion(int id);
  Q_INVOKABLE QVariantList skills(const QString &agent) const;
  Q_INVOKABLE QString skillText(const QString &name) const;
  Q_INVOKABLE bool saveSkill(const QString &markdown, const QString &origin);
  Q_INVOKABLE void setSkillEnabled(const QString &name, const QString &agent, bool enabled);
  Q_INVOKABLE void importSkill(const QString &url);
  Q_INVOKABLE void buildSkill(const QString &goal, const QVariantList &answers, bool draftNow);
  Q_INVOKABLE void chooseProject();
  Q_INVOKABLE void scanProjects();
  Q_INVOKABLE bool openKnownProject(const QString &working);
  Q_INVOKABLE void forgetProject(const QString &path);
  Q_INVOKABLE void chooseWorkingDirectory();
  Q_INVOKABLE bool setWorkingDirectory(const QString &path);
  // Settings for a connected project. Pickers propose a folder; the page
  // confirms before either change is applied.
  Q_INVOKABLE void chooseSettingsFolder(const QString &kind);
  Q_INVOKABLE bool changeSource(const QString &path);
  Q_INVOKABLE bool changeWorkingDirectory(const QString &path);
  Q_INVOKABLE void locateTool(const QString &id);
  Q_INVOKABLE bool selectProject(const QString &path);
  Q_INVOKABLE bool connectProject();
  Q_INVOKABLE void clearError();
  Q_INVOKABLE bool openProject(const QString &path, bool create = false);
  Q_INVOKABLE void sync();
  Q_INVOKABLE bool saveConversation(const QString &text);
  Q_INVOKABLE void search(const QString &text);
  Q_INVOKABLE void searchProjects(const QString &text, bool allProjects);
  Q_INVOKABLE void learn(const QString &provider);
  Q_INVOKABLE void connectAgents();
  Q_INVOKABLE QString readPage(const QString &relative, const QString &archive = {});
  Q_INVOKABLE QVariantMap readHandoff(const QString &session = {}) const;
  Q_INVOKABLE void copyHandoff(const QString &text);
  Q_INVOKABLE bool saveHandoff(const QString &session, const QVariantMap &notes);
  Q_INVOKABLE void chooseChatGptExport();
  Q_INVOKABLE QVariantList previewChatGptExport(const QString &path);
  Q_INVOKABLE void importChatGptExport(const QString &path, const QString &conversation);
  Q_INVOKABLE QString markdown(const QString &text, const QString &accent) const;
  Q_INVOKABLE void reveal();
  Q_INVOKABLE void cancel();
signals:
  void skillsChanged();
  void skillImported(const QString &markdown, const QString &origin, const QString &error);
  void skillBuilt(const QString &question, const QString &markdown, const QString &error);
  // The project itself: which one, its folders, and whether it is connected.
  void changed();
  // Each part of the project view notifies on its own and only when it
  // differs, so a capture tick or a countdown does not re-run every binding.
  void statsChanged();
  void brainChanged();
  void activityChanged();
  void answerChanged();
  void countdownChanged();
  void progressChanged();
  void messageChanged();
  void usageChanged();
  void projectsChanged();
  // Lists notify separately and only when their contents differ, so a
  // background capture or a preference save does not rebuild the wiki.
  void sessionsChanged();
  void memoriesChanged();
  void filesChanged();
  void resultsChanged();
  void chatGptExportSelected(const QString &path);
  void statusChanged();
  void modelChoicesChanged();
  void learningFinished(bool ok);
  void folderProposed(const QString &kind, const QString &path);
  void liveChanged();
  // New model output since the last signal, batched about ten times a second.
  void liveAppended(const QString &text);
  void promptsChanged();
  void knowledgeChanged();
  void presetsChanged();
private:
  std::unique_ptr<Store> m_store;
  QVariantMap m_stats;
  QVariantList m_sessions, m_memories, m_results, m_nextSteps;
  QString m_fingerprint, m_shownProject;
  QString m_brain, m_status, m_query, m_provider;
  QJsonArray m_batch;
  QByteArray m_output, m_error;
  QProcess m_process;
  QTimer m_watch, m_timeout, m_auto;
  bool m_cancelled = false;
  bool m_functionTimedOut = false;
  bool m_autoLearning = false;
  QString m_learningProvider = "codex", m_answerProvider = "codex", m_verifyProvider = "codex", m_autoProvider = "codex", m_lastAutoBuild;
  int m_autoAfter = 25, m_autoEvery = 0;
  bool m_autoRun = false;
  QString m_manualProvider;  // The provider a Build wiki press is working through the backlog with.
  int m_buildDone = 0, m_buildRounds = 0;
  bool m_retried = false;
  QDateTime m_autoSnooze;  // Cancel pauses automatic builds until then.
  QTimer m_schedule;
  QNetworkAccessManager m_network;
  QString terminalTime() const;
  QPointer<QNetworkReply> m_reply;
  Toolbox m_tools;
  FilePicker *m_picker;
  QString m_pickerPurpose, m_preferences, m_selectedProject, m_workingDirectory, m_errorMessage;
  bool m_personalSetup = false, m_personalCapture = false;
  bool openProjectMode(const QString &path, bool create, bool personal);
  QStringList m_recent;
  QVariantMap m_projectOptions;
  QVariantMap m_modelOptions;
  int m_modelRevision = 0;
  QString m_activeModel, m_activeEffort;
  QVariantList m_suggested;
  QStringList m_hiddenProjects;
  QFutureWatcher<QJsonObject> m_scan;
  bool m_connected = false, m_restoring = false, m_capturing = false;
  CapturePaths m_capturePaths;
  QFutureWatcher<QJsonObject> m_capture;
  QFutureWatcher<QJsonObject> m_work;
  QFutureWatcher<QJsonObject> m_search;
  QString m_searchError;
  quint64 m_searchRevision = 0, m_runningSearch = 0;
  bool m_searchPending = false, m_searchActive = false, m_searchAllProjects = false;
  void startSearch();
  std::shared_ptr<std::atomic_bool> m_cancelWork;
  QVariantList m_files, m_activity, m_deliveries;
  QVariantMap m_semantic, m_answer, m_preview;
  QString m_live, m_livePending; QTimer m_liveFlush; QByteArray m_liveLine, m_localContent;
  QVariantList m_liveIssues;
  // The local model's recent output, to notice it repeating itself.
  QString m_loopWindow; qint64 m_loopSeen = 0, m_loopChecked = 0; bool m_loopStopped = false;
  // A looping local model is restarted with a fresh request (up to 3 times),
  // and a wiki batch that keeps failing on it is halved, then skipped.
  QString m_lastPrompt; int m_loopRetries = 0, m_batchLimit = 0; bool m_loopRetry = false;
  // A local call that lost its server continues once with the backup.
  bool m_backupTried = false, m_fallingBack = false;
  std::shared_ptr<Store::Progress> m_progress; QTimer m_progressPoll; QVariantMap m_semanticView;
  QVariantMap m_sessionUsage, m_totalUsage; int m_localContext = 0;
  QElapsedTimer m_modelTime;
  void recordUsage();
  QJsonArray m_legacyPrompts;
  QVariantList m_customCommands;
  QString m_quickAgent = "claude";
  void setPromptPreset(const QString &kind, const QString &id);
  void live(const QString &text);
  void liveIssue(const QString &text, const QString &kind);
  void resetModelOutput();
  QString modelTaskLabel() const;
  bool m_modelTextStreamed = false;
  void liveLines(const QByteArray &chunk);
  int m_autoBuildDelay = 30, m_autoVerifyDelay = 60, m_countdownLeft = 0;
  QString m_countdownKind; QTimer m_countdown;
  void startCountdown(const QString &kind);
  void stopCountdown();
  void startAutomatic(const QString &kind);
  bool m_autoVerify = false, m_verifyAuto = false;
  QString m_autoVerifyProvider = "codex", m_lastAutoVerify;
  int m_autoVerifyAfter = 10, m_autoVerifyEvery = 0;
  QDateTime m_verifySnooze;
  bool autoVerifyDue() const;
  QString verifyModel() const { return m_verifyAuto ? m_autoVerifyProvider : m_verifyProvider; }
  void updateSchedule();
  QList<int> m_verifyQueue; int m_verifyDone = 0, m_verifyFailed = 0, m_verifyTotal = 0, m_reviewWaiting = 0; bool m_verifyRunning = false;
  bool m_verifyHadError = false;
  void continueVerify();
  QDateTime m_pauseAll;
  bool paused() const { return m_pauseAll.isValid() && QDateTime::currentDateTimeUtc() < m_pauseAll; }
  QString m_workKind, m_operation, m_question;
  bool m_notificationsEnabled = true;
  QVariantList m_notices;
  QTimer m_notificationExpiry;
  QJsonArray m_answerEvidence, m_verifyEvidence;
  int m_askContext = 0;
  int m_verifyId = 0;
  bool m_quietCapture = false, m_quietWork = false, m_revealed = false;
  QTimer m_reveal;
  std::function<void()> m_whenIdle;
  struct Review { int id; QString action, correction; };
  QList<Review> m_reviews;
  QTimer m_semanticSoon;
  QVariantList m_shownSessions, m_shownMemories, m_shownFiles;
  MemoryModel m_memoryModel;
  mutable QList<QPair<QByteArray, QString>> m_rendered;
  void startReview();
  void emitLists(bool force = false);
  void work(const QString &kind, const QString &query = {}, bool quiet = false);
  void syncNow(bool quiet);
  bool deferUntilIdle(std::function<void()> action);
  void quietWorkStarted();
  void quietWorkEnded();
  void runDeferred();
  QString m_knowledgeJob, m_knowledgeProvider;
  QJsonArray m_knowledgeQueue, m_functionRecords;
  QJsonObject m_knowledgeChunk;
  int m_knowledgePosition = 0, m_nextFunctionLine = 1, m_failedFunctionFiles = 0;
  int m_functionRetryLines = 0;
  qint64 m_transcriptCursor = -1;
  bool m_knowledgeCancelled = false;
  QVariantList m_functions, m_transcriptFindings;
  int m_indexedFunctionFiles = 0, m_eligibleFunctionFiles = 0;
  int m_hiddenWikiEntries = 0, m_shownHiddenWikiEntries = 0;
  void refreshKnowledge();
  void startKnowledgeJob(const QString &kind, const QString &provider, const QJsonObject &options = {});
  void nextKnowledgeChunk();
  void acceptKnowledgeResult(const QJsonObject &result);
  void finishKnowledgeCommit(const QJsonObject &result);
  void retryFunctionChunk(const QString &error);
  void skipFunctionFile(const QString &error);
  void finishKnowledge(const QString &error = {});
  void startModel(const QString &prompt, const QString &provider);
  void finishSkill(const QJsonObject &result, const QString &error = {});
  QString m_skillProject;
  bool m_skillNeedsQuestion = false, m_skillDraftNow = false;
  void startLocalModel(const QString &prompt);
  void finishModel(QString answer, const QString &failure);
  bool modelRunning() const { return m_process.state() != QProcess::NotRunning || m_reply; }
  QString activeProvider() const;
  void stopModel();
  bool automationDue() const;
  void runAutomation();
  bool startRound(const QString &provider);
  void continueBuild();
  void endBuild();
  void refresh();
  void applySnapshot(const QJsonObject &snapshot);
  void emitAll();
  void quietMessage(const QString &text);
  void message(const QString &text, bool error = false);
  void savePreferences();
  void restorePreferences();
  bool connectionValid() const;
};
