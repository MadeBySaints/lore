#pragma once
#include "store.h"
#include "filepicker.h"
#include "toolbox.h"
#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <memory>

class Backend : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString project READ project NOTIFY changed)
  Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
  Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
  Q_PROPERTY(QVariantList memories READ memories NOTIFY changed)
  Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
  Q_PROPERTY(QString brain READ brain NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
  Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY statusChanged)
  Q_PROPERTY(bool autoLearning READ autoLearning WRITE setAutoLearning NOTIFY statusChanged)
  Q_PROPERTY(QString learningProvider READ learningProvider WRITE setLearningProvider NOTIFY statusChanged)
  Q_PROPERTY(QString selectedProject READ selectedProject NOTIFY changed)
  Q_PROPERTY(bool connected READ connected NOTIFY changed)
  Q_PROPERTY(QString error READ error NOTIFY statusChanged)
  Q_PROPERTY(bool selecting READ selecting NOTIFY statusChanged)
  Q_PROPERTY(bool capturing READ capturing NOTIFY statusChanged)
  Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY changed)
  Q_PROPERTY(Toolbox *tools READ tools CONSTANT)
  Q_PROPERTY(QVariantList files READ files NOTIFY changed)
  Q_PROPERTY(QVariantList activity READ activity NOTIFY changed)
  Q_PROPERTY(QVariantList deliveries READ deliveries NOTIFY changed)
  Q_PROPERTY(QVariantMap semantic READ semantic NOTIFY changed)
  Q_PROPERTY(QVariantMap answer READ answer NOTIFY changed)
  Q_PROPERTY(QVariantMap preview READ preview NOTIFY changed)
  Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled NOTIFY statusChanged)
  Q_PROPERTY(QVariantList notices READ notices NOTIFY statusChanged)
public:
  struct CapturePaths { QString codex, claude; };
  explicit Backend(QObject *parent = nullptr, FilePicker *picker = nullptr, const QString &preferences = {}, CapturePaths capturePaths = {});
  ~Backend() override;
  QString project() const { return m_store ? m_store->root() : QString(); }
  QVariantMap stats() const { return m_stats; }
  QVariantList sessions() const { return m_sessions; }
  QVariantList memories() const { return m_memories; }
  QVariantList results() const { return m_results; }
  QString brain() const { return m_brain; }
  QString status() const { return m_status; }
  bool busy() const { return m_process.state() != QProcess::NotRunning || m_work.isRunning(); }
  bool watching() const { return m_watch.isActive(); }
  void setWatching(bool on);
  bool autoLearning() const { return m_autoLearning; }
  void setAutoLearning(bool on);
  QString learningProvider() const { return m_learningProvider; }
  void setLearningProvider(const QString &provider);
  QString selectedProject() const { return m_selectedProject; }
  bool connected() const { return m_connected; }
  QString error() const { return m_errorMessage; }
  bool selecting() const { return !m_pickerPurpose.isEmpty(); }
  bool capturing() const { return m_capturing; }
  QVariantList recentProjects() const;
  Toolbox *tools() { return &m_tools; }
  QVariantList files() const { return m_files; }
  QVariantList activity() const { return m_activity; }
  QVariantList deliveries() const { return m_deliveries; }
  QVariantMap semantic() const { return m_semantic; }
  QVariantMap answer() const { return m_answer; }
  QVariantMap preview() const { return m_preview; }
  bool notificationsEnabled() const { return m_notificationsEnabled; }
  void setNotificationsEnabled(bool on);
  QVariantList notices() const { return m_notices; }
  Q_INVOKABLE void indexProject();
  Q_INVOKABLE void updateSemantic(bool download = false);
  Q_INVOKABLE void disableSemantic();
  Q_INVOKABLE void installSemanticRuntime();
  Q_INVOKABLE void openReleases();
  Q_INVOKABLE void ask(const QString &question);
  Q_INVOKABLE void previewContext(const QString &query);
  Q_INVOKABLE void reviewMemory(int id, const QString &action, const QString &correction = {});
  Q_INVOKABLE void chooseProject();
  Q_INVOKABLE void locateTool(const QString &id);
  Q_INVOKABLE bool selectProject(const QString &path);
  Q_INVOKABLE bool connectProject();
  Q_INVOKABLE void clearError();
  Q_INVOKABLE bool openProject(const QString &path, bool create = false);
  Q_INVOKABLE void sync();
  Q_INVOKABLE void search(const QString &text);
  Q_INVOKABLE void learn(const QString &provider);
  Q_INVOKABLE void connectAgents();
  Q_INVOKABLE QString readPage(const QString &relative);
  Q_INVOKABLE QString markdown(const QString &text, const QString &accent) const;
  Q_INVOKABLE void reveal();
  Q_INVOKABLE void cancel();
signals:
  void changed();
  void resultsChanged();
  void statusChanged();
  void learningFinished(bool ok);
private:
  std::unique_ptr<Store> m_store;
  QVariantMap m_stats;
  QVariantList m_sessions, m_memories, m_results;
  QString m_brain, m_status, m_query, m_provider;
  QJsonArray m_batch;
  QByteArray m_output, m_error;
  QProcess m_process;
  QTimer m_watch, m_timeout, m_auto;
  bool m_cancelled = false;
  bool m_autoLearning = false;
  QString m_learningProvider = "codex";
  Toolbox m_tools;
  FilePicker *m_picker;
  QString m_pickerPurpose, m_preferences, m_selectedProject, m_errorMessage;
  QStringList m_recent;
  QVariantMap m_projectOptions;
  bool m_connected = false, m_restoring = false, m_capturing = false;
  CapturePaths m_capturePaths;
  QFutureWatcher<QJsonObject> m_capture;
  QFutureWatcher<QJsonObject> m_work;
  std::shared_ptr<std::atomic_bool> m_cancelWork;
  QVariantList m_files, m_activity, m_deliveries;
  QVariantMap m_semantic, m_answer, m_preview;
  QString m_workKind, m_operation, m_question;
  bool m_notificationsEnabled = true;
  QVariantList m_notices;
  QTimer m_notificationExpiry;
  QJsonArray m_answerEvidence;
  void work(const QString &kind, const QString &query = {});
  void startModel(const QString &prompt, const QString &provider);
  void refresh();
  void message(const QString &text, bool error = false);
  void savePreferences();
  void restorePreferences();
  bool connectionValid() const;
};
