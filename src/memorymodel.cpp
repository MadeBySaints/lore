#include "memorymodel.h"

namespace {
QVariant identity(const QVariant &entry) { return entry.toMap().value("id"); }

bool needsAttention(const QVariantMap &entry) {
  if (entry.value("supersededBy").toInt() || entry.value("excluded").toBool()) return false;
  if (!entry.value("verification").toMap().isEmpty() || !entry.value("completion").toMap().isEmpty()) return false;
  const QString review = entry.value("review").toString();
  if (review == "accepted" || review == "outdated" || review == "forgotten") return false;
  return review == "unreviewed" || entry.value("needsChecking").toBool() || !entry.value("conflicts").toList().isEmpty();
}
}

QVariant MemoryModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= m_entries.size() || role != EntryRole) return {};
  return m_entries.at(index.row());
}

void MemoryModel::setEntries(const QVariantList &entries) {
  const bool grew = entries.size() != m_all.size();
  m_all = entries; apply(matching());
  if (grew) emit countChanged();
}
void MemoryModel::setFilter(const QString &filter) {
  if (filter == m_filter) return;
  m_filter = filter; apply(matching()); emit filterChanged(); emit countChanged();
}
void MemoryModel::setAttentionOnly(bool enabled) {
  if (enabled == m_attentionOnly) return;
  m_attentionOnly = enabled;
  apply(matching());
  emit attentionOnlyChanged();
}
QVariantList MemoryModel::matching() const {
  const QStringList words = m_filter.toLower().split(' ', Qt::SkipEmptyParts);
  if (words.isEmpty() && !m_attentionOnly) return m_all;
  QVariantList result;
  for (const auto &entry : m_all) {
    const auto o = entry.toMap();
    if (m_attentionOnly && !needsAttention(o)) continue;
    if (words.isEmpty()) { result.append(entry); continue; }
    const QString text = QStringList{o.value("title").toString(), o.value("body").toString(), o.value("kind").toString(), o.value("review").toString(), o.value("files").toStringList().join(' ')}.join(' ').toLower();
    bool all = true; for (const auto &word : words) if (!text.contains(word)) { all = false; break; }
    if (all) result.append(entry);
  }
  return result;
}
bool MemoryModel::removeMissingRows(const QVariantList &entries) {
  // Only remove incrementally when the surviving rows keep their order.
  qsizetype next = 0;
  for (const auto &entry : m_entries) {
    if (next == entries.size()) break;
    if (identity(entry) == identity(entries.at(next))) ++next;
  }
  if (next != entries.size()) return false;

  for (qsizetype row = m_entries.size() - 1; row >= 0;) {
    if (next > 0 && identity(m_entries.at(row)) == identity(entries.at(next - 1))) {
      --next;
      --row;
      continue;
    }
    const qsizetype end = row;
    do { --row; } while (row >= 0 && (next == 0 || identity(m_entries.at(row)) != identity(entries.at(next - 1))));
    beginRemoveRows({}, int(row + 1), int(end));
    m_entries.remove(row + 1, end - row);
    endRemoveRows();
  }
  return true;
}
void MemoryModel::apply(const QVariantList &entries) {
  const qsizetype before = m_entries.size();
  // New and corrected memories arrive at the top (the list is newest first).
  qsizetype added = entries.size() - before;
  bool sameTail = added >= 0;
  for (qsizetype i = 0; sameTail && i < before; ++i) sameTail = identity(entries.at(i + added)) == identity(m_entries.at(i));
  if (added < 0 && removeMissingRows(entries)) {
    m_lastChange = "remove";
    emit countChanged();
  } else if (!sameTail) {
    beginResetModel(); m_entries = entries; endResetModel();
    m_lastChange = "reset"; if (before != m_entries.size()) emit countChanged();
    return;
  } else if (added > 0) {
    beginInsertRows({}, 0, int(added - 1));
    for (qsizetype i = added - 1; i >= 0; --i) m_entries.prepend(entries.at(i));
    endInsertRows();
    m_lastChange = "insert"; emit countChanged();
  } else m_lastChange = "none";
  for (qsizetype i = qMax<qsizetype>(0, added); i < entries.size(); ++i) {
    if (m_entries.at(i) == entries.at(i)) continue;
    m_entries[i] = entries.at(i);
    const auto row = index(int(i));
    emit dataChanged(row, row, {EntryRole});
    if (m_lastChange == "none") m_lastChange = "update";
  }
}
