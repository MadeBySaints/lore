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
  bool busy() const { return m_process.state() != QProcess::NotRunning; }
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
  void refresh();
  void message(const QString &text, bool error = false);
  void savePreferences();
  void restorePreferences();
  bool connectionValid() const;
};
