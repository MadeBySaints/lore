import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Lore's own project browser: every remembered project with its source and
// working directory, plus folders used with Claude Code or Codex that have no
// memory yet. The system folder chooser stays available as a fallback.
ScrollView {
  id: root
  contentWidth: availableWidth
  clip: true
  signal opened()
  signal setUp()
  readonly property bool idle: !backend.showBusy && !backend.showCapturing && !backend.selecting
  property string filter: ""
  function shown(path) { return Qt.platform.os === "windows" ? path.replace(/\//g, "\\") : path }
  function matches(item) {
    const words = root.filter.trim().toLowerCase()
    return words.length === 0 || [item.name, item.source, item.working, item.path].join(" ").toLowerCase().indexOf(words) >= 0
  }
  function when(stamp) { return stamp ? new Date(stamp).toLocaleDateString(Qt.locale(), Locale.ShortFormat) : "" }
  onVisibleChanged: if (visible) backend.scanProjects()
  // Opening or selecting a project rebuilds these lists and destroys the
  // clicked delegate, so the follow-up runs here rather than in the delegate.
  function openKnown(working) { if (backend.openKnownProject(working)) opened() }
  function setUpFolder(path) { if (backend.selectProject(path)) setUp() }

  ColumnLayout {
    width: root.availableWidth; spacing: 16
    RowLayout { Layout.fillWidth: true; spacing: 8
      TextField { objectName: "projectFilter"; Layout.fillWidth: true; placeholderText: qsTr("Filter projects by name or folder"); selectByMouse: true; onTextChanged: root.filter = text }
      Button { objectName: "scanProjectsButton"; text: backend.scanningProjects ? qsTr("Looking…") : qsTr("Find projects"); enabled: !backend.scanningProjects; onClicked: backend.scanProjects() }
      Button { objectName: "browseProjectButton"; text: qsTr("Browse…"); enabled: root.idle; onClicked: { backend.chooseProject(); root.setUp() } }
    }
    RowLayout { Layout.fillWidth: true; spacing: 8
      TextField { id: pathField; objectName: "projectPathField"; Layout.fillWidth: true; placeholderText: qsTr("Or paste a project folder path"); selectByMouse: true; onAccepted: useButton.clicked() }
      Button { id: useButton; text: qsTr("Use folder"); enabled: root.idle && pathField.text.trim().length > 0; onClicked: { if (backend.selectProject(pathField.text)) { pathField.text = ""; backend.connected ? root.opened() : root.setUp() } } }
    }

    Label { text: qsTr("REMEMBERED PROJECTS"); color: "#9aa69b"; font.pixelSize: 11; font.letterSpacing: 1.2 }
    Label { visible: backend.knownProjects.length === 0; text: qsTr("No projects yet. Find projects on this computer, browse to a folder, or set up one of the folders below."); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Repeater {
      model: backend.knownProjects
      delegate: Rectangle {
        required property var modelData
        required property int index
        objectName: "knownProject_" + modelData.name
        visible: root.matches(modelData)
        Layout.fillWidth: true; implicitHeight: visible ? known.implicitHeight + 32 : 0
        color: modelData.current ? "#24291c" : "#192019"; border.color: modelData.current ? theme.accent : "#303a31"; radius: theme.radius
        ColumnLayout { id: known; anchors.fill: parent; anchors.margins: 16; spacing: 6
          RowLayout { Layout.fillWidth: true; spacing: 8
            Label { text: modelData.name; color: "#e8ede5"; font.pixelSize: 17; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { visible: modelData.current; text: qsTr("OPEN NOW"); color: theme.accent; font.pixelSize: 11; font.letterSpacing: 1.2 }
            Button { objectName: "openProject_" + modelData.name; visible: !modelData.current; text: qsTr("Open"); enabled: root.idle && !modelData.missing; onClicked: root.openKnown(modelData.working) }
            Button { objectName: "forgetProject_" + modelData.name; visible: !modelData.current; text: qsTr("Remove"); flat: true; enabled: root.idle; onClicked: backend.forgetProject(modelData.working) }
          }
          GridLayout { columns: 2; columnSpacing: 12; rowSpacing: 2; Layout.fillWidth: true
            Label { text: qsTr("Source"); color: "#9aa69b"; font.pixelSize: 12 }
            Label { text: root.shown(modelData.source); color: "#e8ede5"; font.pixelSize: 12; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: qsTr("Working directory"); color: "#9aa69b"; font.pixelSize: 12 }
            Label { text: modelData.together ? qsTr("Inside the project folder") : root.shown(modelData.working); color: "#e8ede5"; font.pixelSize: 12; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
          }
          Label {
            text: modelData.missing ? qsTr("Lore's files were not found here. Remove it, or browse to where the project moved.")
              : [qsTr("%1 conversations").arg(modelData.conversations || 0), qsTr("%1 wiki entries").arg(modelData.entries || 0)].concat(modelData.lastOpened ? [qsTr("opened %1").arg(root.when(modelData.lastOpened))] : []).join(" · ")
            color: modelData.missing ? "#f0c7b5" : "#9aa69b"; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true
          }
        }
      }
    }

    Label { visible: backend.suggestedProjects.length > 0; text: qsTr("FOUND ON THIS COMPUTER"); color: "#9aa69b"; font.pixelSize: 11; font.letterSpacing: 1.2; Layout.topMargin: 8 }
    Label { visible: backend.suggestedProjects.length > 0; text: qsTr("Folders you have used with Claude Code or Codex that have no Lore memory yet."); color: "#9aa69b"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Repeater {
      model: backend.suggestedProjects
      delegate: Rectangle {
        required property var modelData
        objectName: "suggestedProject_" + modelData.name
        visible: root.matches(modelData)
        Layout.fillWidth: true; implicitHeight: visible ? suggestion.implicitHeight + 24 : 0
        color: "#151b16"; border.color: "#303a31"; radius: theme.radius
        RowLayout { id: suggestion; anchors.fill: parent; anchors.margins: 12; spacing: 8
          ColumnLayout { Layout.fillWidth: true; spacing: 2
            Label { text: modelData.name; color: "#e8ede5"; font.pixelSize: 14; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: root.shown(modelData.path) + " · " + modelData.tools + (modelData.lastUsed ? " · " + qsTr("used %1").arg(root.when(modelData.lastUsed)) : ""); color: "#9aa69b"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
          }
          Button { objectName: "setUpProject_" + modelData.name; text: qsTr("Set up"); enabled: root.idle; onClicked: root.setUpFolder(modelData.path) }
          Button { text: qsTr("Hide"); flat: true; onClicked: backend.forgetProject(modelData.path) }
        }
      }
    }
  }
}
