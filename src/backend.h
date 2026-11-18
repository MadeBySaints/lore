#pragma once
#include "store.h"
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
public:
  explicit Backend(QObject *parent = nullptr);
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
  void setLearningProvider(const QString &provider) { m_learningProvider = provider; emit statusChanged(); }
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
  void refresh();
  void message(const QString &text);
};
