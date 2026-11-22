#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QHash>
#include <QMap>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QUrl>

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
  Q_PROPERTY(int localContextTokens READ localContextTokens WRITE setLocalContextTokens NOTIFY localContextTokensChanged)
public:
  explicit Toolbox(QObject *parent = nullptr);
  ~Toolbox() override;
  QVariantList entries() const;
  bool checking() const { return m_checking || m_localChecking; }
  ToolCommand command(const QString &id) const;
  static ToolCommand resolve(const QString &id, const QString &path);
  QVariantMap overrides() const { return m_overrides; }
  void setOverrides(const QVariantMap &paths);
  void setExecutable(const QString &id, const QString &path);
  Q_INVOKABLE void check();
  Q_INVOKABLE void install(const QString &id);
  Q_INVOKABLE void signIn(const QString &id);
  Q_INVOKABLE void startSession(const QString &id, const QString &project);
  // Opens the agent in a terminal in the project, already working on prompt.
  void startSessionWith(const QString &id, const QString &project, const QString &prompt, const QStringList &options = {});
  Q_INVOKABLE bool available(const QString &id) const { return id == "local" ? localReachable() : command(id).available(); }
  Q_INVOKABLE bool ready(const QString &id) const { return id == "local" ? m_localState == "ready" : available(id) && m_states.value(id) == "ready"; }
  // A local model: an Ollama server (or one with Ollama's API) on this
  // computer. Lore sends it nothing over the network: the address must be loopback.
  // The model and effort Lore asks Claude Code or Codex to use for its own
  // calls; empty keeps the tool's default. Saved with the tool settings.
  Q_INVOKABLE void setAgentModel(const QString &id, const QString &model);
  Q_INVOKABLE void setAgentEffort(const QString &id, const QString &effort);
  QString agentModel(const QString &id) const { return m_overrides.value(id + ".model").toString(); }
  QString agentEffort(const QString &id) const { return m_overrides.value(id + ".effort").toString(); }
  // Extra command-line arguments for those choices.
  QStringList agentArguments(const QString &id) const;
  static QStringList agentArguments(const QString &id, const QString &model, const QString &effort);
  static bool validAgentModel(const QString &model);
  Q_INVOKABLE QStringList agentModels(const QString &id) const;
  Q_INVOKABLE QStringList effortLevels(const QString &id, const QString &model = {}) const;
  bool localModelReady(const QString &model) const { return localReachable() && m_localModels.contains(model); }
  Q_INVOKABLE void setLocalUrl(const QString &url);
  Q_INVOKABLE void setLocalModel(const QString &model);
  // The address the user added (or LORE_LLM_URL); Lore also looks at
  // Ollama's own port (11434) unless LORE_LLM_DISCOVER is 0, and at
  // LORE_LLM_URLS (comma-separated). Only servers with Ollama's API count.
  QString localUrl() const;
  QString localModel() const;
  // Zero keeps automatic sizing and the default input budgets.
  int localContextTokens() const;
  int localAskContextTokens() const { const int tokens = localContextTokens(); return tokens ? qMax(4096, tokens) : 8192; }
  void setLocalContextTokens(int tokens);
  int localInputScale() const { return qMax(1, localContextTokens() / 16384); }
  // The Ollama server that has a model (empty when none has it). Ollama also
  // serves the Responses API that Codex uses for local card work.
  QString chatServer(const QString &model) const;
  // What Lore uses when the chosen local model is not running: claude,
  // codex, auto (whichever of them is ready), or none.
  QString localBackup() const { return m_overrides.value("local.backup", qEnvironmentVariable("LORE_LOCAL_BACKUP", "none")).toString(); }
  bool localChecking() const { return m_localChecking; }
  // Whether a look for local servers has finished; before that, calls go to
  // the configured address as they always did.
  bool localScanned() const { return m_localState != "unchecked" && !(m_localState == "checking" && m_servers.isEmpty()); }
  Q_INVOKABLE void setLocalBackup(const QString &backup);
  QString backupProvider() const;
  // Looks for local servers again, without showing a check in progress.
  Q_INVOKABLE void rescanLocal();
  static QUrl loopback(const QString &url);
signals:
  void changed();
  void settingsChanged();
  void localContextTokensChanged();
  void notice(const QString &text, bool error);
private:
  QVariantMap m_overrides;
  QMap<QString, QString> m_states;
  QProcess m_probe;
  QTimer m_timeout;
  bool m_checking = false, m_localChecking = false, m_quietScan = false;
  QString m_localState = "unchecked";
  QStringList m_localModels;
  struct LocalServer { QString url; QStringList models; };
  QList<LocalServer> m_servers;
  QStringList localCandidates() const;
  QTimer m_rescan;
  QNetworkAccessManager m_network;
  bool localReachable() const { return m_localState == "ready" || m_localState == "nomodel"; }
  void probeLocal();
  int m_index = 0;
  void next();
  void terminal(const QString &id, const QStringList &arguments, const QString &project = {});
};
