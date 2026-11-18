import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
  id: win
  width: 1180; height: 800
  minimumWidth: 860; minimumHeight: 620
  visible: true
  title: qsTr("Lore · Project memory")
  color: "#101411"
  Material.theme: Material.Dark
  Material.accent: theme.accent
  Material.roundedScale: theme.radius
  palette.link: theme.accent
  property int page: 0
  property string document: ""
  property string documentTitle: ""
  property bool helpVisible: false
  readonly property color ink: "#e8ede5"
  readonly property color muted: "#9aa69b"
  readonly property color edge: "#303a31"
  readonly property bool ready: backend.project.length > 0
  readonly property bool setup: !backend.connected || page === 4
  readonly property bool typing: activeFocusItem && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit)
  readonly property string language: translator.language
  readonly property var toolEntries: backend.tools.entries
  readonly property var nextSteps: backend.memories.filter(function(entry) { return entry.kind === "next" && entry.supersededBy === 0 && !entry.stale && !entry.excluded && entry.conflicts.length === 0 && entry.review !== "outdated" && entry.review !== "forgotten" })

  function showDocument(title, text) { documentTitle = title; document = text }
  function toolAvailable(id) { return win.toolEntries.some(function(tool) { return tool.id === id && tool.available }) }
  function toolReady(id) { return win.toolEntries.some(function(tool) { return tool.id === id && tool.state === "ready" }) }
  function follow(link) {
    let path = link.toString().split("#")[0]
    if (path.startsWith(".lore/")) showDocument(path, backend.readPage(path))
    else if (path.startsWith("../sources/")) showDocument(qsTr("Source evidence"), backend.readPage(".lore/sources/" + path.substring(11)))
    else if (/^[0-9]+\.md$/.test(path)) showDocument(qsTr("Wiki page"), backend.readPage(".lore/wiki/" + path))
    else if (/^[a-f0-9]+\.md$/.test(path)) showDocument(qsTr("Conversation"), backend.readPage(".lore/sources/" + path))
  }
  Shortcut { sequence: "?"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
  Shortcut { sequence: "Q"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.busy ? quitDialog.open() : Qt.quit() }
  Shortcut { sequence: "Space"; enabled: !win.typing && ready; context: Qt.ApplicationShortcut; onActivated: backend.sync() }
  Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: { win.page = 3; searchField.forceActiveFocus() } }
  Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: backend.chooseProject() }
  Shortcut { sequence: "F11"; context: Qt.ApplicationShortcut; onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen() }
  Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { win.document = ""; win.helpVisible = false } }
  onClosing: function(close) { if (backend.busy) { close.accepted = false; quitDialog.open() } }

  component Caption: Label { color: win.muted; font.pixelSize: 11; font.letterSpacing: 1.2 }
  component Line: Rectangle { color: win.edge; Layout.fillWidth: true; implicitHeight: 1 }
  component Card: Rectangle { color: "#192019"; border.color: win.edge; radius: theme.radius; Layout.fillWidth: true }
  component Stat: Card {
    property string caption
    property string value
    implicitHeight: 98
    Column { anchors.fill: parent; anchors.margins: 18; spacing: 10
      Caption { text: caption }
      Label { text: value; color: win.ink; font.pixelSize: 27 }
    }
  }
  component Body: Label { color: win.muted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
  component PrimaryButton: Button {
    id: primary
    font.pixelSize: 13
    leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
    background: Rectangle { implicitWidth: 110; implicitHeight: 42; radius: theme.radius; color: !primary.enabled ? "#2b352d" : primary.down ? Qt.darker(theme.accent, 1.15) : theme.accent }
    contentItem: Label { text: primary.text; font: primary.font; color: primary.enabled ? theme.accentForeground : win.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
  }
  component ToolRow: ColumnLayout {
    property var entry
    Layout.fillWidth: true; spacing: 6
    RowLayout {
      Layout.fillWidth: true
      Rectangle { width: 8; height: 8; radius: 4; color: entry.state === "ready" ? theme.accent : entry.state === "missing" ? "#bd9971" : win.muted }
      Label { text: entry.name; color: win.ink; font.pixelSize: 16; font.bold: true; Layout.fillWidth: true }
      RadioButton { text: qsTr("Use for learning"); checked: backend.learningProvider === entry.id; enabled: entry.available && !backend.busy; onClicked: backend.learningProvider = entry.id }
      Button { text: qsTr("Install"); visible: !entry.available; onClicked: backend.tools.install(entry.id) }
      Button { text: qsTr("Sign in"); visible: entry.available && entry.state !== "ready"; enabled: !backend.tools.checking; onClicked: backend.tools.signIn(entry.id) }
      Button { text: qsTr("Locate…"); flat: true; enabled: !backend.selecting && !backend.tools.checking; onClicked: backend.locateTool(entry.id) }
    }
    Body { text: entry.detail }
  }

  Item {
    anchors.fill: parent
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    RowLayout {
      anchors.fill: parent; spacing: 0
      Rectangle {
        Layout.preferredWidth: 192; Layout.fillHeight: true; color: "#151b16"
        ColumnLayout {
          anchors.fill: parent; anchors.margins: 12; spacing: 4
          RowLayout {
            Rectangle { width: 30; height: 34; color: theme.accent; radius: theme.radius
              Label { anchors.centerIn: parent; text: "L"; color: theme.accentForeground; font.bold: true; font.pixelSize: 25 }
            }
            Label { text: "lore"; color: win.ink; font.pixelSize: 29 }
          }
          Body { text: qsTr("Your project, remembered."); font.pixelSize: 11; Layout.bottomMargin: 4 }
          Caption { text: qsTr("PROJECT") }
          Label { text: ready ? backend.stats.name : qsTr("Choose a project"); color: win.ink; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
          Button { Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34; objectName: "switchProjectButton"; text: qsTr("Switch project…"); flat: true; enabled: !backend.busy && !backend.capturing && !backend.selecting; onClicked: backend.chooseProject() }
          Repeater {
            model: [qsTr("Overview"), qsTr("Ask project"), qsTr("Project wiki"), qsTr("Conversations"), qsTr("Search"), qsTr("Project files"), qsTr("Context"), qsTr("Connections")]
            delegate: Rectangle {
              required property int index
              required property string modelData
              readonly property int targetPage: [0,6,1,2,3,7,8,4][index]
              objectName: "nav" + targetPage
              Layout.fillWidth: true; implicitHeight: 30; radius: theme.radius
              color: (win.setup && targetPage === 4) || (!win.setup && win.page === targetPage) ? "#2b3828" : navMouse.containsMouse ? "#202a20" : "transparent"
              Label { anchors.verticalCenter: parent.verticalCenter; x: 12; text: modelData; color: win.ink; font.pixelSize: 13; opacity: ready || targetPage === 4 ? 1 : 0.45 }
              MouseArea { id: navMouse; anchors.fill: parent; hoverEnabled: true; enabled: ready || targetPage === 4; onClicked: { win.page = targetPage; win.document = "" } }
            }
          }
          Item { Layout.fillHeight: true }
          Line {}
          RowLayout {
            Rectangle { width: 7; height: 7; radius: 4; color: backend.watching ? theme.accent : win.muted }
            Label { text: backend.watching ? qsTr("Capture is on") : qsTr("Capture is paused"); color: win.muted; font.pixelSize: 11 }
          }
          Body { text: backend.autoLearning ? qsTr("Wiki updates automatically") : qsTr("Wiki updates when you choose"); font.pixelSize: 10 }
          Button { Layout.preferredHeight: 32; Layout.minimumHeight: 32; Layout.maximumHeight: 32; text: qsTr("Help & shortcuts"); flat: true; font.pixelSize: 11; onClicked: win.helpVisible = true }
        }
      }
      Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: win.edge }
      ColumnLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        RowLayout {
          Layout.fillWidth: true; Layout.margins: 26; spacing: 12
          ColumnLayout {
            Layout.fillWidth: true; spacing: 6
            Caption { text: win.setup ? qsTr("LET’S GET YOU CONNECTED") : qsTr("PROJECT MEMORY") }
            Label { text: win.setup ? qsTr("Give your project a memory.") : [qsTr("Pick up where you left off."), qsTr("Your project wiki"), qsTr("Saved conversations"), qsTr("Find something you remember."), qsTr("Connections"), "", qsTr("Ask your project"), qsTr("Your project files"), qsTr("Context & delivery")][win.page]; color: win.ink; font.pixelSize: 26; Layout.fillWidth: true; wrapMode: Text.Wrap }
          }
          Button { text: backend.capturing ? qsTr("Capturing…") : qsTr("Capture now"); enabled: ready && !backend.capturing && !backend.busy; visible: !win.setup; onClicked: backend.sync() }
          ComboBox { id: provider; model: ["Claude Code", "Codex"]; currentIndex: backend.learningProvider === "claude" ? 0 : 1; implicitWidth: 166; enabled: !backend.busy; visible: !win.setup; onActivated: backend.learningProvider = currentIndex === 0 ? "claude" : "codex" }
          PrimaryButton { text: backend.busy ? qsTr("Cancel") : qsTr("Build wiki"); enabled: ready && (backend.busy || (backend.stats.pending > 0 && win.toolReady(backend.learningProvider))); visible: !win.setup; onClicked: backend.busy ? backend.cancel() : backend.learn(backend.learningProvider) }
        }
        Line {}
        Rectangle {
          Layout.fillWidth: true; implicitHeight: errorRow.implicitHeight + 24; visible: backend.error.length > 0; color: "#38241f"
          RowLayout { id: errorRow; anchors.fill: parent; anchors.margins: 12
            Label { text: backend.error; color: "#f0c7b5"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
            Button { text: qsTr("Dismiss"); flat: true; onClicked: backend.clearError() }
          }
        }
        StackLayout {
          Layout.minimumHeight: 0;
          Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 26; Layout.topMargin: 18
          currentIndex: win.document.length > 0 ? 5 : win.setup ? 4 : win.page
          ScrollView {
            clip: true; contentWidth: availableWidth
            ColumnLayout { width: parent.width; spacing: 18
              Card { visible: backend.sessions.length === 0; implicitHeight: connectedContent.implicitHeight + 36
                ColumnLayout { id: connectedContent; anchors.fill: parent; anchors.margins: 18; spacing: 12
                  Caption { text: qsTr("PROJECT CONNECTED"); color: theme.accent }
                  Label { text: backend.stats.name || ""; color: win.ink; font.pixelSize: 23; font.bold: true }
                  Body { text: qsTr("Lore is watching for saved conversations. You can keep using an existing coding session.") }
                  Button { text: qsTr("Set up learning in Connections"); visible: !win.toolReady(backend.learningProvider); flat: true; onClicked: win.page = 4 }
                  RowLayout {
                    PrimaryButton { objectName: "startClaudeButton"; text: qsTr("Start Claude Code"); enabled: win.toolAvailable("claude"); onClicked: backend.tools.startSession("claude", backend.project) }
                    Button { text: qsTr("Start Codex"); enabled: win.toolAvailable("codex"); onClicked: backend.tools.startSession("codex", backend.project) }
                    Button { text: qsTr("Open project folder"); flat: true; onClicked: backend.reveal() }
                  }
                }
              }
              RowLayout { visible: backend.sessions.length > 0; Layout.fillWidth: true
                Button { text: qsTr("Start Claude Code"); enabled: win.toolAvailable("claude"); onClicked: backend.tools.startSession("claude",backend.project) }
                Button { text: qsTr("Start Codex"); enabled: win.toolAvailable("codex"); onClicked: backend.tools.startSession("codex",backend.project) }
                Button { text: qsTr("Ask project"); onClicked: win.page=6 }
              }
              Body { text: qsTr("Last capture: %1 · %2 messages awaiting learning").arg(backend.stats.lastCapture || qsTr("Not yet")).arg(backend.stats.pending || 0) }
              RowLayout { Layout.fillWidth: true; spacing: 12
                Stat { caption: qsTr("CONVERSATIONS"); value: ready ? (win.language, backend.stats.sources.toLocaleString(Qt.locale(), 'f', 0)) : "0" }
                Stat { caption: qsTr("SAVED RECORDS"); value: ready ? (win.language, backend.stats.events.toLocaleString(Qt.locale(), 'f', 0)) : "0" }
                Stat { caption: qsTr("WIKI ENTRIES"); value: ready ? (win.language, backend.stats.memories.toLocaleString(Qt.locale(), 'f', 0)) : "0" }
              }
              RowLayout { Layout.fillWidth: true
                Button { text: qsTr("%1 memories need checking").arg(backend.stats.stale || 0); onClicked: win.page=1 }
                Button { text: qsTr("%1 possible conflicts").arg(backend.stats.conflicts || 0); onClicked: win.page=1 }
                Button { text: qsTr("View context returned to Claude"); onClicked: win.page=8 }
              }
              Card { implicitHeight: nextContent.implicitHeight + 36
                ColumnLayout { id: nextContent; anchors.fill: parent; anchors.margins: 18; spacing: 12
                  Label { text: qsTr("What’s next"); color: win.ink; font.pixelSize: 18 }
                  Repeater { model: win.nextSteps.slice(0, 5)
                    delegate: RowLayout { required property var modelData; Layout.fillWidth: true
                      Label { text: modelData.title; color: win.ink; Layout.fillWidth: true; wrapMode: Text.Wrap }
                      Button { text: qsTr("Open"); onClicked: win.showDocument(modelData.title, backend.readPage(".lore/wiki/" + modelData.id + ".md")) }
                    }
                  }
                  Body { visible: win.nextSteps.length === 0; text: backend.stats.pending > 0 ? qsTr("You have new messages ready to learn from. Choose Build wiki to extract decisions and next steps.") : qsTr("Your next steps will appear here after your first conversation and wiki update.") }
                  Button { text: qsTr("Read the project entry point"); flat: true; enabled: ready; onClicked: win.showDocument("BRAIN.md", backend.brain) }
                }
              }
              Caption { text: qsTr("RECENT ACTIVITY") }
              Repeater { model: backend.activity.slice(0,6); delegate: Body { required property var modelData; text: modelData.body + " · " + modelData.time } }
              Body { text: qsTr("Claude capture works even when Lore is closed. Keep Lore open to capture Codex sessions and run automatic wiki updates.") }
            }
          }
          ColumnLayout {
            spacing: 14
            Body { text: qsTr("Facts, decisions, questions, and next steps. Open a page to see its original evidence.") }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.memories
              delegate: MemoryCard { required property var modelData; entry: modelData; width: ListView.view.width; height: implicitHeight; onRead: function(title,text) { win.showDocument(title,text) } }
              Label { anchors.centerIn: parent; visible: backend.memories.length === 0; text: qsTr("Capture a conversation, then choose Build wiki."); color: win.muted; width: parent.width - 40; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            }
          }
          ColumnLayout {
            Body { text: qsTr("Your original conversations stay on disk. Repeated captures do not create duplicates.") }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.sessions
              delegate: Card {
                required property var modelData
                width: ListView.view.width; height: 94
                Column { x: 18; y: 16; width: parent.width - 36; spacing: 10
                  Caption { text: modelData.provider.toUpperCase() + " / " + qsTr("%1 records").arg(modelData.count) }
                  Label { text: modelData.title; color: win.ink; font.pixelSize: 16; width: parent.width; elide: Text.ElideRight }
                  Label { text: modelData.time; color: win.muted; font.pixelSize: 11 }
                }
                MouseArea { anchors.fill: parent; onClicked: win.showDocument(modelData.title, backend.readPage(".lore/sources/" + modelData.id + ".md")) }
              }
              Label { anchors.centerIn: parent; visible: backend.sessions.length === 0; text: qsTr("No conversations yet. Start a new coding session in this project."); color: win.muted; width: parent.width - 40; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
            }
          }
          ColumnLayout {
            TextField { id: searchField; objectName: "searchField"; Layout.fillWidth: true; placeholderText: qsTr("Search decisions, errors, file names, or exact terms…"); selectByMouse: true; onTextChanged: searchTimer.restart() }
            Timer { id: searchTimer; interval: 220; onTriggered: backend.search(searchField.text) }
            ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.results
              delegate: Card {
                required property var modelData
                width: ListView.view.width; height: searchColumn.implicitHeight + 32
                Column { id: searchColumn; x: 18; y: 16; width: parent.width - 36; spacing: 10
                  Caption { text: (modelData.type || "").toUpperCase() + " / " + modelData.title; color: theme.accent }
                  Label { text: modelData.body; color: win.ink; width: parent.width; maximumLineCount: 4; wrapMode: Text.Wrap; elide: Text.ElideRight; font.pixelSize: 13; textFormat: Text.PlainText }
                }
                MouseArea { anchors.fill: parent; onClicked: win.showDocument(modelData.title, modelData.body) }
              }
              Label { anchors.centerIn: parent; visible: backend.results.length === 0; text: qsTr("No matching records."); color: win.muted }
            }
          }
          ColumnLayout {
            spacing: 12
            ScrollView {
              Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
              ColumnLayout { width: parent.width; spacing: 16
                Card { implicitHeight: folderContent.implicitHeight + 36
                  ColumnLayout { id: folderContent; anchors.fill: parent; anchors.margins: 18; spacing: 10
                    Caption { text: qsTr("1 / CHOOSE A PROJECT") }
                    RowLayout { Layout.fillWidth: true
                      Label { text: backend.selectedProject.length > 0 ? qsTr("Project folder selected") : qsTr("Which project should Lore remember?"); color: win.ink; font.pixelSize: 18; Layout.fillWidth: true; wrapMode: Text.Wrap }
                      PrimaryButton { objectName: "chooseProjectButton"; text: backend.selecting ? qsTr("Choosing…") : backend.selectedProject.length > 0 ? qsTr("Change folder…") : qsTr("Choose folder…"); enabled: !backend.selecting && !backend.busy && !backend.capturing; onClicked: backend.chooseProject() }
                    }
                    Body { text: backend.selectedProject || qsTr("Select the folder that contains your project’s code or documents.") }
                    Button { id: manualToggle; text: qsTr("Enter a folder path instead"); flat: true; checkable: true; font.pixelSize: 11 }
                    RowLayout { visible: manualToggle.checked; Layout.fillWidth: true
                      TextField { id: manualPath; Layout.fillWidth: true; placeholderText: qsTr("Project folder path"); selectByMouse: true; onAccepted: backend.selectProject(text) }
                      Button { text: qsTr("Use folder"); onClicked: backend.selectProject(manualPath.text) }
                    }
                    ColumnLayout { visible: backend.selectedProject.length === 0 && backend.recentProjects.length > 0; Layout.fillWidth: true
                      Caption { text: qsTr("RECENT PROJECTS") }
                      Repeater { model: backend.recentProjects
                        delegate: Button { required property var modelData; text: modelData.name; flat: true; onClicked: backend.selectProject(modelData.path) }
                      }
                    }
                  }
                }
                Card { implicitHeight: toolContent.implicitHeight + 36
                  ColumnLayout { id: toolContent; anchors.fill: parent; anchors.margins: 18; spacing: 10
                    RowLayout { Layout.fillWidth: true
                      Caption { text: qsTr("2 / CHECK YOUR CODING TOOLS"); Layout.fillWidth: true }
                      Button { objectName: "checkToolsButton"; text: backend.tools.checking ? qsTr("Checking…") : qsTr("Check tools"); enabled: !backend.tools.checking; onClicked: backend.tools.check() }
                    }
                    Repeater { model: backend.tools.entries; delegate: ToolRow { required property var modelData; entry: modelData } }
                    Body { text: qsTr("Lore uses the coding tools and accounts already on this computer. Install or sign in above if needed.") }
                  }
                }
                Card { implicitHeight: captureContent.implicitHeight + 36
                  ColumnLayout { id: captureContent; anchors.fill: parent; anchors.margins: 18; spacing: 8
                    Caption { text: qsTr("OPTIONAL / CAPTURE & LEARNING") }
                    Switch { objectName: "captureSwitch"; text: qsTr("Capture automatically while Lore is open"); checked: backend.watching || !backend.connected; enabled: backend.connected; onToggled: backend.watching = checked }
                    Body { text: qsTr("Capture turns on when you connect. Claude also saves through project hooks when Lore is closed.") }
                    Switch { objectName: "autoLearningSwitch"; text: qsTr("Update the wiki automatically"); checked: backend.autoLearning; enabled: backend.connected && win.toolReady(backend.learningProvider); onToggled: backend.autoLearning = checked }
                    Body { visible: !backend.connected; text: qsTr("Connect this project first to enable automatic wiki updates.") }
                    Body { visible: backend.connected && !win.toolReady(backend.learningProvider); text: qsTr("Sign in to your selected tool above to enable wiki updates.") }
                    Body { text: qsTr("Automatic wiki updates send new messages to your selected tool after a quiet minute and use its model quota. Leave this off to choose Build wiki yourself.") }
                    Button { text: qsTr("What will Lore change?"); checkable: true; flat: true; id: changesToggle }
                    Body { visible: changesToggle.checked; text: qsTr("Lore creates a private .lore archive, adds a BRAIN.md entry point, and connects the project’s AGENTS.md, CLAUDE.md, and local Claude hooks. Existing instructions and other hooks are preserved.") }
                  }
                }
                Card { implicitHeight: semanticContent.implicitHeight+36
                  ColumnLayout { id: semanticContent; anchors.fill: parent; anchors.margins: 18; spacing: 8
                    Caption { text: qsTr("OPTIONAL / LOCAL SEMANTIC SEARCH") }
                    Body { text: qsTr("Find related ideas even when wording differs. Install and open Ollama, then download the local embedding model (about 622 MB). Project text stays on this computer for embeddings.") }
                    Body { text: backend.semantic.enabled ? qsTr("Enabled · %1 excerpts indexed. New captures and memory changes update the index automatically.").arg(backend.semantic.indexed || 0) : qsTr("Off · keyword search works without a model.") }
                    Body { visible: !!backend.semantic.enabled && !!backend.semantic.needsUpdate; text: qsTr("New evidence is waiting to be indexed. Choose Update index to catch up now.") }
                    ProgressBar { visible: backend.busy; indeterminate: true; Layout.fillWidth: true }
                    Flow { Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 6
                      Button { text: qsTr("Install Ollama"); onClicked: backend.installSemanticRuntime() }
                      Button { objectName: "enableSemanticButton"; text: qsTr("Download model & enable"); enabled: backend.connected && !backend.busy && !backend.capturing; onClicked: backend.updateSemantic(true) }
                      Button { text: qsTr("Update index"); enabled: backend.connected && !backend.busy && !backend.capturing; onClicked: backend.updateSemantic(false) }
                      Button { text: qsTr("Turn off"); enabled: !!backend.semantic.enabled && !backend.busy; onClicked: backend.disableSemantic() }
                    }
                    Body { text: qsTr("If Ollama is unavailable, search uses keywords and reports the fallback. Answers and wiki learning still use your selected coding tool.") }
                  }
                }
                Card { implicitHeight: notificationContent.implicitHeight+36
                  ColumnLayout { id: notificationContent; anchors.fill: parent; anchors.margins: 18; spacing: 8
                    Caption { text: qsTr("NOTIFICATIONS") }
                    Switch { objectName: "notificationsSwitch"; text: qsTr("Show notification banners"); checked: backend.notificationsEnabled; onToggled: backend.notificationsEnabled=checked }
                    Body { text: qsTr("Banners clear on success or after 12 seconds. Temporary database contention retries quietly. Recent issues remain available here even with banners off.") }
                    Button { text: qsTr("View recent issues (%1)").arg(backend.notices.length); onClicked: win.showDocument(qsTr("Recent issues"),backend.notices.map(function(n) { return n.time + "\n" + n.text }).join("\n\n") || qsTr("No issues in this app session.")) }
                  }
                }
              }
            }
            Line {}
            RowLayout { Layout.fillWidth: true
              Body { text: backend.connected ? qsTr("This project is connected. You can start a new coding session.") : qsTr("Connect once. Lore will remember this project next time.") }
              PrimaryButton { objectName: "connectProjectButton"; text: backend.connected ? qsTr("Done") : qsTr("Connect project"); enabled: backend.selectedProject.length > 0 && !backend.busy && !backend.capturing; onClicked: { if (backend.connected || backend.connectProject()) { win.page = 0; win.document = "" } } }
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
          AskPage { onPreviewRequested: win.page=8; onRead: function(title,text) { win.showDocument(title,text) } }
          ProjectPage { onRead: function(title,text) { win.showDocument(title,text) } }
          ContextPage { onRead: function(title,text) { win.showDocument(title,text) } }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: statusText.implicitHeight + 22; color: "#151b16"
          Label { id: statusText; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 26; text: backend.status || qsTr("Choose a project folder to get started."); color: win.muted; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight }
        }
      }
    }
    Rectangle { visible: win.helpVisible; anchors.fill: parent; color: "#ed101411"
      MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
      Rectangle { anchors.centerIn: parent; width: Math.min(530, win.width - 60); height: 440; color: "#1c271d"; border.color: win.edge; radius: theme.radius
        Column { anchors.fill: parent; anchors.margins: 28; spacing: 20
          Label { text: qsTr("Help & shortcuts"); color: win.ink; font.pixelSize: 24 }
          Label { text: qsTr("Choose a folder, check your tools, and connect. Use Start Claude Code or Start Codex to begin a session in the right project."); color: win.muted; width: parent.width; wrapMode: Text.Wrap }
          Label { text: qsTr("Ctrl+O   Choose project folder\nCtrl+K   Search memory\nSpace    Capture now\nF11      Toggle full screen\nEscape   Close page or help\n?        Show this help\nQ        Quit"); color: win.ink; font.family: "monospace"; font.pixelSize: 13; lineHeight: 1.5 }
          PrimaryButton { objectName: "checkUpdatesButton"; text: qsTr("Check for updates"); onClicked: backend.openReleases() }
        }
      }
    }
  }
  Dialog { id: quitDialog; anchors.centerIn: parent; modal: true; title: qsTr("Cancel learning and quit?"); standardButtons: Dialog.Yes | Dialog.No
    Label { text: qsTr("Learning is still running. Captured conversations are already saved."); width: 360; wrapMode: Text.Wrap }
    onAccepted: { backend.cancel(); Qt.quit() }
  }
}
