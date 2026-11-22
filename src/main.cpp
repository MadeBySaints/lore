#include "backend.h"
#include "mcp.h"
#include "theme.h"
#include "translator.h"
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QStandardPaths>
#include <QIcon>
#include <cstdio>
#include <memory>
#ifdef Q_OS_WIN
#include <shobjidl.h>
#endif

static void output(const QJsonValue &v) {
  QTextStream out(stdout);
  out << (v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject())).toJson(QJsonDocument::Compact) << '\n'; out.flush();
}

static bool databaseBusy(const std::exception &error) {
  const auto message = QString::fromUtf8(error.what());
  return message.startsWith("database is locked") || message.startsWith("database table is locked")
    || message.startsWith("database is busy");
}

int main(int argc, char **argv) {
  const bool profile = qEnvironmentVariableIntValue("LORE_PROFILE") == 1;
  QElapsedTimer processTimer;
  processTimer.start();
  QJsonObject timings;
  qint64 lastMark = 0;
  const auto mark = [&](const QString &stage) {
    if (!profile) return;
    const qint64 now = processTimer.nsecsElapsed();
    timings[stage] = (now - lastMark) / 1.0e6;
    lastMark = now;
  };
  const auto report = [&] {
    if (profile) QTextStream(stderr) << "lore-profile " << QJsonDocument(timings).toJson(QJsonDocument::Compact) << '\n';
  };
  const QString command = argc > 1 && !QString::fromLocal8Bit(argv[1]).startsWith('-') ? QString::fromLocal8Bit(argv[1]) : "open";
  bool informational = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    informational |= arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v";
  }
  const bool desktop = command == "open" && !informational;
  std::unique_ptr<QCoreApplication> application;
  if (desktop) application = std::make_unique<QGuiApplication>(argc, argv);
  else application = std::make_unique<QCoreApplication>(argc, argv);
  auto &app = *application;
  app.setApplicationName("lore"); app.setApplicationVersion("0.8.1");
  if (desktop) {
    QGuiApplication::setDesktopFileName("lore");
    QGuiApplication::setWindowIcon(QIcon(":/lore.svg"));
  }
#ifdef Q_OS_WIN
  if (desktop) SetCurrentProcessExplicitAppUserModelID(L"MadeBySaints.Lore");
