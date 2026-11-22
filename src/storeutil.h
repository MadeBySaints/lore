#pragma once
// Helpers shared by Store's source files (store.cpp, board.cpp). Internal to
// Lore; each file gets its own copy.
#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <stdexcept>

namespace {
[[noreturn]] inline void fail(const QString &s) { throw std::runtime_error(s.toStdString()); }
inline QString hash(const QByteArray &s) { return QString::fromLatin1(QCryptographicHash::hash(s, QCryptographicHash::Sha256).toHex().left(24)); }
inline void execute(QSqlQuery &q) { if (!q.exec()) fail(q.lastError().text()); }
inline QString compact(const QJsonValue &v) {
  return QString::fromUtf8(v.isArray() ? QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact)
                                       : QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
}
inline const QRegularExpression finishedList("^(done|complete[d]?|shipped|finished)$", QRegularExpression::CaseInsensitiveOption);
inline const QRegularExpression doingList("^(doing|in progress|working)$", QRegularExpression::CaseInsensitiveOption);
inline QString normalItem(const QString &s) { return s.simplified().toLower(); }
inline QStringList strings(const QJsonValue &v) { QStringList out; for (const auto &s : v.toArray()) out << s.toString(); return out; }
// SQLite NOT NULL columns: a missing field is an empty string, not NULL.
inline QString orEmpty(const QString &s) { return s.isNull() ? QStringLiteral("") : s; }
// The latest version of each memory: corrections and replacements follow
// the supersession chain.
struct Chains {
  QHash<int,QJsonObject> byId; QHash<int,int> next;
  explicit Chains(const QJsonArray &memoryList) {
    for (const auto &v : memoryList) { const auto o = v.toObject(); byId.insert(o.value("id").toInt(), o); if (const int by = o.value("supersededBy").toInt()) next.insert(o.value("id").toInt(), by); }
  }
  QJsonObject latest(int id) const { for (int guard = 0; next.contains(id) && guard < 1000; ++guard) id = next.value(id); return byId.value(id); }
  // Open work: a current next step or question, not outdated or forgotten.
  static bool open(const QJsonObject &o) {
    return !o.isEmpty() && !o.value("excluded").toBool() && o.value("supersededBy").toInt() == 0 && !QStringList{"outdated", "forgotten"}.contains(o.value("review").toString())
      && (o.value("kind").toString() == "next" || o.value("kind").toString() == "question");
  }
  // Finished: its latest version became a fact or decision, or was marked outdated.
  bool ended(int id) const {
    const auto o = latest(id); if (o.isEmpty() || o.value("excluded").toBool()) return false;
    return o.value("review").toString() == "outdated" || (o.value("kind").toString() != "next" && o.value("kind").toString() != "question");
  }
  bool allEnded(const QList<int> &ids) const { if (ids.isEmpty()) return false; for (int id : ids) if (!ended(id)) return false; return true; }
};
inline QList<int> ids(const QJsonValue &v) { QList<int> out; for (const auto &x : v.toArray()) if (x.toInt() > 0 && !out.contains(x.toInt())) out << x.toInt(); return out; }
// Every memory a planned card refers to: its own and its items'.
inline QList<int> cardMemories(const QJsonObject &card) {
  QList<int> out = ids(card.value("memories"));
  for (const auto &cl : card.value("checklists").toArray()) for (const auto &it : cl.toObject().value("items").toArray()) {
    for (int id : ids(it.toObject().value("memories"))) if (!out.contains(id)) out << id;
    for (const auto &ch : it.toObject().value("items").toArray()) for (int id : ids(ch.toObject().value("memories"))) if (!out.contains(id)) out << id;
  }
  return out;
}
inline QSet<int> plannedMemories(const QJsonArray &cards) {
  QSet<int> planned;
  for (const auto &v : cards) {
    const auto card = v.toObject();
    if (card.value("active").toBool())
      for (int id : cardMemories(card)) planned.insert(id);
  }
  return planned;
}
}
