import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Timestamp.js" as Timestamp

// Keyword search across project memory and saved conversation evidence.
ColumnLayout {
  id: root
  objectName: "askSearchPage"
  signal read(string title, string text)
  signal follow(string url, string archive)
  spacing: 12
  function focusSearch() { question.forceActiveFocus() }
  function runSearch() { backend.searchProjects(question.text, allProjects.checked) }
  TextField {
    id: question
    objectName: "askQuestion"
    Layout.fillWidth: true
    selectByMouse: true
    placeholderText: qsTr("Search decisions, errors, or file names")
    onTextChanged: searchTimer.restart()
    onAccepted: { searchTimer.stop(); root.runSearch() }
  }
  Timer { id: searchTimer; interval: 220; onTriggered: root.runSearch() }
  RowLayout {
    ComboBox {
      id: evidenceType
      model: [qsTr("All evidence"), qsTr("Conversations"), qsTr("Memory"), qsTr("Code")]
      Accessible.name: qsTr("Evidence type")
    }
    CheckBox { id: allProjects; text: qsTr("All projects"); onToggled: root.runSearch() }
    Item { Layout.fillWidth: true }
  }
  ListView { id: results; Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; clip: true; spacing: 8
    model: backend.results.filter(function(item) {
      switch (evidenceType.currentIndex) {
        case 1: return item.type === "conversation"
        case 2: return item.type === "memory" || item.type === "verification"
        case 3: return item.type === "file" || item.type === "function"
        default: return true
      }
    })
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    delegate: Rectangle {
      required property var modelData
      width: ListView.view.width; height: resultColumn.implicitHeight + 24; radius: theme.radius; color: theme.card; border.color: resultMouse.containsMouse ? theme.accent : theme.edge
      Column { id: resultColumn; x: 14; y: 12; width: parent.width - 28; spacing: 6
        Caption { text: (modelData.projectName ? modelData.projectName + " · " : "") + (modelData.type || modelData.role || "").toUpperCase() + " / " + modelData.title + (modelData.time ? " · " + Timestamp.format(modelData.time) : ""); textFormat: Text.PlainText; color: theme.accent; width: parent.width; elide: Text.ElideRight }
        Label { text: modelData.body; color: theme.ink; width: parent.width; maximumLineCount: 3; wrapMode: Text.Wrap; elide: Text.ElideRight; font.pixelSize: 13; textFormat: Text.PlainText }
      }
      MouseArea {
        id: resultMouse
        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        onClicked: {
          const archive = modelData.archive || ""
          if (modelData.url && modelData.type === "conversation") root.follow(modelData.url, archive)
          else if (modelData.url && modelData.line) root.follow(modelData.url.split("#")[0] + "#L" + modelData.line, archive)
          else if (modelData.url) root.follow(modelData.url, archive)
          else root.read(modelData.title, modelData.body)
        }
      }
    }
    Label { anchors.centerIn: parent; visible: results.count === 0; width: parent.width - 40; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; color: theme.muted
      text: backend.searching || searchTimer.running ? qsTr("Searching…") : backend.searchError.length ? backend.searchError
        : question.text.trim().length > 0 ? qsTr("No matching records.") : qsTr("Capture a conversation or index your project files to start searching.") }
  }
}
