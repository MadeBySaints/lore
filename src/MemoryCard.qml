import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "Timestamp.js" as Timestamp

Rectangle {
  id: root
  required property var entry
  signal read(string title, string text)
  function reviewLabel(value) { if (entry.byModel) return value === "outdated" ? qsTr("Marked outdated by the model") : qsTr("Accepted by the model"); return {"unreviewed": qsTr("Awaiting review"), "confirmed": qsTr("Confirmed by you"), "corrected": qsTr("Corrected by you"), "outdated": qsTr("Outdated"), "forgotten": qsTr("Forgotten"), "accepted": qsTr("Accepted by the model")}[value] || value }
  function verdictLabel(value) { return {"supported": qsTr("Supported by cited evidence"), "contradicted": qsTr("Contradicted by cited evidence"), "complete": qsTr("Complete"), "incomplete": qsTr("Incomplete"), "unclear": qsTr("Evidence is unclear")}[value] || value }
  function verdictColor(value) { return ["supported", "complete"].includes(value) ? "#a6d189" : ["contradicted", "incomplete"].includes(value) ? "#e78284" : "#d8b888" }
  function checkLabel(check, title) {
    return check ? [title + " · " + verdictLabel(check.verdict), Timestamp.format(check.time),
                    check.current ? "" : qsTr("Cited evidence changed")].filter(Boolean).join(" · ") : ""
  }
  function originLabel(value) { return {"user correction": qsTr("User correction"), "includes user evidence": qsTr("Includes user evidence"), "assistant claim": qsTr("Assistant claim")}[value] || value }
  color: "#192019"; border.color: "#303a31"; radius: theme.radius
  implicitHeight: content.implicitHeight + 28
  ColumnLayout { id: content; anchors.fill: parent; anchors.margins: 14; spacing: 8
    Label { text: entry.kind.toUpperCase() + " · " + reviewLabel(entry.review) + (entry.supersededBy > 0 ? " · " + qsTr("Superseded") : ""); color: theme.accent; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { text: originLabel(entry.origin) + (entry.confidence >= 0 ? "  ·  " + qsTr("Model confidence %1% (%2)").arg(entry.confidence).arg(entry.confidenceSource === "check" ? qsTr("from Verify") : qsTr("from the wiki build")) : ""); color: "#9aa69b"; font.pixelSize: 11; Layout.fillWidth: true }
    Label { visible: !!text; text: [entry.time ? qsTr("Recorded %1").arg(Timestamp.format(entry.time)) : "", entry.reviewTime ? qsTr("Reviewed %1").arg(Timestamp.format(entry.reviewTime)) : ""].filter(Boolean).join(" · "); color: "#9aa69b"; wrapMode: Text.Wrap; font.pixelSize: 11; Layout.fillWidth: true }
    Label { text: entry.title; color: "#e8ede5"; font.pixelSize: 17; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { text: entry.body; color: "#9aa69b"; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
    Label { visible: !!entry.excluded; text: qsTr("Excluded from recall and learning."); color: "#d8b888"; Layout.fillWidth: true; wrapMode: Text.Wrap }
    Label { visible: entry.needsChecking || entry.conflicts.length > 0; text: entry.needsChecking ? qsTr("Needs checking: linked code changed.") : qsTr("Possible conflicting decisions: %1").arg(entry.conflicts.join(", ")); color: "#d8b888"; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { objectName: "verificationLabel"; visible: !!entry.verification; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12
      color: entry.verification ? verdictColor(entry.verification.verdict) : "#9aa69b"
      text: root.checkLabel(entry.verification, qsTr("Code check")) }
    Label { visible: !!entry.verification; text: entry.verification ? entry.verification.summary : ""; color: "#9aa69b"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
    Label { objectName: "completionLabel"; visible: !!entry.completion; text: root.checkLabel(entry.completion, qsTr("Completion check")); color: entry.completion ? verdictColor(entry.completion.verdict) : theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
    Label { visible: !!entry.completion; text: entry.completion ? entry.completion.summary : ""; textFormat: Text.PlainText; color: theme.muted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
    Label { visible: entry.files.length > 0; text: entry.files.join(" · "); color: "#9aa69b"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
    Flow { Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 6
      Button { id: verifyButton; objectName: "verifyMemoryButton"; visible: !backend.personalWiki; text: qsTr("Verify")
        enabled: {
          const tools = backend.tools.entries
          const revision = backend.modelRevision
          return !backend.modelBusy && entry.supersededBy === 0 && !entry.excluded && backend.modelReady("verify", backend.verifyProvider)
        }
        ToolTip.visible: hovered; ToolTip.delay: 600
        ToolTip.text: backend.modelBusy ? qsTr("Available when the current model call finishes.") : qsTr("Ask your selected coding tool to check this memory against indexed documentation and source files. Uses its model quota.")
        onClicked: backend.verifyMemory(entry.id)
        background: Rectangle { implicitWidth: 80; implicitHeight: 36; radius: theme.radius; color: !verifyButton.enabled ? "#2b352d" : verifyButton.down ? Qt.darker(theme.accent, 1.15) : theme.accent }
        contentItem: Label { text: verifyButton.text; font: verifyButton.font; color: verifyButton.enabled ? theme.accentForeground : "#9aa69b"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter } }
      Button { Material.roundedScale: theme.radius; text: qsTr("Evidence"); onClicked: root.read(entry.title,backend.readPage(".lore/wiki/" + entry.id + ".md")) }
      Button { objectName: "checkCompletionButton"; visible: !backend.personalWiki; Material.roundedScale: theme.radius; text: qsTr("Check completion")
        enabled: verifyButton.enabled && !backend.busy && !backend.verifyProgress.active
        onClicked: backend.checkCompletion(entry.id) }
      Button { Material.roundedScale: theme.radius; text: qsTr("Confirm"); enabled: entry.supersededBy === 0 && entry.review !== "forgotten"; onClicked: backend.reviewMemory(entry.id,"confirmed") }
      Button { Material.roundedScale: theme.radius; text: qsTr("Correct"); enabled: entry.supersededBy === 0 && entry.review !== "forgotten"; onClicked: { correctionLoader.active=true; correctionLoader.item.open() } }
      Button { Material.roundedScale: theme.radius; text: qsTr("Outdated"); enabled: entry.review !== "forgotten"; onClicked: backend.reviewMemory(entry.id,"outdated") }
      Button { Material.roundedScale: theme.radius; text: qsTr("Forget"); enabled: entry.review !== "forgotten"; onClicked: { forgetLoader.active=true; forgetLoader.item.open() } }
      Button { objectName: "reopenMemoryButton"; Material.roundedScale: theme.radius; flat: true; text: qsTr("Reopen"); visible: ["confirmed", "outdated", "accepted"].indexOf(entry.review) >= 0; ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: qsTr("Put this entry back to awaiting review."); onClicked: backend.reviewMemory(entry.id, "reopen") }
    }
  }
  // Dialogs load on demand: wiki lists can hold hundreds of cards.
  Loader { id: correctionLoader; active: false
    sourceComponent: Dialog { anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: Math.min(540,Overlay.overlay.width-40); modal: true; title: qsTr("Correct this memory"); standardButtons: Dialog.Save | Dialog.Cancel
      contentItem: TextArea { id: correction; objectName: "memoryCorrection"; wrapMode: TextEdit.Wrap; selectByMouse: true; implicitHeight: 190 }
      onAboutToShow: correction.text=root.entry.body
      onAccepted: backend.reviewMemory(root.entry.id,"corrected",correction.text)
      onClosed: Qt.callLater(function() { correctionLoader.active=false })
    }
  }
  Loader { id: forgetLoader; active: false
    sourceComponent: Dialog { anchors.centerIn: Overlay.overlay; parent: Overlay.overlay; width: Math.min(480,Overlay.overlay.width-40); modal: true; title: qsTr("Forget this memory?"); standardButtons: Dialog.Yes | Dialog.No
      contentItem: Label { text: qsTr("Exclude this memory and its supporting messages from future recall and learning. Original conversation records stay in the archive. Related memories using those messages are also excluded from recall."); wrapMode: Text.Wrap; color: "#e8ede5" }
      onAccepted: backend.reviewMemory(root.entry.id,"forgotten")
      onClosed: Qt.callLater(function() { forgetLoader.active=false })
    }
  }
}
