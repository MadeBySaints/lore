import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The project wiki: facts, decisions, questions, and next steps, with checks.
ColumnLayout {
  id: root
  objectName: "wikiPage"
  signal read(string title, string text)
  signal navigate(string page)
  spacing: 12
  readonly property bool ready: backend.project.length > 0
  readonly property var progress: backend.verifyProgress || ({})
  readonly property bool canDelete: root.ready && backend.memoryModel.total > 0 && !backend.busy
    && !backend.capturing && !backend.buildProgress.active && !root.progress.active
  Card {
    id: skippedCard
    readonly property var sources: backend.stats.skippedLearning || []
    visible: sources.length > 0
    spacing: 8
    padding: 14
    Label { text: qsTr("Skipped during wiki learning"); color: theme.ink; font.bold: true }
    RowLayout {
      Layout.fillWidth: true
      ComboBox {
        id: skippedSource
        objectName: "skippedLearningSource"
        Layout.fillWidth: true
        model: skippedCard.sources.map(function(source) {
          return { source: source.source, label: qsTr("%1 · %n message(s)", "", source.count).arg(source.title) }
        })
        textRole: "label"
        valueRole: "source"
      }
      Button {
        objectName: "retrySkippedLearning"
        text: qsTr("Queue for next build")
        enabled: skippedSource.currentIndex >= 0 && !backend.busy && !backend.capturing && !backend.buildProgress.active
        onClicked: backend.retrySkippedLearning(skippedSource.currentValue)
      }
    }
  }
  Card {
    objectName: "verifyAllRow"
    visible: !backend.personalWiki
    spacing: 10
    padding: 14
    RowLayout {
      Layout.fillWidth: true
      ColumnLayout {
        Layout.fillWidth: true
        spacing: 3
        Label { text: qsTr("Verify wiki entries"); color: theme.ink; font.pixelSize: 16; font.bold: true }
        Body {
          text: root.progress.active ? qsTr("Checking entries against project files and newer evidence.")
            : backend.reviewWaiting > 0 ? qsTr("%n entries awaiting agent review", "", backend.reviewWaiting)
            : qsTr("No entries awaiting agent review")
          font.pixelSize: 12
        }
      }
      PrimaryButton {
        objectName: "agentReviewButton"
        visible: !root.progress.active
        text: qsTr("Agent review…")
        enabled: root.ready && !backend.modelBusy
        onClicked: agentReview.open()
      }
      Button { objectName: "verifyAllStop"; visible: !!root.progress.active; text: qsTr("Stop"); onClicked: backend.cancel() }
    }
    ColumnLayout {
      visible: !!root.progress.active
      Layout.fillWidth: true
      spacing: 4
      Label {
        objectName: "verifyAllText"
        text: qsTr("Checked %1 of %2").arg((root.progress.done || 0) + (root.progress.failed || 0)).arg(root.progress.total || 0)
          + ((root.progress.failed || 0) > 0 ? " · " + qsTr("%1 could not be checked").arg(root.progress.failed) : "")
        color: theme.muted
        font.pixelSize: 12
      }
      ProgressBar {
        Layout.fillWidth: true
        from: 0
        to: Math.max(1, root.progress.total || 0)
        value: (root.progress.done || 0) + (root.progress.failed || 0)
      }
    }
  }
  Dialog {
    id: agentReview
    objectName: "agentReviewDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(510, parent.width - 36)
    modal: true
    title: qsTr("Agent review")
    property string provider: ""
    readonly property var providers: ["claude", "codex", "local"]
    readonly property bool providerReady: {
      const revision = backend.modelRevision
      const entries = backend.tools.entries
      return backend.modelReady("verify", provider)
    }
    readonly property bool canStart: root.ready && backend.reviewWaiting > 0
      && !backend.modelBusy && !root.progress.active && providerReady
    onAboutToShow: provider = backend.verifyProvider
    onOpened: reviewAgent.forceActiveFocus()
    contentItem: ColumnLayout {
      spacing: 10
      Body { text: qsTr("Review %n queued wiki entries against project files and newer evidence.", "", backend.reviewWaiting) }
      GridLayout {
        Layout.fillWidth: true
        columns: 2
        columnSpacing: 10
        Label { text: qsTr("Agent"); color: theme.muted }
        ModelSelector {
          id: reviewAgent
          objectName: "verifyProviderBox"
          Layout.fillWidth: true
          Accessible.name: qsTr("Review agent")
          model: ["Claude Code", "Codex", qsTr("Local model")]
          currentIndex: agentReview.providers.indexOf(agentReview.provider)
          onActivated: function(index) { agentReview.provider = agentReview.providers[index] }
        }
      }
      ModelOptions { task: "verify"; provider: agentReview.provider }
      PromptPresetBox { objectName: "verifyPresetBox"; kind: "verify"; Layout.fillWidth: true }
      Body {
        visible: !agentReview.providerReady
        text: qsTr("Choose a ready agent or set up this agent in AI settings.")
      }
    }
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
      const start = standardButton(Dialog.Ok)
      start.objectName = "startAgentReview"
      start.text = qsTr("Start review")
      start.enabled = Qt.binding(function() { return agentReview.canStart })
    }
    onAccepted: {
      if (!canStart) return
      backend.verifyProvider = provider
      backend.verifyAll()
    }
  }
  Dialog {
    id: confirmDelete
    objectName: "deleteWikiDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(480, parent.width - 36)
    modal: true
    title: qsTr("Delete the compiled wiki?")
    height: 230
    contentItem: Item {
      Body {
        width: confirmDelete.width - confirmDelete.leftPadding - confirmDelete.rightPadding
        text: qsTr("This permanently deletes all wiki entries, corrections, and verification results. Saved conversations and the project index will remain. Future wiki builds will start with new conversations.")
      }
    }
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
      const deleteButton = standardButton(Dialog.Ok)
      deleteButton.objectName = "confirmDeleteWiki"
      deleteButton.text = qsTr("Delete wiki")
      deleteButton.palette.buttonText = theme.warning
      deleteButton.enabled = Qt.binding(function() { return root.canDelete })
      standardButton(Dialog.Cancel).objectName = "cancelDeleteWiki"
    }
    onOpened: standardButton(Dialog.Cancel).forceActiveFocus()
    onAccepted: backend.deleteWiki()
  }
  RowLayout { Layout.fillWidth: true; spacing: 10
    CheckBox { objectName: "wikiAttentionOnly"; text: qsTr("Needs attention only"); checked: backend.memoryModel.attentionOnly; onToggled: backend.memoryModel.attentionOnly = checked }
    TextField { objectName: "wikiSearch"; Layout.fillWidth: true; placeholderText: qsTr("Search the wiki: titles, text, kinds, or file names"); selectByMouse: true; text: backend.memoryModel.filter; onTextChanged: backend.memoryModel.filter = text }
    Button {
      objectName: "deleteWikiButton"
      text: qsTr("Delete compiled wiki…")
      flat: true
      palette.buttonText: theme.warning
      enabled: root.canDelete
      onClicked: confirmDelete.open()
    }
    Label { objectName: "wikiCount"; visible: backend.memoryModel.total > 0; text: backend.memoryModel.count < backend.memoryModel.total || backend.memoryModel.filter.trim().length > 0 ? qsTr("%1 of %2").arg(backend.memoryModel.count).arg(backend.memoryModel.total) : qsTr("%1 entries").arg(backend.memoryModel.total); color: theme.muted; font.pixelSize: 12 }
  }
  Label {
    objectName: "hiddenWikiEntries"
    Layout.fillWidth: true
    visible: backend.hiddenWikiEntries > 0
    text: backend.hiddenWikiEntries === 1
      ? qsTr("One wiki entry is hidden because it cites a forgotten conversation.")
      : qsTr("%1 wiki entries are hidden because they cite forgotten conversations.").arg(backend.hiddenWikiEntries)
    color: theme.muted
    wrapMode: Text.Wrap
  }
  ListView { objectName: "wikiList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: backend.memoryModel
    ScrollBar.vertical: ScrollBar { objectName: "wikiScrollBar"; policy: ScrollBar.AsNeeded }
    delegate: MemoryCard { required property var modelData; entry: modelData; width: ListView.view.width; height: implicitHeight; onRead: function(title, text) { root.read(title, text) } }
    ColumnLayout {
      anchors.centerIn: parent
      width: Math.min(420, parent.width - 32)
      visible: backend.memoryModel.count === 0
      Label {
        objectName: "wikiEmptyState"
        Layout.fillWidth: true
        text: backend.memoryModel.total === 0
          ? (backend.hiddenWikiEntries > 0 ? qsTr("Capture or save a new conversation, then choose Build wiki.") : qsTr("Capture a conversation, then choose Build wiki."))
          : backend.memoryModel.filter.trim().length > 0 ? qsTr("No wiki entries match your search.") : qsTr("No wiki entries need your attention.")
        color: theme.muted
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
      }
      Button {
        Layout.alignment: Qt.AlignHCenter
        visible: backend.memoryModel.total > 0 && backend.memoryModel.attentionOnly
        text: qsTr("Show all entries")
        onClicked: backend.memoryModel.attentionOnly = false
      }
      Button {
        Layout.alignment: Qt.AlignHCenter
        visible: backend.memoryModel.total === 0
        text: qsTr("Open sources")
        onClicked: root.navigate("sources")
      }
    }
  }
}
