import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Project status, recent activity, and the next steps in memory.
ScrollView {
  id: root
  objectName: "homePage"
  signal read(string title, string text)
  signal navigate(string page)
  signal follow(string url)
  clip: true; contentWidth: availableWidth
  readonly property bool ready: backend.project.length > 0
  function count(n) { return Number(n || 0).toLocaleString(Qt.locale(), 'f', 0) }
  function toolAvailable(id) { return backend.tools.entries.some(function(tool) { return tool.id === id && tool.available }) }
  readonly property bool buildReady: { const revision = backend.modelRevision; const entries = backend.tools.entries; return backend.modelReady("build", backend.learningProvider) }
  readonly property var countdown: backend.countdown || ({})

  ColumnLayout { width: root.availableWidth; spacing: 16
    Card {
      id: resumeCard
      caption: qsTr("RESUME WORK")
      spacing: 8
      property int revision: 0
      readonly property var handoff: (backend.stats, revision, backend.readHandoff(sessionPicker.currentValue || ""))
      ComboBox {
        id: sessionPicker
        objectName: "resumeSessionPicker"
        Layout.fillWidth: true
        visible: backend.sessions.length > 0
        model: backend.sessions
        textRole: "title"
        valueRole: "id"
        Accessible.name: qsTr("Conversation to resume")
      }
      Body {
        visible: !resumeCard.handoff.session
        text: qsTr("Capture or import a conversation to prepare a handoff.")
      }
      Repeater {
        model: [resumeCard.handoff.latestRequest, resumeCard.handoff.latestReport].filter(function(item) { return !!item })
        delegate: RowLayout {
          required property var modelData
          Layout.fillWidth: true
          ColumnLayout {
            Layout.fillWidth: true
            Caption { text: modelData.role === "user" ? qsTr("Latest request") : qsTr("Latest assistant report") }
            Body { Layout.fillWidth: true; textFormat: Text.PlainText; text: modelData.body }
          }
          Button { text: qsTr("Open source"); flat: true; onClicked: root.follow(modelData.url) }
        }
      }
      Repeater {
        model: [
          {label: qsTr("Task"), key: "task"}, {label: qsTr("Last change"), key: "change"},
          {label: qsTr("Reported checks"), key: "checks"}, {label: qsTr("Blocker"), key: "blocker"},
          {label: qsTr("Next action"), key: "next"}
        ].filter(function(item) { return !!(resumeCard.handoff.notes || {})[item.key] })
        delegate: ColumnLayout {
          required property var modelData
          Layout.fillWidth: true
          Caption { text: modelData.label }
          Body { textFormat: Text.PlainText; text: (resumeCard.handoff.notes || {})[modelData.key] || "" }
        }
      }
      RowLayout {
        visible: !!resumeCard.handoff.session
        Button {
          objectName: "copyResumeHandoff"
          text: qsTr("Copy handoff")
          onClicked: backend.copyHandoff(resumeCard.handoff.text)
        }
        Button {
          objectName: "editResumeHandoff"
          text: qsTr("Edit handoff")
          onClicked: editHandoff.open()
        }
      }
      Dialog {
        id: editHandoff
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(540, root.width - 40)
        title: qsTr("Edit session handoff")
        modal: true
        property string session: ""
        onAboutToShow: {
          session = resumeCard.handoff.session
          const notes = resumeCard.handoff.notes || {}
          task.text = notes.task || ""
          lastChange.text = notes.change || ""
          checks.text = notes.checks || ""
          blocker.text = notes.blocker || ""
          nextAction.text = notes.next || ""
        }
        contentItem: ColumnLayout {
          spacing: 8
          Label { text: qsTr("Task") }
          TextField { id: task; Layout.fillWidth: true; maximumLength: 280; Accessible.name: qsTr("Task") }
          Label { text: qsTr("Last change") }
          TextField { id: lastChange; Layout.fillWidth: true; maximumLength: 280; Accessible.name: qsTr("Last change") }
          Label { text: qsTr("Reported checks") }
          TextField { id: checks; Layout.fillWidth: true; maximumLength: 280; Accessible.name: qsTr("Reported checks") }
          Label { text: qsTr("Blocker") }
          TextField { id: blocker; Layout.fillWidth: true; maximumLength: 280; Accessible.name: qsTr("Blocker") }
          Label { text: qsTr("Next action") }
          TextField { id: nextAction; objectName: "handoffNextAction"; Layout.fillWidth: true; maximumLength: 280; Accessible.name: qsTr("Next action") }
        }
        footer: DialogButtonBox {
          Button { text: qsTr("Cancel"); onClicked: editHandoff.close() }
          PrimaryButton {
            objectName: "saveResumeHandoff"
            text: qsTr("Save handoff")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
              if (!backend.saveHandoff(editHandoff.session, {task: task.text, change: lastChange.text,
                      checks: checks.text, blocker: blocker.text, next: nextAction.text})) return
              ++resumeCard.revision
              editHandoff.close()
            }
          }
        }
      }
    }
    // Status: capture, what is waiting, and what runs next on its own.
    Rectangle { objectName: "homeStatus"; Layout.fillWidth: true; implicitHeight: statusRow.implicitHeight + 32; radius: theme.radius; color: theme.inset; border.color: theme.edge
      RowLayout { id: statusRow; anchors.fill: parent; anchors.margins: 16; spacing: 18
        ColumnLayout { Layout.fillWidth: true; spacing: 4
          RowLayout { spacing: 8
            Rectangle { width: 9; height: 9; radius: 5; color: backend.watching ? theme.accent : theme.muted }
            Label { text: backend.watching ? qsTr("Capturing this project's conversations") : qsTr("Capture is paused"); color: theme.ink; font.pixelSize: 15 } }
          Body { font.pixelSize: 12; text: qsTr("Last capture: %1 · %2 messages awaiting learning").arg(backend.stats.lastCapture ? String(backend.stats.lastCapture).replace("T", " ").slice(0, 16) : qsTr("not yet")).arg(root.count(backend.stats.pending)) }
          Body { font.pixelSize: 12; visible: !!root.countdown.kind; color: theme.accent
            text: root.countdown.kind === "build" ? qsTr("Automatic wiki build starts in %1 s").arg(root.countdown.seconds) : qsTr("Automatic verification starts in %1 s").arg(root.countdown.seconds) }
          Body { font.pixelSize: 12; visible: !root.countdown.kind && (backend.autoLearning || backend.autoVerify); text: [backend.autoStatus, backend.autoVerifyStatus].filter(function(s) { return !!s }).join(" · ") }
        }
        Button { text: qsTr("Sources"); flat: true; onClicked: root.navigate("sources") }
      }
    }
    // A project with no conversations yet: how to get the first one.
    Card { visible: backend.sessions.length === 0; caption: qsTr("PROJECT CONNECTED"); heading: backend.stats.name || ""
      Body { text: qsTr("Lore is watching for saved conversations. Start a coding session in this project, or keep using one that is already open.") }
      Button { text: qsTr("Set up an AI to build the wiki"); visible: !root.buildReady; flat: true; onClicked: root.navigate("settings") }
      RowLayout { spacing: 8
        PrimaryButton { objectName: "startClaudeButton"; text: qsTr("Start Claude Code"); enabled: root.toolAvailable("claude"); onClicked: backend.tools.startSession("claude", backend.source) }
        Button { text: qsTr("Start Codex"); enabled: root.toolAvailable("codex"); onClicked: backend.tools.startSession("codex", backend.source) }
        Button { text: qsTr("Open project folder"); flat: true; onClicked: backend.reveal() }
      }
    }
    RowLayout { visible: backend.sessions.length > 0; Layout.fillWidth: true; spacing: 12
      Stat { caption: qsTr("CONVERSATIONS"); value: root.ready ? root.count(backend.stats.sources) : "0" }
      Stat { caption: qsTr("SAVED RECORDS"); value: root.ready ? root.count(backend.stats.events) : "0" }
      Stat { caption: qsTr("WIKI ENTRIES"); value: root.ready ? root.count(backend.stats.memories) : "0"; detail: backend.reviewWaiting > 0 ? qsTr("%1 awaiting review").arg(root.count(backend.reviewWaiting)) : "" }
    }
    RowLayout { visible: (backend.stats.stale || 0) + (backend.stats.conflicts || 0) > 0; Layout.fillWidth: true; spacing: 8
      Button { visible: (backend.stats.stale || 0) > 0; text: qsTr("%1 entries need checking").arg(backend.stats.stale || 0); onClicked: root.navigate("wiki") }
      Button { visible: (backend.stats.conflicts || 0) > 0; text: qsTr("%1 possible conflicts").arg(backend.stats.conflicts || 0); onClicked: root.navigate("wiki") }
    }
    // Current next steps from the wiki.
    Card { caption: qsTr("WHAT'S NEXT"); spacing: 8
      Repeater { model: backend.nextSteps.slice(0, 6)
        delegate: RowLayout { required property var modelData; Layout.fillWidth: true; spacing: 8
          Label { text: modelData.title; color: theme.ink; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 14 }
          Button { text: qsTr("Open"); flat: true; onClicked: root.read(modelData.title, backend.readPage(".lore/wiki/" + modelData.id + ".md")) }
        }
      }
      Body { visible: backend.nextSteps.length === 0; text: backend.stats.pending > 0 ? qsTr("You have new messages ready to learn from. Choose Build wiki to extract decisions and next steps.") : qsTr("Your next steps will appear here after your first conversation and wiki update.") }
      RowLayout { spacing: 8
        Button { text: qsTr("Read BRAIN.md"); flat: true; enabled: root.ready; onClicked: root.read("BRAIN.md", backend.brain) }
        Button { visible: backend.nextSteps.length > 6; text: qsTr("All %1 next steps in the wiki").arg(backend.nextSteps.length); flat: true; onClicked: root.navigate("wiki") }
      }
    }
    Card { caption: qsTr("RECENT ACTIVITY"); spacing: 6
      Repeater { model: backend.activity.slice(0, 6); delegate: Body { required property var modelData; font.pixelSize: 12; text: modelData.body + " · " + String(modelData.time).replace("T", " ").slice(0, 16) } }
      Body { visible: backend.activity.length === 0; font.pixelSize: 12; text: qsTr("Nothing yet.") }
      Body { font.pixelSize: 11; text: qsTr("Claude capture works even when Lore is closed. Keep Lore open to capture Codex sessions and build the wiki.") }
    }
  }
}
