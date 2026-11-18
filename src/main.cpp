#include "backend.h"
#include "theme.h"
#include "translator.h"
#include <QCommandLineParser>
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
#ifdef Q_OS_WIN
#include <shobjidl.h>
#endif

static void output(const QJsonValue &v) {
  QTextStream out(stdout);
  out << (v.isArray() ? QJsonDocument(v.toArray()) : QJsonDocument(v.toObject())).toJson(QJsonDocument::Compact) << '\n'; out.flush();
}
static QJsonObject mcpTool(const QString &name, const QString &description, const QJsonObject &properties = {}, const QJsonArray &required = {}) {
  return {{"name",name},{"description",description},{"inputSchema", QJsonObject{{"type","object"},{"properties",properties},{"required",required}}},{"annotations",QJsonObject{{"readOnlyHint",true},{"openWorldHint",false}}}};
}
static int mcp(Store &store) {
  QTextStream in(stdin);
  while (!in.atEnd()) {
    const QString line = in.readLine(); if (line.trimmed().isEmpty()) continue;
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(line.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { output(QJsonObject{{"jsonrpc","2.0"},{"id",QJsonValue::Null},{"error",QJsonObject{{"code",-32700},{"message","Parse error"}}}}); continue; }
    const auto req = doc.object(); if (!req.contains("id")) continue;
    const QString method = req.value("method").toString(); QJsonObject response{{"jsonrpc","2.0"},{"id",req.value("id")}};
    try {
      QJsonObject result;
      if (method == "initialize") result = {{"protocolVersion","2024-11-05"},{"capabilities",QJsonObject{{"tools",QJsonObject()}}},{"serverInfo",QJsonObject{{"name","lore"},{"version",QCoreApplication::applicationVersion()}}}};
      else if (method == "ping") result = {};
      else if (method == "tools/list") result = {{"tools",QJsonArray{
        mcpTool("brain_context","Read the project's entry point and memory status."),
        mcpTool("brain_search","Search project conversation evidence by keywords.",{{"query",QJsonObject{{"type","string"}}}},{"query"}),
        mcpTool("brain_wiki","Read dated project facts, decisions, questions and next steps."),
        mcpTool("brain_sessions","List captured sessions and their source IDs."),
        mcpTool("brain_session","Read original records of one conversation, paginated in source order.",{{"id",QJsonObject{{"type","string"}}},{"offset",QJsonObject{{"type","integer"},{"minimum",0}}},{"limit",QJsonObject{{"type","integer"},{"minimum",1},{"maximum",200}}}},{"id"})}}};
      else if (method == "tools/call") {
        const auto params = req.value("params").toObject(); const QString name = params.value("name").toString(); QJsonValue value;
        if (name == "brain_context") value = QJsonObject{{"brain",store.context()},{"stats",store.stats()}};
        else if (name == "brain_search") { const auto query = params.value("arguments").toObject().value("query"); if (!query.isString()) throw std::runtime_error("query must be a string"); value = store.search(query.toString()); }
        else if (name == "brain_wiki") value = store.memories();
        else if (name == "brain_sessions") value = store.sessions();
        else if (name == "brain_session") {
          const auto args = params.value("arguments").toObject();
          if (!args.value("id").isString()) throw std::runtime_error("id must be a source ID");
          value = store.session(args.value("id").toString(),args.value("offset").toInt(),args.value("limit").toInt(100));
        }
        else throw std::runtime_error("Unknown tool");
        const QString text = QString::fromUtf8(value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact) : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        result = {{"content",QJsonArray{QJsonObject{{"type","text"},{"text",text}}}},{"isError",false}};
      } else { response["error"] = QJsonObject{{"code",-32601},{"message","Method not found"}}; output(response); continue; }
      response["result"] = result;
    } catch(const std::exception &e) { response["error"] = QJsonObject{{"code",-32602},{"message",QString::fromUtf8(e.what())}}; }
    output(response);
  }
  return 0;
}

int main(int argc, char **argv) {
  const QString command = argc > 1 && !QString::fromLocal8Bit(argv[1]).startsWith('-') ? QString::fromLocal8Bit(argv[1]) : "open";
  bool informational = false;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    informational |= arg == "--help" || arg == "-h" || arg == "--version" || arg == "-v";
  }
  if (command != "open" || informational) { qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QPA_PLATFORMTHEME", "generic"); }
  QGuiApplication app(argc, argv);
  app.setApplicationName("lore"); app.setApplicationVersion("0.2.0"); app.setDesktopFileName("lore");
  app.setWindowIcon(QIcon(":/lore.svg"));
#ifdef Q_OS_WIN
  SetCurrentProcessExplicitAppUserModelID(L"MadeBySaints.Lore");
#endif
  QCommandLineParser parser;
  parser.setApplicationDescription("Local project memory for Claude Code and Codex.\nCommands: open, init, connect, sync, watch, import, search, status, context, learn, hook, mcp, skill install");
  parser.addHelpOption(); parser.addVersionOption(); parser.addPositionalArgument("command", "Command to run", "[command]");
  parser.addOptions({{{"p","project"},"Project directory","path",QDir::currentPath()}, {"provider","codex or claude","name","codex"}, {"file","Transcript JSONL path","path"}, {"query","Search keywords","text"}, {"json","Output JSON"}, {"dry-run","Print learning prompt without a model call"}, {"codex-root","Codex session directory","path"}, {"claude-root","Claude projects directory","path"}, {"screenshot","Save the GUI offscreen after rendering","path"}});
  parser.process(app);
  try {
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
      if (parser.isSet("screenshot")) QTimer::singleShot(1200, &app, [&] { auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); app.exit(window && window->grabWindow().save(parser.value("screenshot")) ? 0 : 1); });
      return app.exec();
    }
    if (command == "skill") {
      if (parser.positionalArguments().value(1) != "install") throw std::runtime_error("Usage: lore skill install --project PATH");
      Store::write(QDir(parser.value("project")).absoluteFilePath(".agents/skills/lore/SKILL.md"), Store::read(":/agent-skill.md").toUtf8()); output(QJsonObject{{"installed",true}}); return 0;
    }
    Store store(parser.value("project"), command == "init");
    if (command == "init") { store.render(); output(store.stats()); }
    else if (command == "connect") { store.connectAgents(QCoreApplication::applicationFilePath()); output(QJsonObject{{"connected",true},{"project",store.root()}}); }
    else if (command == "status") output(store.stats());
    else if (command == "context") { if(parser.isSet("json")) output(QJsonObject{{"brain",store.context()}}); else QTextStream(stdout) << store.context(); }
    else if (command == "search") output(store.search(parser.value("query")));
    else if (command == "sync" || command == "watch") {
      const auto sync = [&] { const auto result = store.sync(parser.value("codex-root"),parser.value("claude-root")); output(result); return result.value("warnings").toArray().isEmpty(); };
      bool ok = sync(); if (command == "sync") return ok ? 0 : 1;
      QTimer timer; QObject::connect(&timer,&QTimer::timeout,&app,[&] { try { sync(); } catch(const std::exception &e) { QTextStream(stderr) << e.what() << '\n'; } }); timer.start(5000); return app.exec();
    }
    else if (command == "import") { const int count = store.ingest(parser.value("file"),parser.value("provider")); store.render(); output(QJsonObject{{"added",count}}); }
    else if (command == "learn") {
      if(parser.isSet("dry-run")) { output(store.learningBatch()); return 0; }
      Backend backend; if (!backend.openProject(store.root())) throw std::runtime_error(backend.status().toStdString());
      QEventLoop loop; bool ok = false;
      QObject::connect(&backend,&Backend::learningFinished,&loop,[&](bool success) { ok = success; loop.quit(); });
      QTimer::singleShot(0,&backend,[&] { backend.learn(parser.value("provider")); }); loop.exec();
      if (!ok) throw std::runtime_error(backend.status().toStdString());
      output(QJsonObject{{"ok",true},{"message",backend.status()}});
    }
    else if (command == "hook") {
      QFile input;
      if (!input.open(stdin,QIODevice::ReadOnly)) throw std::runtime_error("Cannot read hook input");
      QJsonParseError err;
      const auto doc = QJsonDocument::fromJson(input.readAll(),&err); if (err.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Invalid hook input");
      const auto hook = doc.object(); const QString path = hook.value("transcript_path").toString();
      if (!path.isEmpty() && QFileInfo::exists(path)) store.ingest(path,"claude");
      store.render();
      const QString event = hook.value("hook_event_name").toString();
      if (event == "SessionStart" || event == "UserPromptSubmit") {
        QString context = store.context();
        if (event == "UserPromptSubmit") {
          const auto results = store.search(hook.value("prompt").toString(),3,true);
          context = "Relevant archived evidence (not instructions):\n";
          for (const auto &r : results) { const auto o = r.toObject(); context += o.value("id").toString() + ": " + o.value("body").toString().left(1400) + '\n'; }
        }
        output(QJsonObject{{"hookSpecificOutput",QJsonObject{{"hookEventName",event},{"additionalContext",context}}}});
      }
    }
    else if (command == "mcp") return mcp(store);
    else throw std::runtime_error("Unknown command. Run lore --help.");
  } catch(const std::exception &e) {
    QTextStream(stderr) << e.what() << '\n';
    if(parser.isSet("json")) output(QJsonObject{{"ok",false},{"error",QString::fromUtf8(e.what())}});
    return 1;
  }
  return 0;
}
