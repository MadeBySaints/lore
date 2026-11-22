import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Timestamp.js" as Timestamp

ScrollView {
  id: root
  objectName: "skillsPage"
  clip: true
  contentWidth: availableWidth
  readonly property string project: backend.project
  property int revision: 0
  readonly property string agent: target.currentIndex === 1 ? "claude" : "codex"
  readonly property var catalog: {
    if (!root.project || root.revision < 0) return []
    return backend.skills(root.agent)
  }
  readonly property var matches: {
    const query = search.text.trim().toLowerCase()
    return root.catalog.filter(row => (row.name + " " + row.description).toLowerCase().includes(query))
  }

  onProjectChanged: {
    editor.close()
    search.clear()
  }
  Connections {
    target: backend
    function onSkillsChanged() { root.revision++ }
    function onSkillImported(markdown, origin, error) {
      if (!editor.visible || !editor.waiting || editor.mode !== "import") return
      editor.waiting = false
      editor.problem = error
      if (markdown) {
        editor.origin = origin
        preview.text = markdown
        editor.reviewing = true
      }
    }
    function onSkillBuilt(question, markdown, error) {
      if (!editor.visible || !editor.waiting || editor.mode !== "build") return
      editor.waiting = false
      editor.problem = error
      if (question || markdown) editor.turns = editor.pendingTurns
      if (question) {
        editor.question = question
        answer.clear()
      }
      if (markdown) {
        preview.text = markdown
        editor.reviewing = true
      }
    }
  }

  ColumnLayout {
    width: root.availableWidth
    spacing: 16
    Body { visible: !root.project; text: qsTr("Connect a project to manage its skills.") }
    Card {
      visible: !!root.project
      heading: qsTr("Project skills")
      Body { text: qsTr("Enable a skill to make it available to the selected coding agent in this project.") }
      RowLayout {
        Layout.fillWidth: true
        ComboBox { id: target; objectName: "skillAgent"; model: ["Codex", "Claude Code"]; Accessible.name: qsTr("Coding agent") }
        Item { Layout.fillWidth: true }
        Button { objectName: "importSkill"; text: qsTr("Import link"); onClicked: editor.begin("import") }
        Button { objectName: "buildSkill"; text: qsTr("Build a skill"); onClicked: editor.begin("build") }
      }
      TextField {
        id: search
        objectName: "skillSearch"
        Layout.fillWidth: true
        placeholderText: qsTr("Filter skills")
        Accessible.name: placeholderText
      }
    }

    Repeater {
      model: root.project ? [{title: qsTr("Your skills"), bundled: false}, {title: qsTr("AI Hero defaults"), bundled: true}] : []
      delegate: Card {
        id: group
        required property var modelData
        readonly property var entries: root.matches.filter(row => row.bundled === modelData.bundled)
        visible: modelData.bundled || entries.length > 0
        heading: modelData.title
        Body { visible: group.entries.length === 0; text: qsTr("No matching skills.") }
        Repeater {
          model: group.entries
          delegate: ColumnLayout {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            Line { visible: parent.index > 0 }
            RowLayout {
              Layout.fillWidth: true
              ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 180
                spacing: 4
                Label {
                  text: modelData.name
                  color: theme.ink
                  font.bold: true
                  elide: Text.ElideRight
                  Layout.fillWidth: true
                }
                Body {
                  text: modelData.description
                  textFormat: Text.PlainText
                  wrapMode: Text.Wrap
                  Layout.minimumWidth: 0
                }
                Body {
                  visible: !!text
                  text: [modelData.created ? qsTr("Added %1").arg(Timestamp.format(modelData.created)) : "",
                         modelData.enabledAt ? qsTr("Enabled %1").arg(Timestamp.format(modelData.enabledAt)) : ""].filter(Boolean).join(" · ")
                  font.pixelSize: 11
                }
              }
              Button {
                text: qsTr("View")
                Accessible.name: qsTr("View %1").arg(modelData.name)
                onClicked: editor.begin("view", modelData)
              }
              Button {
                objectName: "skillToggle-" + modelData.name
                text: !modelData.installed ? qsTr("Enable") : modelData.managed ? qsTr("Disable") : qsTr("Installed")
                Accessible.name: text + " " + modelData.name
                enabled: !modelData.installed || modelData.managed
                onClicked: backend.setSkillEnabled(modelData.name, root.agent, !modelData.installed)
              }
            }
          }
        }
      }
    }
  }

  Dialog {
    id: editor
    objectName: "skillEditor"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(680, parent ? parent.width - 32 : 680)
    modal: true
    closePolicy: waiting ? Popup.NoAutoClose : Popup.CloseOnEscape
    title: mode === "view" ? skillName : mode === "import" ? qsTr("Import a skill") : qsTr("Build a skill")
    property string mode: ""
    property string skillName: ""
    property string origin: ""
    property string problem: ""
    property string question: ""
    property bool waiting: false
    property bool reviewing: false
    property var turns: []
    property var pendingTurns: []

    function begin(kind, item) {
      mode = kind
      skillName = item ? item.name : ""
      origin = item ? item.origin : ""
      problem = ""
      question = ""
      waiting = false
      reviewing = kind === "view"
      turns = []
      pendingTurns = []
      link.clear()
      goal.clear()
      answer.clear()
      preview.text = item ? backend.skillText(item.name) : ""
      open()
    }

    function next(draftNow) {
      pendingTurns = question && answer.text.trim()
        ? turns.concat([{question: question, answer: answer.text.trim()}]) : turns
      waiting = true
      problem = ""
      backend.buildSkill(goal.text, pendingTurns, draftNow)
    }

    contentItem: ColumnLayout {
      spacing: 10
      Body {
        visible: editor.mode === "import" && !editor.reviewing
        text: qsTr("Paste a direct HTTPS link to a SKILL.md file or its GitHub file page.")
      }
      TextField {
        id: link
        objectName: "skillImportUrl"
        visible: editor.mode === "import" && !editor.reviewing
        enabled: !editor.waiting
        Layout.fillWidth: true
        placeholderText: "https://"
        Accessible.name: qsTr("Skill file URL")
      }
      Body {
        visible: editor.mode === "build" && !editor.reviewing && !editor.question
        text: qsTr("Describe a workflow you want an agent to repeat. The builder uses your Ask & search model.")
      }
      ScrollView {
        visible: editor.mode === "build" && !editor.reviewing && !editor.question
        Layout.fillWidth: true
        Layout.preferredHeight: 116
        contentWidth: availableWidth
        TextArea {
          id: goal
          objectName: "skillGoal"
          enabled: !editor.waiting && !editor.question
          placeholderText: qsTr("What should this skill help you do?")
          wrapMode: TextEdit.Wrap
          Accessible.name: placeholderText
        }
      }
      Body { visible: editor.mode === "build" && !editor.reviewing && !!editor.question; text: editor.question; textFormat: Text.PlainText }
      ScrollView {
        visible: editor.mode === "build" && !editor.reviewing && !!editor.question
        Layout.fillWidth: true
        Layout.preferredHeight: 116
        contentWidth: availableWidth
        TextArea {
          id: answer
          objectName: "skillAnswer"
          enabled: !editor.waiting
          placeholderText: qsTr("Your answer")
          wrapMode: TextEdit.Wrap
          Accessible.name: placeholderText
        }
      }
      Body {
        visible: editor.reviewing && editor.mode !== "view"
        text: qsTr("Review or edit the skill before saving it to this project's catalog.")
      }
      ScrollView {
        visible: editor.reviewing
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(320, editor.parent ? editor.parent.height * 0.45 : 320)
        contentWidth: availableWidth
        TextArea {
          id: preview
          objectName: "skillMarkdown"
          readOnly: editor.mode === "view"
          wrapMode: TextEdit.Wrap
          textFormat: TextEdit.PlainText
          Accessible.name: qsTr("Skill Markdown")
        }
      }
      Body { visible: editor.waiting; text: editor.mode === "import" ? qsTr("Downloading skill…") : qsTr("Waiting for the model…") }
      Body { visible: !!editor.problem; text: editor.problem; color: theme.warning; textFormat: Text.PlainText }
      RowLayout {
        Layout.fillWidth: true
        Button { visible: !!editor.origin; text: qsTr("Open source"); onClicked: Qt.openUrlExternally(editor.origin) }
        Item { Layout.fillWidth: true }
        Button { visible: !editor.waiting; text: qsTr("Close"); onClicked: editor.close() }
        Button {
          visible: editor.waiting && editor.mode === "build"
          text: qsTr("Stop")
          onClicked: backend.cancel()
        }
        Button {
          visible: editor.mode === "import" && !editor.reviewing
          enabled: !editor.waiting && !!link.text.trim()
          text: qsTr("Preview")
          onClicked: {
            editor.waiting = true
            editor.problem = ""
            backend.importSkill(link.text)
          }
        }
        Button {
          visible: editor.mode === "build" && !editor.reviewing && !!editor.question
          enabled: !editor.waiting
          text: qsTr("Draft now")
          onClicked: editor.next(true)
        }
        Button {
          objectName: "skillNext"
          visible: editor.mode === "build" && !editor.reviewing
          enabled: !editor.waiting && (editor.question ? !!answer.text.trim() : !!goal.text.trim())
          text: editor.question ? qsTr("Continue") : qsTr("Start interview")
          onClicked: editor.next(false)
        }
        Button {
          objectName: "saveSkill"
          visible: editor.mode !== "view" && editor.reviewing
          enabled: !!preview.text.trim()
          text: qsTr("Save to catalog")
          onClicked: {
            if (backend.saveSkill(preview.text, editor.origin)) editor.close()
            else editor.problem = backend.status
          }
        }
      }
    }
  }
}
