import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
  id: root
  objectName: "functionWikiPage"
  signal navigate(string page)
  signal read(string title, string text)
  property string selectedId: ""
  readonly property var rows: backend.functions.filter(function(entry) {
    const query = search.text.trim().toLowerCase()
    return !query || [entry.qualified, entry.path, entry.summary].concat((entry.touches || []).map(touch => touch.name)).join(" ").toLowerCase().includes(query)
  })
  readonly property var selected: backend.functions.find(function(entry) { return entry.id === root.selectedId }) || root.rows[0] || null

  RowLayout {
    Layout.fillWidth: true
    TextField {
      id: search
      objectName: "functionSearch"
      Layout.fillWidth: true
      placeholderText: qsTr("Search functions, files, or dependencies")
      selectByMouse: true
      onTextEdited: root.selectedId = ""
    }
    Label { text: qsTr("%1 functions").arg(root.rows.length); color: theme.muted }
    Button { text: qsTr("Scan functions"); onClicked: root.navigate("functionIndex") }
  }

  RowLayout {
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 12
    ListView {
      id: list
      objectName: "indexedFunctionsList"
      Layout.preferredWidth: 320
      Layout.minimumWidth: 220
      Layout.fillHeight: true
      model: root.rows
      clip: true
      spacing: 4
      ScrollBar.vertical: ScrollBar {}
      delegate: ItemDelegate {
        id: functionRow
        required property var modelData
        width: list.width
        highlighted: root.selected && root.selected.id === modelData.id
        onClicked: root.selectedId = modelData.id
        contentItem: ColumnLayout {
          Label { text: functionRow.modelData.qualified; textFormat: Text.PlainText; color: theme.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
          Body { text: functionRow.modelData.path + ":" + functionRow.modelData.line; textFormat: Text.PlainText; font.pixelSize: 11 }
        }
      }
    }
    ScrollView {
      id: detail
      Layout.fillWidth: true
      Layout.fillHeight: true
      clip: true
      contentWidth: availableWidth
      ColumnLayout {
        width: detail.availableWidth
        Card {
          visible: !!root.selected
          heading: root.selected ? root.selected.qualified : ""
          Label {
            Layout.fillWidth: true
            text: root.selected ? root.selected.path + ":" + root.selected.line + (root.selected.endLine > root.selected.line ? "–" + root.selected.endLine : "") : ""
            textFormat: Text.PlainText
            color: theme.accent
            wrapMode: Text.Wrap
          }
          Body { visible: !!(root.selected && root.selected.stale); text: qsTr("Source changed. Scan the file again to refresh this entry."); color: theme.caution }
          TextArea {
            Layout.fillWidth: true
            text: root.selected ? root.selected.signature : ""
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            font.family: "monospace"
            color: theme.ink
          }
          Body { text: root.selected ? root.selected.summary : ""; textFormat: Text.PlainText; color: theme.ink }
          Body {
            visible: !!(root.selected && !root.selected.stale && root.selected.needsDependencyScan)
            text: qsTr("Scan this file again to document its dependencies and effects.")
          }
          Caption {
            visible: !!(root.selected && !root.selected.stale && (root.selected.touches || []).length)
            text: qsTr("Dependencies and effects")
          }
          Repeater {
            model: root.selected && !root.selected.stale ? root.selected.touches || [] : []
            delegate: ColumnLayout {
              required property var modelData
              Layout.fillWidth: true
              spacing: 2
              Body {
                text: qsTr("%1 %2 · %3").arg(modelData.access).arg(modelData.name).arg(modelData.kind)
                textFormat: Text.PlainText
                color: theme.ink
              }
              Body { text: modelData.detail; textFormat: Text.PlainText }
              RowLayout {
                Layout.fillWidth: true
                Button {
                  objectName: "functionTouchSource_" + modelData.line
                  text: qsTr("Source line %1").arg(modelData.line)
                  flat: true
                  onClicked: root.read(root.selected.path + ":" + modelData.line, backend.readPage(root.selected.sourcePage))
                }
                Button {
                  visible: !!modelData.targetPage
                  text: modelData.targetPath || ""
                  flat: true
                  onClicked: root.read(modelData.targetPath, backend.readPage(modelData.targetPage))
                }
              }
            }
          }
          Caption { visible: !!root.selected && !root.selected.stale; text: qsTr("Candidate calls") }
          Repeater {
            model: root.selected && !root.selected.stale ? root.selected.calls || [] : []
            delegate: Button {
              required property var modelData
              Layout.fillWidth: true
              text: qsTr("%1 · line %2").arg(modelData.name).arg(modelData.line)
                + (modelData.target ? "" : qsTr(" · unresolved"))
              enabled: !!modelData.target
              flat: true
              onClicked: root.selectedId = modelData.target
            }
          }
          Caption { visible: !!root.selected && (root.selected.calledBy || []).length > 0; text: qsTr("Candidate callers") }
          Repeater {
            model: root.selected ? root.selected.calledBy || [] : []
            delegate: Button {
              required property var modelData
              Layout.fillWidth: true
              text: modelData.name + " · " + modelData.path + ":" + modelData.line
              flat: true
              onClicked: root.selectedId = modelData.target
            }
          }
        }
        Body { visible: !root.selected; text: search.text ? qsTr("No functions match this search.") : qsTr("Scan project functions to fill the function wiki.") }
      }
    }
  }
}
