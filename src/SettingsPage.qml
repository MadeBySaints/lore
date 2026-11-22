import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Settings: the project's folders, the AI tools and their models, local
// and the app itself.
ColumnLayout {
  id: root
  objectName: "settingsPage"
  spacing: 12
  signal setupRequested()
  property alias tab: tabs.currentIndex
  readonly property bool connected: backend.project.length > 0
  readonly property bool idle: !backend.showBusy && !backend.showCapturing && !backend.selecting
  // Output settings only need background work to be idle, not a running wiki build.
  readonly property bool workIdle: !backend.workBusy && !backend.showCapturing && !backend.selecting
  readonly property bool together: backend.workingDirectory === backend.source
  property string pendingKind: ""
  property string pendingPath: ""
  function shown(path) { return Qt.platform.os === "windows" ? path.replace(/\//g, "\\") : path }
  function propose(kind, path) { pendingKind = kind; pendingPath = path; confirm.open() }
  Connections {
    target: backend
    function onFolderProposed(kind, path) { if (path.length > 0) root.propose(kind, path) }
  }
  TabBar { id: tabs; objectName: "settingsTabs"; Layout.fillWidth: true
    TabButton { objectName: "settingsFoldersTab"; text: qsTr("Folders") }
    TabButton { objectName: "settingsModelsTab"; text: qsTr("AI && models") }
    TabButton { objectName: "settingsAboutTab"; text: qsTr("About") }
  }
  StackLayout { currentIndex: tabs.currentIndex; Layout.fillWidth: true; Layout.fillHeight: true
  ScrollView { id: foldersView; clip: true; contentWidth: availableWidth
  ColumnLayout {
    width: foldersView.availableWidth; spacing: 16
    Label { visible: !root.connected; text: qsTr("Connect a project first. Its folders appear here."); color: theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Rectangle {
      objectName: "settingsMigrateCard"; visible: root.connected && root.together
      Layout.fillWidth: true; implicitHeight: migrateColumn.implicitHeight + 36; color: theme.inset; border.color: theme.accent; radius: theme.radius
      ColumnLayout { id: migrateColumn; anchors.fill: parent; anchors.margins: 18; spacing: 8
        Label { text: qsTr("Lore's files are inside this project folder."); color: theme.ink; font.pixelSize: 15; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Label { text: qsTr("Migrate them to a separate working directory so .lore and BRAIN.md stop mixing with your project. Lore copies the archive, checks the copy, reconnects Claude and Codex, then removes the old copy."); color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Button { id: migrateButton; objectName: "settingsMigrate"; enabled: root.idle
          text: qsTr("Migrate to %1").arg(root.shown((backend.source, backend.suggestedWorkingDirectory())))
          onClicked: root.propose("working", backend.suggestedWorkingDirectory())
          background: Rectangle { implicitHeight: 38; radius: theme.radius; color: !migrateButton.enabled ? theme.disabled : migrateButton.down ? Qt.darker(theme.accent, 1.15) : theme.accent }
          contentItem: Label { text: migrateButton.text; font: migrateButton.font; color: migrateButton.enabled ? theme.accentForeground : theme.muted; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle } }
      }
    }
    Rectangle {
      visible: root.connected; Layout.fillWidth: true; implicitHeight: sourceColumn.implicitHeight + 36; color: theme.card; border.color: theme.edge; radius: theme.radius
      ColumnLayout { id: sourceColumn; anchors.fill: parent; anchors.margins: 18; spacing: 8
        Label { text: qsTr("SOURCE FOLDER"); color: theme.muted; font.pixelSize: 11; font.letterSpacing: 1.2 }
        RowLayout { Layout.fillWidth: true; spacing: 8
          Label { objectName: "settingsSourcePath"; text: root.shown(backend.source); color: theme.ink; font.pixelSize: 15; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
          Button { objectName: "settingsChangeSource"; text: qsTr("Change…"); enabled: root.idle; onClicked: backend.chooseSettingsFolder("source") }
        }
        Label { text: qsTr("The project Lore remembers. Indexing, Verify, and capture use this folder. It only receives Claude hooks and a short pointer in AGENTS.md and CLAUDE.md."); color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
      }
    }
    Rectangle {
      visible: root.connected; Layout.fillWidth: true; implicitHeight: workingColumn.implicitHeight + 36; color: theme.card; border.color: theme.edge; radius: theme.radius
      ColumnLayout { id: workingColumn; anchors.fill: parent; anchors.margins: 18; spacing: 8
        Label { text: qsTr("WORKING DIRECTORY"); color: theme.muted; font.pixelSize: 11; font.letterSpacing: 1.2 }
        RowLayout { Layout.fillWidth: true; spacing: 8
          Label { objectName: "settingsWorkingPath"; text: root.together ? qsTr("Same as the project folder") : root.shown(backend.workingDirectory); color: theme.ink; font.pixelSize: 15; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
          Button { text: qsTr("Open"); flat: true; onClicked: backend.reveal() }
          Button { objectName: "settingsChangeWorking"; text: qsTr("Change…"); enabled: root.idle; onClicked: backend.chooseSettingsFolder("working") }
        }
        Label { text: qsTr("Where Lore keeps its database, wiki, and BRAIN.md for this project."); color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Button { objectName: "settingsMoveToSource"; visible: !root.together; flat: true; font.pixelSize: 11; text: qsTr("Move to project folder"); enabled: root.idle; onClicked: root.propose("working", backend.source) }
      }
    }
  }
  }
  // AI tools: what is installed and signed in, which builds the wiki and
  // answers, and each tool's default model and effort.
  ScrollView { id: modelsView; clip: true; contentWidth: availableWidth
    ColumnLayout { width: modelsView.availableWidth; spacing: 16
      Card { spacing: 6
        RowLayout { Layout.fillWidth: true
          Label { text: qsTr("Local model context"); color: theme.ink; Layout.fillWidth: true }
          Label { color: theme.ink
            text: localContext.value === 0 ? qsTr("Automatic") : qsTr("%1 tokens").arg(Number(localContext.value).toLocaleString(Qt.locale(), "f", 0))
          }
        }
        Slider {
          id: localContext
          objectName: "localContextSlider"
          Accessible.name: qsTr("Local model context")
          Layout.fillWidth: true
          from: 0; to: 131072; stepSize: 4096; snapMode: Slider.SnapAlways
          value: backend.tools.localContextTokens
          onMoved: if (!pressed) backend.tools.localContextTokens = value
          onPressedChanged: if (!pressed) backend.tools.localContextTokens = value
        }
        Body { font.pixelSize: 12; text: qsTr("Ask fits selected evidence and room for the answer into this window. Automatic uses 8,192 tokens for Ask. A larger window can also fit more messages per wiki build round.") }
      }
      Card { objectName: "toolsCard"; spacing: 12
        RowLayout { Layout.fillWidth: true
          Caption { text: qsTr("AI TOOLS"); Layout.fillWidth: true }
          Button { objectName: "checkToolsButton"; text: backend.tools.checking ? qsTr("Checking…") : qsTr("Check tools"); enabled: !backend.tools.checking; onClicked: backend.tools.check() }
        }
        Repeater { model: backend.tools.entries; delegate: ToolRow { required property var modelData; entry: modelData } }
        Body { font.pixelSize: 12; text: qsTr("Lore uses the coding tools and accounts already on this computer, or a local model through Ollama or another loopback server. Credentials stay with each tool; Lore never stores them.") }
      }
      Card { caption: qsTr("WIKI LEARNING"); spacing: 8; visible: backend.connected
        Body { text: qsTr("Choose the model Lore uses to build the project wiki.") }
        Label { text: qsTr("Build wiki"); color: theme.ink }
        ModelOptions { task: "build"; provider: backend.learningProvider }
        Switch {
          objectName: "autoLearningSwitch"
          text: qsTr("Build the wiki while Lore is idle")
          checked: backend.autoLearning
          onToggled: {
            if (checked) {
              backend.autoProvider = backend.learningProvider
              backend.autoAfter = 1
              backend.autoEvery = 0
            }
            backend.autoLearning = checked
          }
        }
        Body { font.pixelSize: 12; text: qsTr("Uses the selected learning model when new messages are ready. Cloud models use the provider’s quota.") }
      }
    }
  }
  ScrollView { id: aboutView; clip: true; contentWidth: availableWidth
    ColumnLayout { width: aboutView.availableWidth; spacing: 16
      Card { caption: qsTr("LORE"); heading: qsTr("Lore %1").arg(backend.version || "?"); spacing: 8
        Body { text: qsTr("A local memory for projects you build with Claude Code or Codex. Your conversations, wiki, and settings stay on this computer.") }
        RowLayout { spacing: 8
          Button { objectName: "settingsCheckUpdates"; text: qsTr("Check for updates"); onClicked: backend.openReleases() }
          Button { objectName: "runSetupAgain"; text: qsTr("Run the setup guide again"); onClicked: root.setupRequested() }
        }
      }
      Card { caption: qsTr("WHAT LORE CHANGES IN YOUR PROJECT"); spacing: 8
        Body { text: qsTr("Lore keeps its database, wiki, and BRAIN.md in its working directory. In the project folder it only adds a short pointer block to AGENTS.md and CLAUDE.md, local Claude Code hooks in .claude/settings.local.json, and a .gitignore block when Lore's files live inside the project. Existing instructions and other hooks are preserved, and disconnecting removes only Lore's blocks.") }
      }
    }
  }
  }
  Dialog {
    id: confirm; objectName: "settingsConfirm"
    anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: Math.min(540, Overlay.overlay.width - 40); modal: true
    title: root.pendingKind === "source" ? qsTr("Change project folder?") : qsTr("Move Lore's files?")
    standardButtons: Dialog.Ok | Dialog.Cancel
    contentItem: ColumnLayout {
      Label {
        Layout.preferredWidth: confirm.availableWidth; wrapMode: Text.Wrap; color: theme.ink
        text: root.pendingKind === "source"
          ? qsTr("Lore will remember %1 instead. It removes its hooks and pointer from the old folder, connects the new one, and re-indexes. Memories that mention files missing from the new folder are flagged for checking.").arg(root.shown(root.pendingPath))
          : qsTr("Lore will copy its files to %1, check the copy, switch to it, and then delete the old copy at %2. Claude hooks and pointers are updated to the new location.").arg(root.shown(root.pendingPath)).arg(root.shown(backend.workingDirectory + "/.lore"))
      }
    }
    onAccepted: root.pendingKind === "source" ? backend.changeSource(root.pendingPath) : backend.changeWorkingDirectory(root.pendingPath)
  }
}
