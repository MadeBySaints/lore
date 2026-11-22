#include "backend.h"
#include <QCryptographicHash>

#include <QDateTime>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <stdexcept>

namespace {
const QString bundled = QStringLiteral(":/skill-catalog/aihero");
const QString receipt = QStringLiteral(".lore-skill.json");
constexpr qsizetype skillLimit = 128 * 1024;

[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

bool validName(const QString &name) {
  static const QRegularExpression pattern(QStringLiteral(R"(\A(?!con\z|prn\z|aux\z|nul\z|com[0-9]\z|lpt[0-9]\z)[a-z0-9]+(?:-[a-z0-9]+)*\z)"));
  return name.size() <= 64 && pattern.match(name).hasMatch();
}

QVariantMap skillMetadata(const QString &markdown) {
  static const QRegularExpression front(QStringLiteral(R"(\A\x{FEFF}?---\r?\n([\s\S]*?)\r?\n---[ \t]*\r?\n(\s*\S))"));
  static const QRegularExpression nameField(QStringLiteral(R"re(^name:[ \t]*["']?([a-z0-9-]+)["']?[ \t]*\r?$)re"), QRegularExpression::MultilineOption);
  static const QRegularExpression descriptionField(QStringLiteral(R"(^description:[ \t]*([^\r\n]+)((?:\r?\n[ \t]+[^\r\n]*)*))"), QRegularExpression::MultilineOption);
  const QString fields = front.match(markdown).captured(1);
  const QString name = nameField.match(fields).captured(1);
  const auto descriptionMatch = descriptionField.match(fields);
  QString description = descriptionMatch.captured(1).trimmed();
  if (description.startsWith('>') || description.startsWith('|')) description = descriptionMatch.captured(2).simplified();
  if (description.startsWith('"') && description.endsWith('"')) {
    const auto value = QJsonDocument::fromJson(("[" + description + "]").toUtf8()).array();
    if (!value.isEmpty()) description = value.first().toString();
  } else if (description.startsWith('\'') && description.endsWith('\'')) {
    description = description.mid(1, description.size() - 2).replace("''", "'");
  }
  if (!validName(name) || description.trimmed().isEmpty() || markdown.contains(QChar::Null) || markdown.toUtf8().size() > skillLimit)
    fail(Backend::tr("Use a SKILL.md with a lowercase, hyphenated name, a description, and instructions. The file must be at most 128 KB."));
  return {{"name", name}, {"description", description.left(1024)}};
}

void checkPath(const QString &path) {
  const QFileInfo info(path);
  if (info.isSymLink() || info.isJunction())
    fail(Backend::tr("Lore cannot manage a linked skill path."));
}

QString agentRoot(const QString &source, const QString &agent) {
  if (agent != "codex" && agent != "claude") fail(Backend::tr("Choose Codex or Claude Code."));
  const QString root = QFileInfo(source).canonicalFilePath();
  if (root.isEmpty()) fail(Backend::tr("The project folder is unavailable."));
  return root + (agent == "codex" ? "/.agents" : "/.claude");
}

QString library(const QString &project) {
  const QString data = project + "/.lore";
  checkPath(data);
  checkPath(data + "/skills");
  return data + "/skills";
}

QJsonObject customSkill(const QString &directory, const QString &name) {
  const QString file = directory + '/' + name + ".json";
  checkPath(file);
  return QJsonDocument::fromJson(Store::read(file).toUtf8()).object();
}

bool isBundled(const QString &name) { return QFile::exists(bundled + '/' + name + "/SKILL.md"); }

QJsonObject contents(const QString &folder) {
  checkPath(folder);
  QJsonObject files;
  QDirIterator entries(folder, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (entries.hasNext()) {
    const QString path = entries.next();
    const QString relative = QDir(folder).relativeFilePath(path);
    checkPath(path);
    if (relative == receipt) continue;
    if (entries.fileInfo().isDir()) { files[relative] = "/"; continue; }
    QFile file(path);
    QCryptographicHash digest(QCryptographicHash::Sha256);
    const QString hash = entries.fileInfo().isFile() && file.open(QIODevice::ReadOnly) && digest.addData(&file)
      ? QString::fromLatin1(digest.result().toHex()) : QString();
    if (hash.isEmpty()) fail(Backend::tr("Cannot read a file in the installed skill."));
    files[relative] = hash;
  }
  return files;
}

bool managed(const QString &folder) {
  const QFileInfo marker(folder + '/' + receipt);
  return marker.isFile() && !marker.isSymLink();
}
}

QVariantList Backend::skills(const QString &agent) const {
  QVariantList result;
  if (!m_store || personalWiki()) return result;
  try {
    const QString target = agentRoot(source(), agent) + "/skills";
    const QString custom = library(project());
    const auto add = [&](const QString &name, const QString &markdown, const QString &origin, bool included, const QString &created) {
      QVariantMap row = skillMetadata(markdown);
      if (row.value("name").toString() != name) return;
      row["origin"] = origin;
      row["bundled"] = included;
      row["created"] = created;
      const QString path = target + '/' + name;
      const QFileInfo installed(path);
      row["installed"] = installed.exists() || installed.isSymLink() || installed.isJunction();
      row["managed"] = managed(path);
      if (row.value("managed").toBool())
        row["enabledAt"] = QFileInfo(path + '/' + receipt).lastModified().toUTC().toString(Qt::ISODate);
      result.append(row);
    };
    for (const auto &name : QDir(bundled).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
      add(name, Store::read(bundled + '/' + name + "/SKILL.md"), "https://www.aihero.dev/skills-" + name, true, {});
    for (const auto &file : QDir(custom).entryList({"*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
      const auto name = QFileInfo(file).completeBaseName();
      if (!validName(name) || isBundled(name)) continue;
      try {
        const auto entry = customSkill(custom, name);
        add(name, entry.value("markdown").toString(), entry.value("origin").toString(), false,
          entry.value("created").toString(QFileInfo(custom + '/' + file).lastModified().toUTC().toString(Qt::ISODate)));
      } catch (const std::exception &) {
        // One damaged custom entry should not hide the rest of the catalog.
      }
    }
  } catch (const std::exception &) {}
  return result;
}

QString Backend::skillText(const QString &name) const {
  if (!m_store || personalWiki() || !validName(name)) return {};
  try {
    if (isBundled(name)) return Store::read(bundled + '/' + name + "/SKILL.md");
    return customSkill(library(project()), name).value("markdown").toString();
  } catch (const std::exception &) { return {}; }
}

bool Backend::saveSkill(const QString &markdown, const QString &origin) {
  if (!m_store || personalWiki()) return false;
  try {
    const auto name = skillMetadata(markdown).value("name").toString();
    const QString directory = library(project());
    if (!QDir().mkpath(directory)) fail(tr("Cannot create this project's skill library."));
    QLockFile lock(directory + "/.lock");
    if (!lock.tryLock(0)) fail(tr("Another Lore window is updating the skill library."));
    const QString file = directory + '/' + name + ".json";
    checkPath(file);
    if (isBundled(name) || QFileInfo::exists(file)) fail(tr("A skill named %1 is already in the catalog.").arg(name));
    const QUrl url(origin, QUrl::StrictMode);
    const QString link = url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty()
      ? url.toString(QUrl::FullyEncoded | QUrl::RemoveQuery | QUrl::RemoveFragment) : QString();
    Store::write(file, QJsonDocument(QJsonObject{{"markdown", markdown}, {"origin", link},
      {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}).toJson(QJsonDocument::Compact));
    emit skillsChanged();
    return true;
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); return false; }
}

void Backend::setSkillEnabled(const QString &name, const QString &agent, bool enabled) {
  if (!m_store || personalWiki() || !validName(name)) return;
  try {
    const QString markdown = skillText(name);
    if (skillMetadata(markdown).value("name").toString() != name) return;
    const QString root = agentRoot(source(), agent), directory = root + "/skills", target = directory + '/' + name;
    for (const auto &path : {root, directory, target}) checkPath(path);
    if (!QDir().mkpath(directory)) fail(tr("Cannot create the agent's project skills folder."));
    QLockFile lock(root + "/.lore-skills.lock");
    if (!lock.tryLock(0)) fail(tr("Another Lore window is updating these skills."));
    QTemporaryDir stage(root + "/.lore-skill-XXXXXX");
    if (!stage.isValid()) fail(tr("Cannot prepare the project skill."));
    const QString staged = stage.filePath(name);
    if (enabled) {
      if (QFileInfo::exists(target)) fail(tr("A skill named %1 already exists in this project's agent folder.").arg(name));
      if (isBundled(name)) {
        const QString folder = bundled + '/' + name;
        QDirIterator entries(folder, QDir::Files, QDirIterator::Subdirectories);
        while (entries.hasNext()) {
          const QString path = entries.next();
          QFile file(path);
          if (!file.open(QIODevice::ReadOnly)) fail(tr("Cannot read the bundled skill."));
          Store::write(staged + '/' + QDir(folder).relativeFilePath(path), file.readAll());
        }
        Store::write(staged + "/LICENSE", Store::read(bundled + "/LICENSE").toUtf8());
      } else {
        Store::write(staged + "/SKILL.md", markdown.toUtf8());
      }
      Store::write(staged + '/' + receipt, QJsonDocument(contents(staged)).toJson(QJsonDocument::Compact));
      if (!QDir().rename(staged, target)) fail(tr("Cannot enable this skill in the project folder."));
    } else {
      if (!managed(target)) fail(tr("This skill was installed outside Lore."));
      const auto expected = QJsonDocument::fromJson(Store::read(target + '/' + receipt).toUtf8()).object();
      if (!expected.contains("SKILL.md") || expected != contents(target))
        fail(tr("This skill has local edits. Move or restore its folder before disabling it in Lore."));
      if (!QDir().rename(target, staged)) fail(tr("Cannot disable this skill in the project folder."));
    }
    emit skillsChanged();
  } catch (const std::exception &e) { message(QString::fromUtf8(e.what()), true); }
}

void Backend::importSkill(const QString &input) {
  if (!m_store || personalWiki()) return;
  const QUrl origin(input.trimmed(), QUrl::StrictMode);
  QUrl url = origin;
  if (url.host() == "github.com") {
    QStringList parts = url.path(QUrl::FullyEncoded).split('/');
    if (parts.size() >= 6 && parts.at(3) == "blob") {
      parts.removeAt(3);
      url.setHost("raw.githubusercontent.com");
      url.setPath(parts.join('/'), QUrl::StrictMode);
      url.setQuery(QString());
    }
  }
  if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty()) {
    emit skillImported({}, {}, tr("Enter an HTTPS link to a SKILL.md file."));
    return;
  }
  url.setFragment(QString());
  const QString working = project();
  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
  QNetworkReply *reply = m_network.get(request);
  auto data = std::make_shared<QByteArray>();
  const auto read = [reply, data] { if (reply->isOpen()) data->append(reply->read(skillLimit + 1 - data->size())); };
  connect(reply, &QNetworkReply::readyRead, this, [reply, data, read] {
    read();
    if (data->size() > skillLimit) reply->abort();
  });
  QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
  connect(reply, &QNetworkReply::finished, this, [this, reply, working, origin, data, read] {
    reply->deleteLater();
    if (project() != working) return;
    read();
    try {
      if (data->size() > skillLimit) fail(tr("This skill exceeds 128 KB."));
      if (reply->error() != QNetworkReply::NoError) fail(tr("Could not download the skill. Check that the link opens a public SKILL.md file."));
      QStringDecoder decoder(QStringDecoder::Utf8);
      const QString markdown = decoder(*data);
      if (decoder.hasError()) fail(tr("The skill must be a UTF-8 Markdown file."));
      skillMetadata(markdown);
      emit skillImported(markdown, origin.toString(QUrl::FullyEncoded | QUrl::RemoveQuery | QUrl::RemoveFragment), {});
    } catch (const std::exception &e) { emit skillImported({}, {}, QString::fromUtf8(e.what())); }
  });
}

void Backend::buildSkill(const QString &goal, const QVariantList &answers, bool draftNow) {
  if (!m_store || personalWiki() || goal.trimmed().isEmpty()) return;
  if (busy() || m_verifyRunning || m_autoRun || !m_manualProvider.isEmpty()) {
    emit skillBuilt({}, {}, tr("Wait for the current model task to finish."));
    return;
  }
  if (!modelReady("ask", m_answerProvider)) {
    emit skillBuilt({}, {}, tr("Set up the model used by Ask & search before building a skill."));
    return;
  }
  QJsonArray interview;
  for (const auto &item : answers.mid(0, 5)) {
    const auto turn = item.toMap();
    interview.append(QJsonObject{{"question", turn.value("question").toString().left(500)}, {"answer", turn.value("answer").toString().left(3000)}});
  }
  m_skillProject = project();
  m_skillDraftNow = draftNow || interview.size() >= 5;
  m_skillNeedsQuestion = !m_skillDraftNow && interview.isEmpty();
  const QString task = m_skillDraftNow ? "Draft now using the information provided." :
    m_skillNeedsQuestion ? "Ask one concrete follow-up question before drafting." :
    "If a useful detail remains unclear, ask one follow-up question. Otherwise draft the skill.";
  const QJsonObject input{{"project", QFileInfo(source()).fileName()}, {"goal", goal.trimmed().left(4000)}, {"interview", interview}};
  const QString prompt = QStringLiteral(
    "Help the user author a reusable, instruction-only agent skill for their project. %1\n"
    "Ask about triggers, project conventions, inputs, outputs, boundaries or how success is verified. Skip answered questions. "
    "Do not run tools, execute the workflow, or write files. Return only JSON with exactly one key: "
    "{\"question\":\"one concise question\"} or {\"markdown\":\"complete SKILL.md\"}. "
    "The skill needs YAML front matter with a lowercase hyphenated name and a description that says when to use it, "
    "then Markdown instructions describing a repeatable workflow and how to check its result. Avoid invented project facts. "
    "The user will review the draft before saving.\n\nUser input:\n%2")
    .arg(task, QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)));
  try {
    m_operation = "skillBuild";
    startModel(prompt, m_answerProvider);
    emit statusChanged();
  } catch (const std::exception &e) { finishSkill({}, QString::fromUtf8(e.what())); }
}

void Backend::finishSkill(const QJsonObject &result, const QString &error) {
  m_operation.clear();
  QTimer::singleShot(0, this, &Backend::runDeferred);
  emit statusChanged();
  if (project() != m_skillProject) return;
  message(tr("Skill builder ready."));
  if (!error.isEmpty()) { emit skillBuilt({}, {}, error); return; }
  const QString question = result.value("question").toString().trimmed();
  const QString markdown = result.value("markdown").toString().trimmed();
  if (!m_skillDraftNow && !question.isEmpty() && question.size() <= 500 && markdown.isEmpty()) {
    emit skillBuilt(question, {}, {});
    return;
  }
  if (!m_skillNeedsQuestion && question.isEmpty() && !markdown.isEmpty()) {
    try { skillMetadata(markdown); emit skillBuilt({}, markdown, {}); }
    catch (const std::exception &e) { emit skillBuilt({}, {}, QString::fromUtf8(e.what())); }
    return;
  }
  emit skillBuilt({}, {}, tr("The model did not return the requested question or complete skill. Try again."));
}
