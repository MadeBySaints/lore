import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Timestamp.js" as Timestamp

ColumnLayout {
  id: root
  objectName: "projectFilesPage"
  signal read(string title, string text)
  readonly property bool grid: backend.projectFileGrid
  spacing: 12
  Label {
    text: qsTr("Index code and documents to connect memory to files. Lore respects Git ignore rules and skips common secrets and generated folders.")
    color: theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true
  }
  RowLayout {
    Layout.fillWidth: true
    Button {
      objectName: "indexProjectButton"
      text: backend.files.length ? qsTr("Refresh file index") : qsTr("Index project files")
      enabled: !backend.busy && !backend.showCapturing
      onClicked: backend.indexProject()
    }
    Label {
      id: lastIndexed
      objectName: "lastIndexedDate"
      text: qsTr("%1 files · Last indexed %2").arg(backend.files.length)
        .arg(Timestamp.friendly(backend.stats.indexedAt, qsTr("Today at %1"), qsTr("Yesterday at %1")) || qsTr("Never"))
      color: theme.muted
      wrapMode: Text.Wrap
      Layout.fillWidth: true
      HoverHandler { id: dateHover }
      ToolTip.visible: dateHover.hovered && !!backend.stats.indexedAt
      ToolTip.text: Timestamp.format(backend.stats.indexedAt)
    }
    ButtonGroup { id: layoutChoices }
    Button { objectName: "filesListButton"; text: qsTr("List"); checkable: true; checked: !root.grid; ButtonGroup.group: layoutChoices; onClicked: backend.projectFileGrid = false }
    Button { objectName: "filesGridButton"; text: qsTr("Grid"); checkable: true; checked: root.grid; ButtonGroup.group: layoutChoices; onClicked: backend.projectFileGrid = true }
  }
  StackLayout {
    Layout.fillWidth: true
    Layout.fillHeight: true
    currentIndex: root.grid ? 1 : 0
    ListView {
      id: fileList
      objectName: "projectFileList"
      model: backend.files
      clip: true
      spacing: 8
      ScrollBar.vertical: ScrollBar {}
      delegate: FileTile { width: fileList.width - 6; height: implicitHeight }
    }
    GridView {
      id: fileGrid
      objectName: "projectFileGrid"
      model: backend.files
      clip: true
      cellWidth: width / Math.max(1, Math.floor(width / 245))
      cellHeight: 112
      ScrollBar.vertical: ScrollBar {}
      delegate: FileTile { width: fileGrid.cellWidth - 9; height: fileGrid.cellHeight - 9 }
    }
  }
  Body { text: qsTr("The file index runs locally. Use Function index for model-assisted source documentation.") }

  component FileTile: ItemDelegate {
    id: tile
    required property var modelData
    objectName: "indexedFile_" + modelData.path
    padding: 10
    hoverEnabled: true
    Accessible.name: modelData.path
    ToolTip.visible: hovered
    ToolTip.delay: 650
    ToolTip.text: modelData.path
    onClicked: root.read(modelData.path, backend.readPage(modelData.page))
    background: Rectangle {
      radius: theme.radius
      color: tile.hovered || tile.activeFocus ? theme.hover : theme.card
      border.color: tile.activeFocus ? theme.accent : theme.edge
    }
    contentItem: ColumnLayout {
      spacing: 4
      Label {
        Layout.fillWidth: true
        text: tile.modelData.path
        textFormat: Text.PlainText
        color: theme.ink
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
      }
      Body {
        visible: !!tile.modelData.modified
        text: qsTr("Modified %1").arg(Timestamp.format(tile.modelData.modified))
        font.pixelSize: 11
      }
      Label {
        Layout.fillWidth: true
        text: tile.modelData.symbols || qsTr("Code or document")
        textFormat: Text.PlainText
        color: theme.muted
        font.pixelSize: 11
        wrapMode: Text.Wrap
        maximumLineCount: 2
        elide: Text.ElideRight
      }
    }
  }
}
