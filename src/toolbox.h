#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QMap>

struct ToolCommand {
  QString program;
  QStringList arguments;
  QString path;
  bool available() const { return !program.isEmpty(); }
};

class Toolbox : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
  Q_PROPERTY(bool checking READ checking NOTIFY changed)
public:
  explicit Toolbox(QObject *parent = nullptr);
  ~Toolbox() override;
  QVariantList entries() const;
  bool checking() const { return m_checking; }
  ToolCommand command(const QString &id) const;
  static ToolCommand resolve(const QString &id, const QString &path);
  QVariantMap overrides() const { return m_overrides; }
  void setOverrides(const QVariantMap &paths);
  void setExecutable(const QString &id, const QString &path);
  Q_INVOKABLE void check();
  Q_INVOKABLE void install(const QString &id);
  Q_INVOKABLE void signIn(const QString &id);
  Q_INVOKABLE void startSession(const QString &id, const QString &project);
  Q_INVOKABLE bool available(const QString &id) const { return command(id).available(); }
  Q_INVOKABLE bool ready(const QString &id) const { return available(id) && m_states.value(id) == "ready"; }
signals:
  void changed();
  void settingsChanged();
  void notice(const QString &text, bool error);
private:
  QVariantMap m_overrides;
  QMap<QString, QString> m_states;
  QProcess m_probe;
  QTimer m_timeout;
  bool m_checking = false;
  int m_index = 0;
  void next();
  void terminal(const QString &id, const QStringList &arguments, const QString &project = {});
};
