import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts

// The setup guide: one step per screen, in order. Steps up to Connect are
// required; the first build can be
// skipped and done later from their pages.
Rectangle {
  id: root
  objectName: "setupWizard"
  color: theme.background
  signal finished()
  signal openPage(string page)
  property int step: 0
  readonly property var steps: [
    { id: "welcome", name: qsTr("Welcome"), optional: false },
    { id: "project", name: qsTr("Project"), optional: false },
    { id: "files", name: qsTr("Lore's files"), optional: false },
    { id: "ai", name: qsTr("AI"), optional: false },
    { id: "connect", name: qsTr("Initialize"), optional: false },
    { id: "first", name: qsTr("First build"), optional: true },
    { id: "done", name: qsTr("Summary"), optional: false } ]
  readonly property int connectStep: 4
  readonly property bool chosen: backend.selectedProject.length > 0
  readonly property bool buildReady: { const revision = backend.modelRevision; const entries = backend.tools.entries; return backend.modelReady("build", backend.learningProvider) }
  function done(id) {
    switch (id) {
    case "welcome": return root.step > 0 || backend.connected
    case "project": return root.chosen
    case "files": return root.chosen
    case "ai": return root.buildReady
    case "connect": return backend.connected
    case "first": return (backend.stats.memories || 0) > 0
    default: return false
    }
  }
  function available(index) { return !backend.personalSetup || index !== 2 }
  function position(index) { return root.steps.filter((step, i) => i <= index && root.available(i)).length }
  function reachable(index) { return available(index) && (index <= root.connectStep || backend.connected) }
  function go(index) { if (index >= 0 && index < steps.length && reachable(index)) root.step = index }
  function advance(direction) {
    let next = root.step + direction
    while (next >= 0 && next < steps.length && !root.available(next)) next += direction
    root.go(next)
  }
  function connectAndAdvance() {
    if (backend.connectProject()) root.go(root.connectStep + 1)
  }
  // Where to resume: the first required step not done, else the first optional one.
  function resume() {
    if (!backend.connected) { root.step = root.chosen || backend.knownProjects.length > 0 ? 1 : 0; return }
    for (let i = root.connectStep + 1; i < steps.length - 1; ++i) if (root.available(i) && !done(steps[i].id)) { root.step = i; return }
    root.step = steps.length - 1
  }
  Component.onCompleted: { resume(); backend.scanProjects() }
  function shown(path) { return Qt.platform.os === "windows" ? String(path).replace(/\//g, "\\") : path }

  RowLayout {
    anchors.fill: parent; spacing: 0
    // The step rail.
    Rectangle { Layout.preferredWidth: 236; Layout.fillHeight: true; color: theme.panel
      ColumnLayout { anchors.fill: parent; anchors.margins: 18; spacing: 4
        RowLayout { spacing: 10; Layout.bottomMargin: 6
          Rectangle { width: 30; height: 34; color: theme.accent; radius: theme.radius
            Label { anchors.centerIn: parent; text: "L"; color: theme.accentForeground; font.bold: true; font.pixelSize: 25 } }
          ColumnLayout { spacing: 0
            Label { text: "lore"; color: theme.ink; font.pixelSize: 26 }
            Label { text: qsTr("Setup guide"); color: theme.muted; font.pixelSize: 11 } } }
        Caption { text: qsTr("STEPS"); Layout.topMargin: 8; Layout.bottomMargin: 4 }
        ColumnLayout { objectName: "setupSteps"; Layout.fillWidth: true; spacing: 2
          Repeater { model: root.steps
            delegate: Rectangle { required property var modelData; required property int index
              objectName: "setupStep_" + modelData.id
              readonly property bool isDone: root.done(modelData.id)
              readonly property bool here: root.step === index
              visible: root.available(index)
              Layout.fillWidth: true; implicitHeight: 34; radius: theme.radius
              color: here ? theme.selected : railMouse.containsMouse && root.reachable(index) ? theme.hover : "transparent"
              opacity: root.reachable(index) ? 1 : 0.45
              RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8; spacing: 10
                Rectangle { width: 22; height: 22; radius: 11; color: isDone ? theme.accent : "transparent"; border.color: isDone ? theme.accent : here ? theme.ink : theme.edge
                  Label { anchors.centerIn: parent; visible: !isDone; text: root.position(index); font.pixelSize: 10; color: here ? theme.ink : theme.muted }
                  IconImage { anchors.centerIn: parent; visible: isDone; source: "qrc:/icons/check.svg"; sourceSize: Qt.size(12, 12); color: theme.accentForeground } }
                Label { text: modelData.name; color: here ? theme.ink : theme.soft; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { visible: modelData.optional; text: qsTr("optional"); color: theme.muted; font.pixelSize: 10 } }
              MouseArea { id: railMouse; anchors.fill: parent; hoverEnabled: true; enabled: root.reachable(index); cursorShape: Qt.PointingHandCursor; onClicked: root.go(index) }
            } } }
        Item { Layout.fillHeight: true }
        Body { font.pixelSize: 11; text: backend.connected ? qsTr("Connected. Everything after Connect can be changed later on its own page.") : qsTr("Nothing in your project changes until you choose Connect.") }
        Button { objectName: "closeSetup"; visible: backend.connected; flat: true; text: qsTr("Close the guide"); onClicked: root.finished() }
      }
    }
    Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.edge }
    // The step itself.
    ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
      ColumnLayout { Layout.fillWidth: true; Layout.margins: 32; Layout.bottomMargin: 12; spacing: 6
        Caption { text: qsTr("STEP %1 OF %2").arg(root.position(root.step)).arg(root.position(root.steps.length - 1)) + (root.steps[root.step].optional ? "  ·  " + qsTr("OPTIONAL") : "") }
        Label { objectName: "setupHeading"; color: theme.ink; font.pixelSize: 28; wrapMode: Text.Wrap; Layout.fillWidth: true
          text: backend.personalSetup && root.step === 1 ? qsTr("Where should Lore keep your personal wiki?")
            : backend.personalSetup && root.step === root.connectStep ? qsTr("Connect live capture.")
            : [qsTr("Give your project a memory."), qsTr("Which project should Lore remember?"), qsTr("Where should Lore keep its files?"), qsTr("Choose an AI for wiki builds."), qsTr("Initialize the project."), qsTr("Build the wiki for the first time."), qsTr("You're set up.")][root.step] }
      }
      StackLayout { id: pages; currentIndex: root.step; Layout.fillWidth: true; Layout.fillHeight: true; Layout.leftMargin: 32; Layout.rightMargin: 32

        // 1 · Welcome: what Lore does, as the loop it runs.
        ScrollView { id: welcomeView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: welcomeView.availableWidth; spacing: 18
            Body { font.pixelSize: 15; color: theme.soft; text: qsTr("Lore keeps project conversations and indexed source files close at hand. Build a cited wiki, document functions and dependencies, and retrieve the evidence you need for the current coding task.") }
            Caption { text: qsTr("HOW THE LOOP WORKS") }
            Flow { objectName: "setupLoop"; Layout.fillWidth: true; spacing: 8
              Repeater { model: [
                  { t: qsTr("You code"), d: qsTr("with Claude Code or Codex, as usual") },
                  { t: qsTr("Capture"), d: qsTr("saved project conversations or pasted chats") },
                  { t: qsTr("Build knowledge"), d: qsTr("a cited wiki and a separate function index") },
                  { t: qsTr("Recall"), d: qsTr("a short handoff and relevant project evidence") } ]
                delegate: Row { required property var modelData; required property int index; spacing: 8
                  Rectangle { width: 168; height: 76; radius: theme.radius; color: theme.card; border.color: index === 0 ? theme.accent : theme.edge
                    Column { anchors.fill: parent; anchors.margins: 10; spacing: 4
                      Label { text: modelData.t; color: theme.ink; font.pixelSize: 13; font.bold: true; width: parent.width; elide: Text.ElideRight }
                      Label { text: modelData.d; color: theme.muted; font.pixelSize: 11; width: parent.width; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight } } }
                  Label { visible: index < 3; text: "→"; color: theme.accent; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter } } } }
            GridLayout { columns: width > 700 ? 2 : 1; Layout.fillWidth: true; columnSpacing: 12; rowSpacing: 12
              Card { caption: qsTr("STAYS ON THIS COMPUTER"); Layout.alignment: Qt.AlignTop
                Body { font.pixelSize: 12; text: qsTr("Conversations, the wiki, and indexes are saved locally. The model you choose receives the excerpts needed for its task.") } }
              Card { caption: qsTr("USES YOUR AI"); Layout.alignment: Qt.AlignTop
                Body { font.pixelSize: 12; text: qsTr("Choose Claude Code, Codex, or a local Ollama model for wiki builds, questions, and source analysis.") } }
            }
          }
        }

        // 2 · Project
        ScrollView { id: projectView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: projectView.availableWidth; spacing: 14
            Body { text: backend.personalSetup
              ? qsTr("Choose one working folder for your wiki. User-level hooks capture new saved conversations from any folder.")
              : qsTr("Choose the folder that holds your project's code or documents: the folder you open Claude Code or Codex in.") }
            Card { highlighted: !root.chosen; spacing: 10
              RowLayout { Layout.fillWidth: true; spacing: 10
                ColumnLayout { Layout.fillWidth: true; spacing: 2
                  Label { text: root.chosen ? (backend.folderFacts(backend.selectedProject).name || "") : qsTr("No folder chosen yet"); color: theme.ink; font.pixelSize: 17; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                  Label { visible: root.chosen; text: root.shown(backend.selectedProject); color: theme.muted; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.fillWidth: true } }
                PrimaryButton { objectName: "chooseProjectButton"; text: backend.selecting ? qsTr("Choosing…") : root.chosen ? qsTr("Change folder…") : qsTr("Choose folder…"); enabled: !backend.selecting && !backend.showBusy && !backend.showCapturing && !backend.connected; onClicked: backend.chooseProject() }
              }
              Flow { visible: root.chosen && !backend.personalSetup; Layout.fillWidth: true; spacing: 8
                readonly property var facts: root.chosen ? backend.folderFacts(backend.selectedProject) : ({})
                Repeater { model: [
                    { on: !!parent.facts.git, t: qsTr("Git repository: ignore rules are respected when indexing") },
                    { on: !!parent.facts.usedWithAgents, t: qsTr("Lore found saved agent activity for this folder") },
                    { on: !!parent.facts.agents || !!parent.facts.claude, t: qsTr("Has AGENTS.md or CLAUDE.md: Lore adds a short block and keeps your text") },
                    { on: !!parent.facts.memory, t: qsTr("Already has a Lore memory inside: it is reused") } ]
                  delegate: Rectangle { required property var modelData; visible: modelData.on; implicitWidth: factText.implicitWidth + 20; implicitHeight: 26; radius: 13; color: theme.inset; border.color: theme.edge
                    Label { id: factText; anchors.centerIn: parent; text: "✓ " + modelData.t; color: theme.soft; font.pixelSize: 11 } } } }
              Button { id: manualToggle; text: qsTr("Enter a folder path instead"); flat: true; checkable: true; font.pixelSize: 11; visible: !backend.connected }
              RowLayout { visible: manualToggle.checked; Layout.fillWidth: true
                TextField { id: manualPath; Layout.fillWidth: true; placeholderText: backend.personalSetup ? qsTr("Working folder path") : qsTr("Project folder path"); selectByMouse: true; onAccepted: backend.selectProject(text) }
                Button { text: qsTr("Use folder"); onClicked: backend.selectProject(manualPath.text) } }
              Body {
                visible: root.chosen && !backend.personalSetup && !backend.connected
                text: qsTr("Lore will store memory in %1 and add project-local Claude hooks and managed agent pointers. You can set up a model later.").arg(root.shown(backend.workingDirectory))
              }
            }
            Card { visible: !root.chosen && backend.knownProjects.filter(function(p) { return !p.missing }).length > 0; caption: qsTr("YOUR PROJECTS"); spacing: 4
              Body { font.pixelSize: 12; text: qsTr("Already set up with Lore. Open one to skip this guide.") }
              Repeater { model: backend.knownProjects.filter(function(p) { return !p.missing }).slice(0, 5)
                delegate: Button { required property var modelData; text: modelData.name + "  ·  " + root.shown(modelData.source || modelData.working); flat: true; onClicked: { if (backend.openKnownProject(modelData.working)) root.finished() } } } }
            Card { visible: !backend.personalSetup && !root.chosen && backend.suggestedProjects.length > 0; caption: qsTr("FOLDERS YOU USED WITH CLAUDE CODE OR CODEX"); spacing: 4
              Repeater { model: backend.suggestedProjects.slice(0, 6)
                delegate: Button { required property var modelData; text: (modelData.name || "") + "  ·  " + root.shown(modelData.path); flat: true; onClicked: backend.selectProject(modelData.path) } }
              Button { text: qsTr("Browse all projects on this computer"); flat: true; objectName: "browseProjectsButton"; onClicked: root.openPage("projects") } }
          }
        }

        // 3 · Lore's files
        ScrollView { id: filesView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: filesView.availableWidth; spacing: 14
            Body { text: qsTr("Lore keeps a database, the wiki, conversation exports, and BRAIN.md for each project. A separate working directory keeps them out of your project folder and its Git history; your project then only gets a few small pointer files.") }
            Card { objectName: "workingDirectoryPanel"; caption: qsTr("WORKING DIRECTORY"); spacing: 8
              RowLayout { Layout.fillWidth: true
                Label { objectName: "workingDirectoryLabel"; text: backend.workingDirectory === backend.selectedProject ? qsTr("Inside the project folder (.lore and BRAIN.md)") : root.shown(backend.workingDirectory); color: theme.ink; font.pixelSize: 15; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                Button { objectName: "chooseWorkingButton"; text: qsTr("Change…"); visible: !backend.connected; enabled: root.chosen && !backend.selecting && !backend.showBusy && !backend.showCapturing; onClicked: backend.chooseWorkingDirectory() } }
              Button { objectName: "keepInProjectButton"; visible: root.chosen && !backend.connected && backend.workingDirectory !== backend.selectedProject; flat: true; font.pixelSize: 11; text: qsTr("Keep Lore's files in the project folder instead"); onClicked: backend.setWorkingDirectory(backend.selectedProject) }
            }
            Card { caption: qsTr("WHAT GOES WHERE"); spacing: 6
              Label { color: theme.soft; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                text: backend.workingDirectory === backend.selectedProject
                  ? root.shown(backend.selectedProject || qsTr("<project>")) + "\n  .lore/        " + qsTr("database, wiki, sources (kept out of Git)") + "\n  BRAIN.md      " + qsTr("the entry point sessions read first") + "\n  AGENTS.md     " + qsTr("+ a short Lore block") + "\n  CLAUDE.md     " + qsTr("+ a short Lore block") + "\n  .claude/      " + qsTr("local hooks")
                  : root.shown(backend.workingDirectory || qsTr("<working directory>")) + "\n  .lore/        " + qsTr("database, wiki, sources") + "\n  BRAIN.md      " + qsTr("the entry point sessions read first") + "\n\n" + root.shown(backend.selectedProject || qsTr("<project>")) + "\n  AGENTS.md     " + qsTr("+ a short block pointing to BRAIN.md") + "\n  CLAUDE.md     " + qsTr("+ a short block pointing to BRAIN.md") + "\n  .claude/      " + qsTr("local hooks (not committed)") }
            }
            Body { font.pixelSize: 12; text: qsTr("Initializing also updates .gitignore and installs the local Claude Code hooks. You can change the working directory later in Settings › Folders.") }
            PrimaryButton {
              objectName: "initializeProjectButton"
              visible: root.chosen && !backend.connected
              enabled: !backend.selecting && !backend.showBusy && !backend.showCapturing
              text: qsTr("Initialize project")
              onClicked: root.connectAndAdvance()
            }
          }
        }

        // 4 · AI
        ScrollView { id: aiView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: aiView.availableWidth; spacing: 14
            Body { text: qsTr("Choose a model for wiki builds. You can set separate models for Ask, verification, function scans, and transcript analysis.") }
            GridLayout { columns: width > 700 ? 2 : 1; Layout.fillWidth: true; columnSpacing: 12; rowSpacing: 12
              Card { caption: qsTr("BUILD WIKI"); Layout.alignment: Qt.AlignTop
                Body { font.pixelSize: 12; text: qsTr("Turns saved conversations into wiki entries with citations. You can set the context window for local models in Settings › AI & models.") } }
              Card { caption: qsTr("QUOTA"); Layout.alignment: Qt.AlignTop
                Body { font.pixelSize: 12; text: qsTr("Claude Code and Codex use your own sign-in and quota. Automatic wiki learning is opt-in.") } }
            }
            Card { spacing: 12
              RowLayout { Layout.fillWidth: true
                Caption { text: (root.buildReady ? "✓  " : "") + qsTr("YOUR AI TOOLS"); color: root.buildReady ? theme.accent : theme.muted; Layout.fillWidth: true }
                Button { objectName: "checkToolsButton"; text: backend.tools.checking ? qsTr("Checking…") : qsTr("Check tools"); enabled: !backend.tools.checking; onClicked: backend.tools.check() } }
              Repeater { model: backend.tools.entries; delegate: ToolRow { required property var modelData; entry: modelData } }
            }
            Body { visible: !root.buildReady; color: theme.warning; font.pixelSize: 12; text: qsTr("Pick a ready tool for Build wiki above, sign in, or choose a local model. You can also continue and set this up later in Settings › AI & models.") }
          }
        }

        // 5 · Connect
        ScrollView { id: connectView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: connectView.availableWidth; spacing: 14
            Body { text: backend.personalSetup
              ? qsTr("The hooks copy saved conversation records to your wiki. Restart your agent to load them. In Codex, review and trust the hooks when prompted.")
              : backend.connected ? qsTr("Project initialized. Lore checks saved Claude Code and Codex transcripts from this project. Claude Code hooks can also capture while Lore is closed.") : qsTr("Initialize the project to create the files below and start checking for saved transcripts.") }
            Card { objectName: "connectionPreview"; caption: qsTr("FILES"); spacing: 10
              Repeater { model: root.step === root.connectStep ? backend.connectionPreview() : []
                delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 10
                  Rectangle { implicitWidth: actionLabel.implicitWidth + 14; implicitHeight: 20; radius: 10; color: modelData.action === "create" ? theme.selected : theme.inset; border.color: theme.edge; Layout.alignment: Qt.AlignTop
                    Label { id: actionLabel; anchors.centerIn: parent; text: modelData.action === "create" ? qsTr("new") : qsTr("update"); color: theme.soft; font.pixelSize: 10 } }
                  ColumnLayout { Layout.fillWidth: true; spacing: 2
                    Label { text: modelData.path; color: theme.ink; font.pixelSize: 12; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; elide: Text.ElideMiddle; Layout.fillWidth: true }
                    Label { text: modelData.what; color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true } } } }
            }
            RowLayout { spacing: 10
              Label { visible: backend.connected; text: qsTr("✓ Connected · %1 conversations captured so far").arg(backend.stats.sources || 0); color: theme.accent; font.pixelSize: 14 } }
            Body { font.pixelSize: 12; text: backend.personalSetup ? qsTr("You can pause live capture from Settings.") : qsTr("Disconnecting later removes only Lore's blocks and hooks.") }
          }
        }

        // 9 · First build
        ScrollView { id: firstView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: firstView.availableWidth; spacing: 14
            Body { text: qsTr("Lore has captured %1 conversations with %2 messages waiting to be learned. A first build writes the wiki's entries with references to the original messages; on a long history it can take a while and works in rounds.").arg(backend.stats.sources || 0).arg(backend.stats.pending || 0) }
            Body {
              visible: !(backend.stats.sources || 0)
              text: backend.stats.captureExclusions
                ? qsTr("Lore skips conversations you previously forgot. Start a fresh coding session in the project folder, or save a chat in Sources.")
                : qsTr("Start Claude Code or Codex in the selected project folder, then check capture. For browser ChatGPT and other chats without a supported transcript, paste the conversation in Sources.")
            }
            RowLayout {
              Button { text: qsTr("Check capture"); enabled: !backend.busy && !backend.capturing; onClicked: backend.sync() }
              Button { text: qsTr("Open Sources"); onClicked: root.openPage("sources") }
            }
            Card { spacing: 10
              RowLayout { spacing: 10
                PrimaryButton { objectName: "setupBuildButton"; text: backend.buildProgress.active ? qsTr("Building…") : qsTr("Build wiki now"); enabled: !backend.buildProgress.active && !backend.modelBusy && (backend.stats.pending || 0) > 0 && root.buildReady; onClicked: backend.learn(backend.learningProvider) }
                Label { text: (backend.stats.memories || 0) > 0 ? qsTr("✓ %1 wiki entries").arg(backend.stats.memories) : (backend.stats.pending || 0) === 0 ? qsTr("Nothing to learn yet: start a coding session first.") : ""; color: theme.accent; font.pixelSize: 13 } }
              ProgressBar { visible: !!backend.buildProgress.active; Layout.fillWidth: true; from: 0; to: Math.max(1, backend.buildProgress.total || 0); value: backend.buildProgress.done || 0 }
              Body { font.pixelSize: 12; text: qsTr("Uses %1. You can keep working; the progress also shows at the top of every page.").arg(backend.learningProvider === "claude" ? "Claude Code" : backend.learningProvider === "codex" ? "Codex" : qsTr("the local model")) }
            }
          }
        }

        // 10 · Summary
        ScrollView { id: doneView; clip: true; contentWidth: availableWidth
          ColumnLayout { width: doneView.availableWidth; spacing: 14
            Body { text: qsTr("Here is what is set up. Anything skipped can be done later from its page.") }
            Card { objectName: "setupSummary"; spacing: 8
              Repeater { model: [
                  { ok: backend.connected, t: backend.personalSetup ? qsTr("Live capture connected") : qsTr("Project connected"), later: qsTr("Connect it to start its memory.") },
                  { ok: root.buildReady, t: qsTr("AI for wiki builds ready"), later: qsTr("Settings › AI & models") },
                  { ok: (backend.stats.memories || 0) > 0, t: qsTr("Wiki built"), later: qsTr("Build wiki in the top bar once a conversation is captured") } ].filter(item => !backend.personalSetup || !item.projectOnly)
                delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 10
                  Rectangle { width: 18; height: 18; radius: 9; color: modelData.ok ? theme.accent : "transparent"; border.color: modelData.ok ? theme.accent : theme.edge
                    IconImage { anchors.centerIn: parent; visible: modelData.ok; source: "qrc:/icons/check.svg"; sourceSize: Qt.size(11, 11); color: theme.accentForeground } }
                  Label { text: modelData.t; color: theme.ink; font.pixelSize: 14; Layout.preferredWidth: 240 }
                  Label { visible: !modelData.ok; text: modelData.later; color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true } } }
            }
            Card { caption: qsTr("NEXT"); spacing: 6
              Body { text: backend.personalSetup ? qsTr("Start a new agent session to capture saved conversations. Build your wiki whenever messages are ready.")
                : qsTr("Check Sources for captured conversations. Use Index to browse source files, Function index to document dependencies, and Terminal to follow model output. BRAIN.md gives coding agents links to the wiki and indexes.") } }
          }
        }
      }
      // Back, Skip, Next. Initialize can use the choices made so far.
      Line {}
      RowLayout { Layout.fillWidth: true; Layout.margins: 20; Layout.leftMargin: 32; Layout.rightMargin: 32; spacing: 10
        Button { objectName: "wizardBack"; text: qsTr("Back"); flat: true; enabled: root.step > 0; onClicked: root.advance(-1) }
        Item { Layout.fillWidth: true }
        Button {
          objectName: "connectProjectButton"
          visible: root.chosen && !backend.connected && root.step > 0 && root.step < root.connectStep && root.step !== 2
          text: backend.personalSetup ? qsTr("Connect now") : qsTr("Connect capture")
          enabled: !backend.showBusy && !backend.showCapturing
          ToolTip.visible: hovered
          ToolTip.text: qsTr("Creates the setup files with the current folder choices and starts capture.")
          onClicked: { if (backend.connectProject()) root.finished() }
        }
        Button { objectName: "wizardSkip"; visible: root.steps[root.step].optional && !root.done(root.steps[root.step].id); text: qsTr("Skip"); flat: true; onClicked: root.advance(1) }
        PrimaryButton { objectName: "wizardNext"
          readonly property bool last: root.step === root.steps.length - 1
          text: last ? (backend.personalSetup ? qsTr("Open wiki") : qsTr("Open Project")) : root.step === 0 ? qsTr("Get started") : root.step === root.connectStep && !backend.connected ? (backend.personalSetup ? qsTr("Connect personal wiki") : qsTr("Initialize project")) : qsTr("Next")
          enabled: last ? backend.connected : root.step === 1 || root.step === 2 ? root.chosen : root.step === root.connectStep ? (backend.connected || (root.chosen && !backend.showBusy && !backend.showCapturing)) : true
          onClicked: {
            if (last) { root.finished(); return }
            if (root.step === root.connectStep && !backend.connected) { root.connectAndAdvance(); return }
            root.advance(1)
          } }
      }
    }
  }
}