#endif
  mark("startup_ms");
  QCommandLineParser parser;
  parser.setApplicationDescription("Local project memory for Claude Code and Codex.\nCommands: open, init, connect, migrate, sync, watch, import, search, brief, handoff, mcp, doctor, backup, restore, index, index-functions, review, deliveries, status, context, learn, skipped-learning, retry-learning, semantic, models, hook");
  parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("command", "Command to run", "[command]");
  parser.addOption(QCommandLineOption("transcript-text", "Import a plain-text conversation (import)."));
  parser.addOption(QCommandLineOption("chatgpt", "Read conversations.json from an extracted ChatGPT export (import)."));
  parser.addOption(QCommandLineOption("semantic", "Use the enabled local semantic index (brief)."));
  parser.addOption(QCommandLineOption("session", "Select a conversation for handoff or retry-learning.", "id"));
  parser.addOption(QCommandLineOption("python", "Absolute path to the interpreter for index-functions.", "path"));
  parser.addOptions({{{"p","project"},"Lore working directory (the project folder unless --source is used)","path",QDir::currentPath()}, {"source","Project source folder when Lore keeps its files elsewhere (init)","path"}, {"to","Destination working directory (migrate); defaults to Documents/Lore-<project>","path"}, {"provider","codex, claude, or local","name","codex"}, {"model","Override the selected provider's model","name"}, {"effort","Override Claude Code or Codex reasoning effort","level"}, {"file","Conversation file path","path"}, {"query","Search keywords","text"}, {"json","Output JSON"}, {"page","Initial GUI page for previews","number","0"}, {"action","Review action","name"}, {"id","Memory ID","number"}, {"text","Correction text","text"}, {"dry-run","Print the prompt without a model call"}, {"codex-root","Codex session directory","path"}, {"claude-root","Claude projects directory","path"}, {"screenshot","Save the GUI offscreen after rendering","path"}});
  parser.process(app);
  const auto configureModel = [&parser](Backend &backend) {
    const QString provider = parser.value("provider");
    if (parser.isSet("model")) {
      const QString model = parser.value("model").trimmed();
      if (model.isEmpty() || (provider != "local" && !Toolbox::validAgentModel(model)))
        throw std::runtime_error("Enter a valid model name using --model.");
      if (provider == "local") backend.tools()->setLocalModel(model);
      else backend.tools()->setAgentModel(provider, model);
    }
    if (parser.isSet("effort")) {
      const QString effort = parser.value("effort");
      if (provider == "local" || !backend.tools()->effortLevels(provider).contains(effort))
        throw std::runtime_error("Choose an effort level supported by the specified Claude Code or Codex model.");
      backend.tools()->setAgentEffort(provider, effort);
    }
  };
  QString hookEvent;
  const auto outputHook = [](const QString &event, const QString &context) {
    output(QJsonObject{{"hookSpecificOutput", QJsonObject{{"hookEventName", event}, {"additionalContext", context}}}});
  };
  const QString busyContext = "Lore's project memory is busy. Read BRAIN.md for project pointers and check the current source.";
  try {
    if (command == "mcp") return runMcp(parser.value("project"), app.applicationVersion());
    if (command == "restore") {
      if (!parser.isSet("file") || !parser.isSet("project"))
        throw std::runtime_error("Choose a backup with --file and a new archive location with --project.");
      Store::restoreBackup(parser.value("file"), parser.value("project"));
      output(QJsonObject{{"archive", QFileInfo(parser.value("project")).absoluteFilePath()}, {"restored", true}});
      return 0;
    }
    if (command == "open") {
      QQuickStyle::setStyle("Material"); Translator translator(qEnvironmentVariable("LORE_LANGUAGE_FILE")); app.installTranslator(&translator);
      Theme theme(qEnvironmentVariable("LORE_THEME_DIRECTORY"));
      const QString preferences = qEnvironmentVariable("LORE_PREFERENCES_FILE", QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/preferences.json");
      Backend backend(nullptr, nullptr, preferences);
      if (parser.isSet("project")) backend.selectProject(parser.value("project"));
      if (!qEnvironmentVariableIsSet("LORE_SKIP_TOOL_CHECKS")) QTimer::singleShot(0, backend.tools(), &Toolbox::check);
      QQmlApplicationEngine engine;
      QObject::connect(&translator,&Translator::changed,&engine,&QQmlEngine::retranslate);
      engine.rootContext()->setContextProperty("backend",&backend); engine.rootContext()->setContextProperty("theme",&theme); engine.rootContext()->setContextProperty("translator",&translator);
      engine.rootContext()->setContextProperty("initialProject",parser.value("project"));
      engine.load(QUrl("qrc:/Main.qml")); if (engine.rootObjects().isEmpty()) return 1;
      if(parser.isSet("page")) engine.rootObjects().first()->setProperty("page",parser.value("page"));  // An id such as wiki, or an older page number.
      if (parser.isSet("screenshot")) QTimer::singleShot(1200, &app, [&] { auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); app.exit(window && window->grabWindow().save(parser.value("screenshot")) ? 0 : 1); });
      return app.exec();
    }
    if (command == "migrate") {
      // Copy, verify, reconnect, then remove the old copy. Each Store closes
      // before the next step so the old database can be deleted.
      QString source, previous, target;
      { Store from(parser.value("project")); source = from.source(); previous = from.root();
        target = from.migrationTarget(parser.isSet("to") ? parser.value("to") : Store::defaultWorkingDirectory(source));
        if (target == previous) { output(QJsonObject{{"migrated",false},{"project",previous},{"source",source}}); return 0; }
        from.copyConnectedTo(target, QCoreApplication::applicationFilePath()); }
      const bool clean = Store::removeArchive(previous, source);
      output(QJsonObject{{"migrated",true},{"project",target},{"source",source},{"previous",previous},{"oldCopyRemoved",clean}}); return clean ? 0 : 2;
    }
    if (command == "models") {
      // Every local model server Lore finds and its models (no model is asked anything).
      Toolbox tools; QEventLoop loop; QTimer guard; guard.setSingleShot(true);
      QObject::connect(&tools, &Toolbox::changed, &loop, [&] { if (!tools.checking()) loop.quit(); });
      QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
      guard.start(20000); QTimer::singleShot(0, &tools, &Toolbox::check); loop.exec();
      for (const auto &e : tools.entries()) if (e.toMap().value("id").toString() == "local") { output(QJsonObject::fromVariantMap(e.toMap())); return 0; }
      return 1;
    }
    if (command == "init" && parser.isSet("source")) QDir().mkpath(parser.value("project"));
    if (command == "personal-hook") {
      const QString provider = parser.value("provider");
      const QString project = parser.isSet("project") ? parser.value("project") : Store::personalHookTarget();
      if (project.isEmpty() || !QFileInfo::exists(project + "/.lore/brain.sqlite")) return 0;
      QFile input;
      if (!input.open(stdin,QIODevice::ReadOnly)) throw std::runtime_error("Cannot read hook input");
      const auto doc = QJsonDocument::fromJson(input.readAll());
      if (!doc.isObject()) throw std::runtime_error("Invalid hook input");
      Store store(project);
      store.capturePersonalHook(doc.object(), provider);
      return 0;
    }
    QJsonObject hook;
    if (command == "hook") {
      QFile input;
      if (!input.open(stdin,QIODevice::ReadOnly)) throw std::runtime_error("Cannot read hook input");
      const auto doc = QJsonDocument::fromJson(input.readAll());
      if (!doc.isObject()) throw std::runtime_error("Invalid hook input");
      hook = doc.object();
      hookEvent = hook.value("hook_event_name").toString();
    }
    const bool prompt = hookEvent == "UserPromptSubmit";
    Store store(parser.value("project"), command == "init", false, prompt ? 100 : 5000);
    mark("store_open_ms");
    if (store.personalWiki() && !QStringList{"init", "connect", "status", "context", "search", "review", "sync", "watch", "learn",
        "skipped-learning", "retry-learning"}.contains(command))
      throw std::runtime_error("This command is available in project mode.");
    if (command == "init") { if (parser.isSet("source")) store.setSource(parser.value("source")); store.render(); output(store.stats()); }
    else if (command == "connect") { store.connectAgents(QCoreApplication::applicationFilePath()); output(QJsonObject{{"connected",true},{"project",store.root()}}); }
    else if (command == "status") output(store.stats());
    else if (command == "doctor") {
      const auto stats = store.stats();
      const QDir source(store.source());
      bool claudeHook = false, hookExecutable = false;
      QFile settings(source.filePath(".claude/settings.local.json"));
      if (settings.open(QIODevice::ReadOnly)) {
        const auto hooks = QJsonDocument::fromJson(settings.readAll()).object().value("hooks").toObject();
        for (const auto &handler : hooks.value("UserPromptSubmit").toArray())
          for (const auto &callback : handler.toObject().value("hooks").toArray()) {
            const auto entry = callback.toObject();
            const auto args = entry.value("args").toArray();
            if (entry.value("statusMessage").toString() != "Lore project memory" || args.size() < 3
                || QDir::fromNativeSeparators(args.at(2).toString()) != store.root()) continue;
            claudeHook = true;
            hookExecutable = QFileInfo(entry.value("command").toString()).isExecutable();
          }
      }
      const auto pointer = [&](const QString &name) {
        QFile file(source.filePath(name));
        return file.open(QIODevice::ReadOnly) && file.readAll().contains("<!-- lore:start -->");
      };
      output(QJsonObject{{"archive", store.root()}, {"source", store.source()},
        {"brainAvailable", QFileInfo(QDir(store.root()).filePath("BRAIN.md")).isReadable()},
        {"claudeHook", claudeHook}, {"claudeHookExecutable", hookExecutable}, {"codexPointer", pointer("AGENTS.md")},
        {"claudePointer", pointer("CLAUDE.md")}, {"lastCapture", stats.value("lastCapture")},
        {"onPath", !QStandardPaths::findExecutable("lore").isEmpty()}});
    }
    else if (command == "backup") {
      if (!parser.isSet("to")) throw std::runtime_error("Choose a destination file using --to.");
      store.backupTo(parser.value("to"));
      output(QJsonObject{{"backup", QFileInfo(parser.value("to")).absoluteFilePath()}, {"verified", true}});
    }
    else if (command == "handoff") {
      const auto handoff = store.handoff(parser.value("session"));
      if (parser.isSet("json")) output(handoff);
      else QTextStream(stdout) << handoff.value("text").toString() << '\n';
    }
    else if (command == "context") { if(parser.isSet("json")) output(QJsonObject{{"brain",store.context()}}); else QTextStream(stdout) << store.context(); }
    else if(command=="index") output(store.indexProject());
    else if (command == "index-functions") output(store.indexPythonFunctions(parser.value("python")));
    else if(command=="brief") {
      const auto brief = store.briefing(parser.value("query"), 8, parser.isSet("semantic"));
      mark("recall_ms");
      output(brief);
      report();
    }
    else if (command == "semantic" && parser.value("action") == "index") output(store.buildSemanticIndex());
    else if(command=="deliveries") output(store.deliveries());
    else if (command == "skipped-learning") output(store.skippedLearning());
    else if (command == "retry-learning") output(QJsonObject{{"queued",store.retrySkippedLearning(parser.value("session"))}});
    else if(command=="review") { store.reviewMemory(parser.value("id").toInt(),parser.value("action"),parser.value("text")); output(QJsonObject{{"ok",true}}); }
    else if (command == "search") output(store.search(parser.value("query")));
    else if (command == "sync" || command == "watch") {
      const auto sync = [&] { const auto result = store.sync(parser.value("codex-root"),parser.value("claude-root")); output(result); return result.value("warnings").toArray().isEmpty(); };
      bool ok = sync(); if (command == "sync") return ok ? 0 : 1;
      QTimer timer; QObject::connect(&timer,&QTimer::timeout,&app,[&] { try { sync(); } catch(const std::exception &e) { QTextStream(stderr) << e.what() << '\n'; } }); timer.start(5000); return app.exec();
    }
    else if (command == "import") {
      int count = 0;
      if (parser.isSet("chatgpt")) {
        output(store.importChatGpt(parser.value("file"), parser.value("session"), parser.isSet("dry-run")));
        return 0;
      } else if (parser.isSet("transcript-text")) {
        QFile file(parser.value("file"));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 2000000)
          throw std::runtime_error("Provide a readable UTF-8 conversation file under 2 MB.");
        count = store.importConversation(QString::fromUtf8(file.readAll()));
      } else {
        count = store.ingest(parser.value("file"), parser.value("provider"));
        store.render();
      }
      output(QJsonObject{{"added",count}});
    }
    else if (command == "learn") {
      if(parser.isSet("dry-run")) { output(store.learningBatch(parser.value("provider")=="local")); return 0; }
      Backend backend; if (!backend.openProject(store.root())) throw std::runtime_error(backend.status().toStdString());
      configureModel(backend);
      QEventLoop loop; bool ok = false;
      QObject::connect(&backend,&Backend::learningFinished,&loop,[&](bool success) { ok = success; loop.quit(); });
      QTimer::singleShot(0,&backend,[&] { backend.learn(parser.value("provider")); }); loop.exec();
      if (!ok) throw std::runtime_error(backend.status().toStdString());
      output(QJsonObject{{"ok",true},{"message",backend.status()}});
    }
    else if (command == "hook") {
      const QString event = hookEvent;
      bool captureBusy = false;
      if (prompt) store.setBusyTimeout(100);
      try { store.captureClaudeHook(hook); }
      catch (const std::exception &error) {
        if (!prompt || !databaseBusy(error)) throw;
        captureBusy = true;
      }
      mark("capture_ms");
      // The ending hook for /clear has a shared provider timeout. The
      // archive is committed; the new session or sync can refresh exports.
      const bool endingClear = event == "SessionEnd" && hook.value("reason").toString() == "clear";
      if (endingClear) store.saveClearCheckpoint(hook);
      if (!prompt && !endingClear && store.exportsStale()) store.render();
      if (!prompt) mark("exports_ms");
      if (event == "SessionStart" || prompt || event == "PostCompact") {
        const QString query = prompt ? hook.value("prompt").toString() : QString();
        QJsonObject brief;
        if (!captureBusy && ((prompt && processTimer.elapsed() < 650) || hook.value("source").toString() == "clear"))
          brief = store.recoverClearCheckpoint(hook);
        const bool recovered = !brief.value("evidence").toArray().isEmpty();
        if (event == "PostCompact") {
          brief = store.transcriptHandoff(hook.value("transcript_path").toString());
          if (brief.value("text").toString().isEmpty())
            brief = {{"text", store.context().left(2400)}, {"evidence", QJsonArray{}}};
        } else if (event == "SessionStart") {
          if (hook.value("source").toString() == "clear" && brief.isEmpty())
            brief = store.recentHandoff(hook.value("transcript_path").toString());
          const QString context = store.context().left(2400) + '\n' + brief.value("text").toString();
          brief["text"] = context;
          if (!brief.contains("evidence")) brief["evidence"] = QJsonArray();
        } else if (!recovered && (captureBusy || processTimer.elapsed() >= 650)) {
          brief = {{"text", busyContext},
                   {"evidence", QJsonArray()}};
          // Capture offsets are committed with records. A later hook or sync
          // can pick up the saved turns that this process could not commit.
        } else if (!recovered) {
          brief = store.briefing(query, 5, false, 1, 3600);
        }
        mark("recall_ms");
        if (prompt) store.setBusyTimeout(0);
        try { store.recordDelivery(event,hook.value("session_id").toString(),query,brief); }
        catch (const std::exception &error) {
          if (!prompt || !databaseBusy(error)) throw;
          QTextStream(stderr) << "Lore could not record the prompt handoff in the delivery audit because the database was busy.\n";
        }
        mark("audit_ms");
        outputHook(event, brief.value("text").toString());
      }
      report();
    }
    else throw std::runtime_error("Unknown command. Run lore --help.");
  } catch(const std::exception &e) {
    if (hookEvent == "UserPromptSubmit" && databaseBusy(e)) {
      outputHook(hookEvent, busyContext);
      report();
      return 0;
    }
    QTextStream(stderr) << e.what() << '\n';
    if(parser.isSet("json") && command != "mcp") output(QJsonObject{{"ok",false},{"error",QString::fromUtf8(e.what())}});
    return 1;
  }
  return 0;
}
