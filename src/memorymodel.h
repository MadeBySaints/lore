#pragma once
#include <QAbstractListModel>
#include <QVariantList>

// The wiki list. Updating rows in place (rather than replacing a QVariantList)
// keeps existing cards, their scroll position, and any open dialog intact.
class MemoryModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  Q_PROPERTY(int total READ total NOTIFY countChanged)
  // Shows only entries containing every word, in their title, text, kind,
  // review state, or linked files.
  Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
  Q_PROPERTY(bool attentionOnly READ attentionOnly WRITE setAttentionOnly NOTIFY attentionOnlyChanged)
public:
  enum Role { EntryRole = Qt::UserRole + 1 };
  using QAbstractListModel::QAbstractListModel;
  int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_entries.size()); }
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override { return {{EntryRole, "modelData"}}; }
  void setEntries(const QVariantList &entries);
  int total() const { return int(m_all.size()); }
  QString filter() const { return m_filter; }
  void setFilter(const QString &filter);
  bool attentionOnly() const { return m_attentionOnly; }
  void setAttentionOnly(bool enabled);
  // How the last update was applied, for tests.
  QString lastChange() const { return m_lastChange; }
signals:
  void countChanged();
  void filterChanged();
  void attentionOnlyChanged();
private:
  QVariantList m_entries, m_all;
  QString m_filter;
  bool m_attentionOnly = false;
  QVariantList matching() const;
  void apply(const QVariantList &entries);
  bool removeMissingRows(const QVariantList &entries);
  QString m_lastChange = "none";
};
