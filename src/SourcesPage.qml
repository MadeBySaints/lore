import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Timestamp.js" as Timestamp

// Conversation sources, including import and removal.
ColumnLayout {
  id: root
  objectName: "sourcesPage"
  signal read(string title, string text)
  readonly property bool personal: backend.personalWiki
  readonly property int forgottenCount: backend.stats.forgottenConversations || 0
  readonly property bool canDeleteForgotten: forgottenCount > 0 && !backend.busy && !backend.capturing
    && !backend.buildProgress.active && !backend.verifyProgress.active
  spacing: 12
  Connections {
    target: backend
    function onChatGptExportSelected(path) {
      exportDialog.path = path
      exportDialog.conversations = backend.previewChatGptExport(path)
      if (exportDialog.conversations.length) exportDialog.open()
    }
  }
  Dialog {
    id: exportDialog
    title: qsTr("Import from ChatGPT")
    modal: true
    width: Math.min(root.width - 24, 570)
    parent: Overlay.overlay
    anchors.centerIn: parent
    property string path: ""
    property var conversations: []
    contentItem: ColumnLayout {
      Body { text: qsTr("Choose a conversation to add to %1. Lore imports its active branch.").arg(backend.stats.name || qsTr("this project")) }
      ComboBox { id: exportSelection; Layout.fillWidth: true; model: exportDialog.conversations; textRole: "title"; valueRole: "id"; Accessible.name: qsTr("Conversation to import") }
    }
    footer: DialogButtonBox {
      Button { text: qsTr("Cancel"); onClicked: exportDialog.close() }
      PrimaryButton {
        text: qsTr("Import conversation")
        DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
        enabled: exportSelection.currentIndex >= 0 && !backend.busy && !backend.capturing
        onClicked: {
          backend.importChatGptExport(exportDialog.path, exportSelection.currentValue)
          exportDialog.close()
        }
      }
    }
  }
  Dialog {
    id: importDialog
    title: qsTr("Save a conversation")
    modal: true
    width: Math.min(root.width - 24, 660)
    anchors.centerIn: parent
    contentItem: ColumnLayout {
      Body { text: qsTr("Paste a conversation from ChatGPT, Claude, or your local agent. Use User: and Assistant: to label turns when available.") }
      ScrollView {
        Layout.fillWidth: true
        Layout.preferredHeight: 230
        TextArea {
          id: conversation
          objectName: "conversationText"
          placeholderText: qsTr("Paste conversation")
          wrapMode: TextEdit.Wrap
        }
      }
    }
    footer: DialogButtonBox {
      Button { text: qsTr("Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
      PrimaryButton {
        objectName: "saveConversationButton"
        text: qsTr("Save session")
        enabled: conversation.text.trim().length > 0 && !backend.busy && !backend.capturing
        DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
        onClicked: if (backend.saveConversation(conversation.text)) { conversation.clear(); importDialog.close() }
      }
    }
  }
  Dialog {
    id: forgetDialog
    objectName: "forgetConversationDialog"
    property string sourceId: ""
    property string conversationTitle: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(510, parent.width - 32)
    height: 245
    modal: true
    title: sourceId ? qsTr("Forget this conversation?") : qsTr("Forget all conversations?")
    contentItem: Item {
      Body {
        width: parent.width
        textFormat: Text.PlainText
        text: (forgetDialog.sourceId ? forgetDialog.conversationTitle + "\n\n" : "")
          + qsTr("Lore removes the saved turns and analysis findings for this selection. Lore skips these conversations in future captures and hides wiki entries that cite them. Original provider transcripts and project files stay on disk.")
      }
    }
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
      standardButton(Dialog.Ok).text = qsTr("Forget")
      standardButton(Dialog.Ok).objectName = "confirmForgetConversation"
      standardButton(Dialog.Ok).palette.buttonText = theme.warning
    }
    onOpened: standardButton(Dialog.Cancel).forceActiveFocus()
    onAccepted: backend.forgetConversations(sourceId)
    function confirm(id, title) { sourceId = id; conversationTitle = title; open() }
  }
  Dialog {
    id: deleteForgottenDialog
    objectName: "deleteForgottenDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(510, parent.width - 32)
    modal: true
    title: qsTr("Delete forgotten conversations?")
    contentItem: Body {
      text: qsTr("This permanently deletes the remaining Lore records for forgotten conversations and all wiki entries that cite them. Lore will keep skipping these conversations during capture. Original transcripts stay on disk.")
    }
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
      const deleteButton = standardButton(Dialog.Ok)
      deleteButton.text = qsTr("Delete forgotten")
      deleteButton.objectName = "confirmDeleteForgotten"
      deleteButton.palette.buttonText = theme.warning
      deleteButton.enabled = Qt.binding(function() { return root.canDeleteForgotten })
      standardButton(Dialog.Cancel).objectName = "cancelDeleteForgotten"
    }
    onOpened: standardButton(Dialog.Cancel).forceActiveFocus()
    onAccepted: backend.deleteForgottenConversations()
  }
  ColumnLayout { Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
      RowLayout {
        Layout.fillWidth: true
        Button {
          objectName: "openConversationImport"
          text: qsTr("Save a conversation")
          visible: !root.personal
          enabled: !backend.busy && !backend.capturing
          onClicked: importDialog.open()
        }
        Button {
          text: qsTr("Import ChatGPT export…")
          visible: !root.personal
          enabled: !backend.busy && !backend.capturing && !backend.selecting
          onClicked: backend.chooseChatGptExport()
        }
        Label { Layout.fillWidth: true; text: qsTr("Conversations (%1)").arg(backend.sessions.length); color: theme.muted }
        Button {
          objectName: "forgetAllConversations"
          text: qsTr("Forget all conversations…")
          palette.buttonText: theme.warning
          enabled: backend.sessions.length > 0 && !backend.busy && !backend.capturing
          onClicked: forgetDialog.confirm("", "")
        }
      }
      Body { text: qsTr("Your original conversations stay on disk. Repeated captures do not create duplicates.") }
      Body {
        objectName: "captureExclusionsNotice"
        visible: (backend.stats.captureExclusions || 0) > 0
        text: qsTr("Capture skips %1 previously forgotten conversations. Start a fresh session to capture new work.").arg(backend.stats.captureExclusions || 0)
      }
      RowLayout {
        visible: root.forgottenCount > 0
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: qsTr("Forgotten conversations (%1)").arg(root.forgottenCount); color: theme.muted }
        Button {
          objectName: "deleteForgottenConversations"
          text: qsTr("Delete forgotten…")
          palette.buttonText: theme.warning
          enabled: root.canDeleteForgotten
          onClicked: deleteForgottenDialog.open()
        }
      }
      ListView { Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8; model: backend.sessions
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
          required property var modelData
          width: ListView.view.width; height: 78; radius: theme.radius; color: theme.card; border.color: theme.edge
          RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6
            ColumnLayout {
              Layout.fillWidth: true
              Caption { text: modelData.provider.toUpperCase() + " / " + qsTr("%1 records").arg(modelData.count) + (modelData.time ? " · " + Timestamp.format(modelData.time) : "") }
              Label { text: modelData.title; textFormat: Text.PlainText; color: theme.ink; Layout.fillWidth: true; elide: Text.ElideRight }
            }
            Button { text: qsTr("Open"); flat: true; onClicked: root.read(modelData.title, backend.readPage(".lore/sources/" + modelData.id + ".md")) }
            Button {
              text: qsTr("Forget…")
              objectName: "forgetConversation_" + modelData.id
              flat: true
              enabled: !backend.busy && !backend.capturing
              onClicked: forgetDialog.confirm(modelData.id, modelData.title)
            }
          }
        }
        Label { anchors.centerIn: parent; visible: backend.sessions.length === 0; text: root.personal ? qsTr("No conversations yet. Start a new agent session to capture one.") : qsTr("No conversations yet. Start a new coding session in this project."); color: theme.muted; width: parent.width - 40; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter }
      }
  }
}
