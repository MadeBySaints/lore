import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
  id: root
  required property var entry
  signal read(string title, string text)
  function reviewLabel(value) { return {"unreviewed": qsTr("Awaiting review"), "confirmed": qsTr("Confirmed by you"), "corrected": qsTr("Corrected by you"), "outdated": qsTr("Outdated"), "forgotten": qsTr("Forgotten")}[value] || value }
  function originLabel(value) { return {"user correction": qsTr("User correction"), "includes user evidence": qsTr("Includes user evidence"), "assistant claim": qsTr("Assistant claim")}[value] || value }
  color: "#192019"; border.color: "#303a31"; radius: theme.radius
  implicitHeight: content.implicitHeight + 28
  ColumnLayout { id: content; anchors.fill: parent; anchors.margins: 14; spacing: 8
    Label { text: entry.kind.toUpperCase() + " · " + reviewLabel(entry.review) + (entry.supersededBy > 0 ? " · " + qsTr("Superseded") : ""); color: theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { text: originLabel(entry.origin); color: "#9aa69b"; font.pixelSize: 11; Layout.fillWidth: true }
    Label { text: entry.title; color: "#e8ede5"; font.pixelSize: 17; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { text: entry.body; color: "#9aa69b"; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
    Label { visible: !!entry.excluded; text: qsTr("Excluded from recall and learning."); color: "#d8b888"; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Label { visible: entry.stale || entry.conflicts.length > 0; text: entry.stale ? qsTr("Needs checking: linked code changed.") : qsTr("Possible conflicting decisions: %1").arg(entry.conflicts.join(", ")); color: "#d8b888"; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { visible: entry.files.length > 0; text: entry.files.join(" · "); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
    Flow { Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 6
      Button { Material.roundedScale: theme.radius; text: qsTr("Evidence"); onClicked: root.read(entry.title,backend.readPage(".lore/wiki/" + entry.id + ".md")) }
      Button { Material.roundedScale: theme.radius; text: qsTr("Confirm"); enabled: !backend.busy && !backend.capturing && entry.supersededBy === 0 && entry.review !== "forgotten"; onClicked: backend.reviewMemory(entry.id,"confirmed") }
      Button { Material.roundedScale: theme.radius; text: qsTr("Correct"); enabled: !backend.busy && !backend.capturing && entry.supersededBy === 0 && entry.review !== "forgotten"; onClicked: { correction.text=entry.body; correctionDialog.open() } }
      Button { Material.roundedScale: theme.radius; text: qsTr("Outdated"); enabled: !backend.busy && !backend.capturing && entry.review !== "forgotten"; onClicked: backend.reviewMemory(entry.id,"outdated") }
      Button { Material.roundedScale: theme.radius; text: qsTr("Forget"); enabled: !backend.busy && !backend.capturing && entry.review !== "forgotten"; onClicked: forgetDialog.open() }
    }
  }
  Dialog { id: correctionDialog; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: Math.min(540,Overlay.overlay.width-40); modal: true; title: qsTr("Correct this memory"); standardButtons: Dialog.Save | Dialog.Cancel
    contentItem: TextArea { id: correction; objectName: "memoryCorrection"; wrapMode: TextEdit.Wrap; selectByMouse: true; implicitHeight: 190 }
    onAccepted: backend.reviewMemory(entry.id,"corrected",correction.text)
  }
  Dialog { id: forgetDialog; anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: Math.min(480,Overlay.overlay.width-40); modal: true; title: qsTr("Forget this memory?"); standardButtons: Dialog.Yes | Dialog.No
    contentItem: Label { text: qsTr("Exclude this memory and its supporting messages from future recall and learning. Original conversation records stay in the archive. Related memories using those messages are also excluded from recall."); wrapMode: Text.Wrap; color: "#e8ede5" }
    onAccepted: backend.reviewMemory(entry.id,"forgotten")
  }
}
