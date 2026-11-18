#pragma once

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
  explicit Store(const QString &project, bool create = false);
  ~Store();
  Store(const Store &) = delete;
  Store &operator=(const Store &) = delete;
  QString root() const { return m_root; }
  int ingest(const QString &path, const QString &provider);
  QJsonObject sync(const QString &codexRoot = {}, const QString &claudeRoot = {});
  QJsonObject stats() const;
  QJsonArray search(const QString &query, int limit = 40, bool anyTerms = false) const;
  QJsonArray session(const QString &id, int offset = 0, int limit = 100) const;
  QJsonArray sessions() const;
  QJsonArray memories() const;
  QJsonObject indexProject();
  QJsonArray projectFiles() const;
  QJsonArray evidence() const;
  QJsonObject briefing(const QString &query, int limit = 8, bool semantic = true) const;
  QJsonObject answerPrompt(const QString &question) const;
  QJsonObject saveAnswer(const QJsonObject &answer, const QJsonArray &evidence, const QString &question);
  void reviewMemory(int id, const QString &action, const QString &correction = {});
  QJsonArray activity() const;
  QJsonArray deliveries() const;
  void recordDelivery(const QString &event, const QString &session, const QString &query, const QJsonObject &briefing);
  QJsonObject semanticStatus() const;
  QJsonObject buildSemanticIndex();
  void setSemanticEnabled(bool enabled);
  void downloadSemanticModel();
  void setCancellation(const std::shared_ptr<std::atomic_bool> &flag) { m_cancel = flag; }
  QString context() const;
  QJsonObject learningBatch() const;
  void learn(const QJsonObject &result, const QJsonArray &batch, const QString &provider);
  void connectAgents(const QString &binary);
  void render();
  static void write(const QString &path, const QByteArray &data);
  static QString read(const QString &path);
  static QJsonObject claudeHook(const QString &binary, const QString &project);
private:
  QSqlDatabase m_db;
  QString m_root, m_connection;
  std::shared_ptr<std::atomic_bool> m_cancel;
  void sql(const QString &statement) const;
  void managed(const QString &path, const QString &text) const;
  void intelligenceSchema();
  void logActivity(const QString &kind, const QString &body);
  void linkMemory(int id, const QJsonArray &files);
};
