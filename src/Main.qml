import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
  id: win
  width: 1280; height: 840
  minimumWidth: 960; minimumHeight: 650
  visible: true
  title: qsTr("Lore · Project memory")
  color: "#101211"
  Material.theme: Material.Dark
  Material.accent: theme.accent
  Material.roundedScale: theme.radius
  palette.link: theme.accent
  palette.linkVisited: theme.accent
  property int page: 0
  property string document: ""
  property string documentTitle: ""
  property bool helpVisible: false
  readonly property color ink: "#e7e9e4"
  readonly property color muted: "#8a938c"
  readonly property color edge: "#2b312c"
  readonly property bool ready: backend.project.length > 0
  readonly property string language: translator.language

  function showDocument(title, text) { documentTitle = title; document = text }
  function follow(link) {
    let path = link.toString().split("#")[0]
    if (path.startsWith(".lore/")) showDocument(path, backend.readPage(path))
    else if (path.startsWith("../sources/")) showDocument(qsTr("Source evidence"), backend.readPage(".lore/sources/" + path.substring(11)))
    else if (/^[0-9]+\.md$/.test(path)) showDocument(qsTr("Wiki page"), backend.readPage(".lore/wiki/" + path))
    else if (/^[a-f0-9]+\.md$/.test(path)) showDocument(qsTr("Conversation"), backend.readPage(".lore/sources/" + path))
  }
  Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
  Shortcut { sequence: "Q"; enabled: !projectPath.activeFocus && !searchField.activeFocus; context: Qt.ApplicationShortcut; onActivated: backend.busy ? quitDialog.open() : Qt.quit() }
  Shortcut { sequence: "Space"; enabled: !projectPath.activeFocus && !searchField.activeFocus && ready; context: Qt.ApplicationShortcut; onActivated: backend.sync() }
  Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: { win.page = 3; searchField.forceActiveFocus() } }
  Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { win.document = ""; win.helpVisible = false } }
  onClosing: function(close) { if (backend.busy) { close.accepted = false; quitDialog.open() } }

  component Caption: Label { color: win.muted; font.pixelSize: 11; font.letterSpacing: 1.4 }
  component Hairline: Rectangle { color: win.edge; Layout.fillWidth: true; implicitHeight: 1 }
  component StatCard: Rectangle {
    property string caption
    property string value
    Layout.fillWidth: true; implicitHeight: 104
    color: "#191d19"; border.color: win.edge; radius: theme.radius
    Column { anchors.fill: parent; anchors.margins: 18; spacing: 8
      Caption { text: caption }
      Label { text: value; color: win.ink; font.pixelSize: 28; font.family: "JetBrains Mono" }
    }
  }
  Item {
    anchors.fill: parent
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    RowLayout {
      anchors.fill: parent; spacing: 0
      Rectangle {
        Layout.preferredWidth: 228; Layout.fillHeight: true; color: "#151815"
        ColumnLayout {
          anchors.fill: parent; anchors.margins: 22; spacing: 12
          RowLayout {
            spacing: 10
            Rectangle { width: 30; height: 34; color: theme.accent; radius: theme.radius
              Label { anchors.centerIn: parent; text: "L"; color: theme.accentForeground; font.bold: true; font.pixelSize: 25 }
            }
            Label { text: "lore"; color: win.ink; font.pixelSize: 29; font.letterSpacing: -1 }
          }
          Label { text: qsTr("A memory for your work."); color: win.muted; font.pixelSize: 12; Layout.bottomMargin: 28 }
          Caption { text: qsTr("PROJECT") }
          Label { text: ready ? backend.stats.name : qsTr("No project connected"); color: win.ink; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
          Label { text: backend.project; color: win.muted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle; Layout.bottomMargin: 18 }
          Repeater {
            model: [qsTr("Entry point"), qsTr("Project wiki"), qsTr("Conversations"), qsTr("Search memory"), qsTr("Connections")]
            delegate: Rectangle {
              required property int index
              required property string modelData
              Layout.fillWidth: true; implicitHeight: 43
              color: win.page === index ? "#283026" : navMouse.containsMouse ? "#1f251f" : "transparent"
              radius: theme.radius
              Rectangle { width: 2; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; color: theme.accent; visible: win.page === index }
              Row { anchors.verticalCenter: parent.verticalCenter; x: 14; spacing: 12
                Label { text: (index + 1).toString().padStart(2, "0"); font.family: "JetBrains Mono"; font.pixelSize: 11; color: win.page === index ? theme.accent : win.muted }
                Label { text: modelData; font.pixelSize: 13; color: win.page === index ? win.ink : win.muted }
              }
              MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; onClicked: { win.page = index; win.document = "" } }
            }
          }
          Item { Layout.fillHeight: true }
          Hairline {}
          RowLayout {
            Rectangle { width: 6; height: 6; radius: 3; color: backend.watching ? theme.accent : win.muted }
            Label { text: backend.watching ? qsTr("Capture is running") : qsTr("Capture is paused"); color: win.muted; font.pixelSize: 11 }
          }
          Label { text: qsTr("Local storage · Your project"); color: win.muted; font.pixelSize: 10 }
          Button { text: qsTr("Keyboard shortcuts  ?"); flat: true; font.pixelSize: 11; onClicked: win.helpVisible = true }
        }
      }
      Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: win.edge }
      ColumnLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        RowLayout {
          Layout.fillWidth: true; Layout.margins: 30; Layout.bottomMargin: 20; spacing: 10
          ColumnLayout {
            Layout.fillWidth: true; spacing: 7
            Caption { text: qsTr("PROJECT INTELLIGENCE / %1").arg([qsTr("OVERVIEW"), qsTr("KNOWLEDGE"), qsTr("ARCHIVE"), qsTr("RECALL"), qsTr("SETUP")][win.page]) }
            Label { text: [qsTr("Start with what you know."), qsTr("Knowledge that accumulates."), qsTr("Every conversation, connected."), qsTr("Find the thread."), qsTr("Connect your coding tools.")][win.page]; color: win.ink; font.pixelSize: 27; font.letterSpacing: -0.7; Layout.fillWidth: true; wrapMode: Text.Wrap }
          }
          Button { text: qsTr("Capture now"); enabled: ready; onClicked: backend.sync() }
          ComboBox { id: provider; model: ["Codex", "Claude"]; implicitWidth: 112; enabled: !backend.busy; onCurrentIndexChanged: backend.learningProvider = currentIndex === 0 ? "codex" : "claude" }
          Button { text: backend.busy ? qsTr("Cancel") : qsTr("Learn"); enabled: ready; highlighted: !backend.busy; onClicked: backend.busy ? backend.cancel() : backend.learn(provider.currentIndex === 0 ? "codex" : "claude") }
        }
        ColumnLayout {
          Layout.fillWidth: true; Layout.leftMargin: 30; Layout.rightMargin: 30; spacing: 10
          RowLayout {
            Layout.fillWidth: true; spacing: 12
            TextField { id: projectPath; Layout.fillWidth: true; text: initialProject; placeholderText: qsTr("Project folder, e.g. /home/you/Work/my-project"); onAccepted: backend.openProject(text, false); selectByMouse: true }
            Button { text: qsTr("Open"); enabled: !backend.busy; onClicked: { if (backend.openProject(projectPath.text, false)) win.document = "" } }
            Button { text: qsTr("Initialize"); enabled: !backend.busy; onClicked: { if (backend.openProject(projectPath.text, true)) win.document = "" } }
          }
          Hairline {}
        }
        StackLayout {
          Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 30; Layout.topMargin: 18
          currentIndex: win.document.length > 0 ? 5 : win.page
          ColumnLayout {
            spacing: 20
            RowLayout {
              Layout.fillWidth: true; spacing: 12
              StatCard { caption: qsTr("CONVERSATIONS"); value: ready ? (win.language, backend.stats.sources.toLocaleString(Qt.locale(), 'f', 0)) : "—" }
              StatCard { caption: qsTr("CAPTURED RECORDS"); value: ready ? (win.language, backend.stats.events.toLocaleString(Qt.locale(), 'f', 0)) : "—" }
              StatCard { caption: qsTr("WIKI ENTRIES"); value: ready ? (win.language, backend.stats.memories.toLocaleString(Qt.locale(), 'f', 0)) : "—" }
            }
            RowLayout { Layout.fillWidth: true
              Caption { text: qsTr("BRAIN.md / ALWAYS READ FIRST"); Layout.fillWidth: true }
              Button { text: qsTr("Open folder"); flat: true; enabled: ready; onClicked: backend.reveal() }
            }
            Rectangle {
              Layout.fillWidth: true; Layout.fillHeight: true; color: "#161a16"; border.color: win.edge; radius: theme.radius
              ScrollView { anchors.fill: parent; anchors.margins: 20; clip: true
                TextArea { readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: win.ink; font.pixelSize: 14; textFormat: TextEdit.RichText; text: backend.markdown(ready ? backend.brain : qsTr("# Give your project a memory\n\nEnter a project folder above and choose Initialize.\n\nLore captures Claude Code and Codex session logs, keeps the original evidence, and turns what you learn into a linked project wiki.\n\nBRAIN.md is the small entry file future sessions read first."), theme.accent); background: null; onLinkActivated: function(link) { win.follow(link) } }
              }
            }
          }
          ColumnLayout {
            spacing: 14
            Label { text: qsTr("Dated facts, decisions, questions, and next steps. Each page links to its evidence."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.memories
              delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: memoryColumn.implicitHeight + 32; color: "#191d19"; border.color: win.edge; radius: theme.radius
                Column { id: memoryColumn; x: 18; y: 16; width: parent.width - 36; spacing: 9
                  Caption { text: modelData.kind.toUpperCase() + "  /  " + modelData.time.substring(0, 10) + (modelData.supersededBy > 0 ? " / " + qsTr("SUPERSEDED") : ""); color: modelData.supersededBy > 0 ? win.muted : theme.accent }
                  Label { text: modelData.title; color: win.ink; font.pixelSize: 17; width: parent.width; wrapMode: Text.Wrap }
                  Label { text: modelData.body; color: win.muted; font.pixelSize: 13; width: parent.width; maximumLineCount: 3; elide: Text.ElideRight; wrapMode: Text.Wrap }
                }
                MouseArea { anchors.fill: parent; onClicked: win.showDocument(modelData.title, backend.readPage(".lore/wiki/" + modelData.id + ".md")) }
              }
              Label { anchors.centerIn: parent; visible: backend.memories.length === 0; text: qsTr("Capture a conversation, then choose Learn to build your wiki."); color: win.muted; width: parent.width - 40; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            }
            Label { text: qsTr("Learn sends pending messages to the selected provider using your existing login."); color: win.muted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
          }
          ColumnLayout {
            Label { text: qsTr("Raw records stay on disk. Repeated imports are deduplicated."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.bottomMargin: 12 }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.sessions
              delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: 98; color: "#191d19"; border.color: win.edge; radius: theme.radius
                Column { x: 18; y: 16; width: parent.width - 36; spacing: 10
                  Caption { text: modelData.provider.toUpperCase() + "  /  " + qsTr("%1 records").arg(modelData.count) }
                  Label { text: modelData.title; color: win.ink; font.pixelSize: 16; width: parent.width; elide: Text.ElideRight }
                  Label { text: modelData.time; color: win.muted; font.pixelSize: 11 }
                }
                MouseArea { anchors.fill: parent; onClicked: win.showDocument(modelData.title, backend.readPage(".lore/sources/" + modelData.id + ".md")) }
              }
              Label { anchors.centerIn: parent; visible: backend.sessions.length === 0; text: qsTr("No conversations captured for this project yet."); color: win.muted }
            }
          }
          ColumnLayout {
            TextField { id: searchField; Layout.fillWidth: true; placeholderText: qsTr("Search decisions, errors, file names, or exact terms…"); selectByMouse: true; onTextChanged: searchTimer.restart() }
            Timer { id: searchTimer; interval: 220; onTriggered: backend.search(searchField.text) }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.results
              delegate: Rectangle {
                required property var modelData
                width: ListView.view.width; height: searchColumn.implicitHeight + 32; color: "#191d19"; border.color: win.edge; radius: theme.radius
                Column { id: searchColumn; x: 18; y: 16; width: parent.width - 36; spacing: 10
                  Caption { text: modelData.provider.toUpperCase() + " / " + modelData.role.toUpperCase(); color: theme.accent }
                  Label { text: modelData.body; color: win.ink; width: parent.width; maximumLineCount: 4; wrapMode: Text.Wrap; elide: Text.ElideRight; font.pixelSize: 13; textFormat: Text.PlainText }
                  Label { text: modelData.id; color: win.muted; font.family: "JetBrains Mono"; font.pixelSize: 10 }
                }
                MouseArea { anchors.fill: parent; onClicked: win.showDocument(modelData.title, backend.readPage(".lore/sources/" + modelData.source + ".md")) }
              }
              Label { anchors.centerIn: parent; visible: backend.results.length === 0; text: qsTr("No matching records."); color: win.muted }
            }
          }
          ScrollView {
            clip: true; contentWidth: availableWidth
            ColumnLayout {
              width: parent.width; spacing: 20
              Label { text: qsTr("One project. Two coding tools. A shared memory."); color: win.ink; font.pixelSize: 21; Layout.fillWidth: true; wrapMode: Text.Wrap }
              Label { text: qsTr("Connect adds a BRAIN.md pointer to AGENTS.md and CLAUDE.md, installs project-local Claude lifecycle hooks, and excludes the private archive from Git. Existing instructions and hooks are preserved."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
              Button { text: qsTr("Connect project"); highlighted: true; enabled: ready; onClicked: backend.connectAgents() }
              Hairline {}
              Label { text: qsTr("Automatic capture"); color: win.ink; font.pixelSize: 18 }
              Label { text: qsTr("Claude hooks capture at prompts, responses, compaction, and session end. For Codex, keep Lore open with capture enabled, or run the watcher below. Only logs belonging to this project are imported."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
              Switch { text: qsTr("Capture session logs every 5 seconds"); checked: backend.watching; enabled: ready; onToggled: backend.watching = checked }
              Switch { text: qsTr("Learn automatically after 60 seconds without new records"); checked: backend.autoLearning; enabled: ready; onToggled: backend.autoLearning = checked }
              Label { text: qsTr("Automatic learning sends pending messages to the selected provider and uses its quota. It stays off until you enable it for this project."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
              TextArea { text: "lore watch --project '" + backend.project.replace(/'/g, "'\\''") + "' --json"; readOnly: true; selectByMouse: true; color: theme.accent; font.family: "JetBrains Mono"; font.pixelSize: 12; wrapMode: TextEdit.Wrap; Layout.fillWidth: true }
              Hairline {}
              Label { text: qsTr("Agent recall via MCP"); color: win.ink; font.pixelSize: 18 }
              Label { text: qsTr("The local MCP server exposes brain_context, brain_search, brain_wiki, and brain_sessions to either coding tool. It only reads memory. See the README for registration commands."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
              TextArea { text: "lore mcp --project '" + backend.project.replace(/'/g, "'\\''") + "'"; readOnly: true; selectByMouse: true; color: theme.accent; font.family: "JetBrains Mono"; font.pixelSize: 12; wrapMode: TextEdit.Wrap; Layout.fillWidth: true }
              Label { text: qsTr("Capture and keyword search are local. Learn uses your chosen model provider. Lore preserves what session logs expose; hidden reasoning and unsaved conversations are unavailable."); color: win.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
          }
          ColumnLayout {
            RowLayout { Layout.fillWidth: true
              Button { text: qsTr("← Back"); flat: true; onClicked: win.document = "" }
              Label { text: win.documentTitle; color: win.muted; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true
              TextArea { text: backend.markdown(win.document, theme.accent); textFormat: TextEdit.RichText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: win.ink; font.pixelSize: 14; background: null; onLinkActivated: function(link) { win.follow(link) } }
            }
          }
        }
        Rectangle {
          Layout.fillWidth: true; implicitHeight: statusText.implicitHeight + 25; color: "#151815"
          Label { id: statusText; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 30; text: backend.status || qsTr("Initialize a project to start building its memory."); color: win.muted; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
        }
      }
    }
    Rectangle {
      visible: win.helpVisible; anchors.fill: parent; color: "#dd101211"
      MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
      Rectangle { anchors.centerIn: parent; width: 470; height: 320; color: "#1c231c"; border.color: win.edge; radius: theme.radius
        Column { anchors.fill: parent; anchors.margins: 30; spacing: 22
          Label { text: qsTr("Keyboard shortcuts"); color: win.ink; font.pixelSize: 24 }
          Label { text: qsTr("Space    Capture now\nCtrl+K   Search memory\nEscape   Close page or help\n?        Show shortcuts\nQ        Quit"); color: win.ink; font.family: "JetBrains Mono"; font.pixelSize: 14; lineHeight: 1.7 }
          Label { text: qsTr("Lore saves captured records and wiki pages automatically."); color: win.muted; width: parent.width; wrapMode: Text.Wrap }
        }
      }
    }
  }
  Dialog {
    id: quitDialog; anchors.centerIn: parent; modal: true; title: qsTr("Cancel learning and quit?")
    standardButtons: Dialog.Yes | Dialog.No
    Label { text: qsTr("The current learning pass has not finished. Captured conversations are already saved."); width: 380; wrapMode: Text.Wrap }
    onAccepted: { backend.cancel(); Qt.quit() }
  }
}
