#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>

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
  void sql(const QString &statement) const;
  void managed(const QString &path, const QString &text) const;
};
