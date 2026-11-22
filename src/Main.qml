import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
  id: win
  width: 1240; height: 820
  minimumWidth: 900; minimumHeight: 620
  visible: true
  title: qsTr("Lore · Project memory")
  color: theme.background
  font.family: Qt.fontFamilies().includes("Inter") ? "Inter" : (Qt.platform.os === "windows" ? "Segoe UI" : "sans-serif")
  Material.theme: Material.Dark
  Material.accent: theme.accent
  Material.roundedScale: theme.radius
  palette.link: theme.accent
  // The page shown, by id. Numbers from older builds (--page) still work.
  property string page: "home"
  property string document: ""
  property string documentTitle: ""
  property string documentArchive: ""
  property var documentHistory: []
  property bool helpVisible: false
  property var visited: ({})
  readonly property var legacy: ({ "0": "home", "1": "wiki", "2": "sources", "3": "ask", "4": "setup", "6": "ask", "7": "sources", "9": "settings", "10": "projects", "11": "output" })
  onPageChanged: {
    if (legacy[page] !== undefined) { page = legacy[page]; return }
    if (!allowed(page)) { page = "wiki"; return }
    if (!visited[page]) { const v = Object.assign({}, visited); v[page] = true; visited = v }
  }
  Component.onCompleted: { const v = {}; v[page] = true; visited = v; if (setup) page = "setup"; else if (!allowed(page)) page = "wiki" }
  readonly property bool personal: backend.personalWiki
  Component { id: personalSettings; PersonalWikiSettings {} }
  Component { id: projectSettings; SettingsPage { onSetupRequested: win.open("setup") } }
  onPersonalChanged: {
    if (!allowed(page)) open("wiki")
    if (personal) { document = ""; helpVisible = false }
  }
  readonly property bool ready: backend.project.length > 0
  // The project browser stays reachable before any project is connected.
  readonly property bool setup: (!backend.connected && page !== "projects") || page === "setup"
  // The guide holds the page while it is up, so connecting moves on to the
  // optional steps instead of closing it.
  onSetupChanged: if (setup && page !== "setup") Qt.callLater(function() { if (win.setup) win.page = "setup" })
  readonly property bool typing: activeFocusItem && (activeFocusItem instanceof TextInput || activeFocusItem instanceof TextEdit)
  readonly property string language: translator.language
  readonly property var pages: ["home", "wiki", "index", "functionIndex", "functionWiki", "transcriptAnalysis", "ask", "sources", "output", "settings", "projects"]
  readonly property var titles: ({ home: qsTr("Resume"), wiki: qsTr("Memory"), index: qsTr("Code"), functionIndex: qsTr("Code"), functionWiki: qsTr("Code"), transcriptAnalysis: qsTr("Memory"), ask: qsTr("Search"), sources: qsTr("Conversations"), output: qsTr("Activity"), settings: qsTr("Settings"), projects: qsTr("Your projects") })
  readonly property bool codePage: ["index", "functionIndex", "functionWiki"].includes(page)
  readonly property bool memoryPage: ["wiki", "transcriptAnalysis"].includes(page)
  readonly property var captions: ({ home: qsTr("PROJECT MEMORY"), wiki: qsTr("KNOWLEDGE"), index: qsTr("KNOWLEDGE"), functionIndex: qsTr("KNOWLEDGE"), functionWiki: qsTr("KNOWLEDGE"), transcriptAnalysis: qsTr("KNOWLEDGE"), ask: qsTr("KNOWLEDGE"), sources: qsTr("KNOWLEDGE"), output: qsTr("ACTIVITY"), settings: qsTr("SETTINGS"), projects: qsTr("PROJECTS") })

  function allowed(id) { return id === "setup" || (pages.includes(id) && (!win.personal || ["wiki", "sources", "output", "settings", "projects"].includes(id))) }
  function open(id) { win.document = ""; win.documentHistory = []; win.page = win.allowed(id) ? id : "wiki" }
  function showDocument(title, text, archive) { documentTitle = title; document = text; documentArchive = archive || "" }
  function documentBack() {
    if (!documentHistory.length) { document = ""; return }
    const previous = documentHistory[documentHistory.length - 1]
    documentHistory = documentHistory.slice(0, -1)
    showDocument(previous.title, previous.text, previous.archive)
    Qt.callLater(function() { documentScroll.contentItem.contentY = previous.offset })
  }
  function providerName(id) { return id === "claude" ? "Claude Code" : id === "codex" ? "Codex" : qsTr("Local model") }
  function toolReady(id, task) { const entries = backend.tools.entries; const revision = backend.modelRevision; return backend.modelReady(task, id) }
  function pageItem(id) { const loader = host.children[pages.indexOf(id)]; return loader ? loader.item : null }
  function follow(link, archive) {
    let path = link.toString()
    let title = path
    if (path.startsWith("../project/")) { title = qsTr("Project file"); path = ".lore/project/" + path.substring(11) }
    else if (path.startsWith("../sources/")) { title = qsTr("Source evidence"); path = ".lore/sources/" + path.substring(11) }
    else if (/^[0-9]+\.md$/.test(path)) { title = qsTr("Wiki page"); path = ".lore/wiki/" + path }
    else if (/^[a-f0-9]+\.md(?:#[a-f0-9]+)?$/.test(path)) { title = qsTr("Conversation"); path = ".lore/sources/" + path }
    if (!path.startsWith(".lore/")) return
    const targetArchive = archive === undefined ? documentArchive : archive
    const text = backend.readPage(path, targetArchive)
    if (!text.length) return
    if (document.length)
      documentHistory = documentHistory.concat([{ title: documentTitle, text: document, archive: documentArchive, offset: documentScroll.contentItem.contentY }])
    showDocument(title, text, targetArchive)
    documentScroll.contentItem.contentY = 0
  }
  Shortcut { sequence: "?"; enabled: !win.typing && !win.personal; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
  Shortcut { sequence: "Q"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: backend.busy ? quitDialog.open() : Qt.quit() }
  Shortcut { sequence: "Space"; enabled: !win.typing && ready; context: Qt.ApplicationShortcut; onActivated: backend.sync() }
  Shortcut { sequence: "Ctrl+K"; enabled: !win.personal; context: Qt.ApplicationShortcut; onActivated: { win.open("ask"); Qt.callLater(function() { const item = win.pageItem("ask"); if (item) item.focusSearch() }) } }
  Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; onActivated: win.open("projects") }
  Shortcut { sequence: "F11"; context: Qt.ApplicationShortcut; onActivated: win.visibility === Window.FullScreen ? win.showNormal() : win.showFullScreen() }
  Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: { win.document = ""; win.helpVisible = false } }
  onClosing: function(close) { if (backend.busy) { close.accepted = false; quitDialog.open() } }

  Item {
    anchors.fill: parent
    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true
    RowLayout {
      anchors.fill: parent; spacing: 0; visible: !win.setup
      // The sidebar: the project, then the pages grouped by what they are for.
      Rectangle {
        objectName: "sidebar"
        Layout.preferredWidth: 212; Layout.fillHeight: true; color: theme.panel
        ColumnLayout {
          anchors.fill: parent; anchors.margins: 12; spacing: 2
          RowLayout { spacing: 10; Layout.leftMargin: 2
            Rectangle { width: 28; height: 32; color: theme.accent; radius: theme.radius
              Label { anchors.centerIn: parent; text: "L"; color: theme.accentForeground; font.bold: true; font.pixelSize: 23 } }
            Label { text: "lore"; color: theme.ink; font.pixelSize: 27 } }
          // The project switcher.
          Rectangle { objectName: "switchProjectButton"; Layout.fillWidth: true; Layout.topMargin: 10; Layout.bottomMargin: 6; implicitHeight: projectColumn.implicitHeight + 16; radius: theme.radius
            color: projectMouse.containsMouse ? theme.hover : theme.card; border.color: win.page === "projects" ? theme.accent : theme.edge
            ColumnLayout { id: projectColumn; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 10; spacing: 1
              Caption { text: win.personal ? qsTr("PERSONAL WIKI") : qsTr("PROJECT") }
              RowLayout { Layout.fillWidth: true
                Label { text: win.ready ? (backend.stats.name || "") : qsTr("Choose a project"); color: theme.ink; font.bold: true; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: "⌄"; color: theme.muted; font.pixelSize: 14 } } }
            MouseArea { id: projectMouse; anchors.fill: parent; hoverEnabled: true; enabled: !backend.selecting; cursorShape: Qt.PointingHandCursor; onClicked: win.open("projects")
              ToolTip.visible: containsMouse; ToolTip.delay: 700; ToolTip.text: qsTr("All projects (Ctrl+O)") } }
          NavItem { visible: !win.personal; pageId: "home"; label: qsTr("Resume"); icon: "home"; current: win.page === "home"; available: win.ready; onActivated: win.open(pageId) }
          Caption { text: qsTr("KNOWLEDGE"); Layout.topMargin: 12; Layout.bottomMargin: 2; Layout.leftMargin: 4 }
          NavItem { pageId: "wiki"; label: qsTr("Memory"); icon: "wiki"; current: win.memoryPage; available: win.ready; badge: win.personal ? 0 : backend.reviewWaiting; onActivated: win.open(pageId) }
          NavItem { visible: !win.personal; pageId: "index"; label: qsTr("Code"); icon: "sources"; current: win.codePage; available: win.ready; onActivated: win.open(pageId) }
          NavItem { visible: !win.personal; pageId: "ask"; label: qsTr("Search"); icon: "ask"; current: win.page === "ask"; available: win.ready; onActivated: win.open(pageId) }
          NavItem { pageId: "sources"; label: qsTr("Conversations"); icon: "sources"; current: win.page === "sources"; available: win.ready; onActivated: win.open(pageId) }
          NavItem {
            pageId: "output"; label: qsTr("Activity"); icon: "output"; current: win.page === pageId
            available: win.ready
            dot: backend.modelBusy || backend.buildProgress.active || backend.verifyProgress.active || backend.knowledgeProgress.active
            onActivated: win.open(pageId)
          }
          Item { Layout.fillHeight: true }
          // Capture and automation state, always in view.
          Rectangle { Layout.fillWidth: true; implicitHeight: stateColumn.implicitHeight + 14; radius: theme.radius; color: "transparent"; border.color: theme.edge
            ColumnLayout { id: stateColumn; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 8; spacing: 3
              RowLayout { spacing: 6
                Rectangle { width: 7; height: 7; radius: 4; color: backend.watching ? theme.accent : theme.muted }
                Label { text: backend.watching ? qsTr("Capture is on") : qsTr("Capture is paused"); color: theme.soft; font.pixelSize: 11 } }
              Label { Layout.fillWidth: true; elide: Text.ElideRight; color: (backend.countdown || {}).kind ? theme.accent : theme.muted; font.pixelSize: 10
                text: (backend.countdown || {}).kind === "build" ? qsTr("Wiki build in %1 s").arg(backend.countdown.seconds) : (backend.countdown || {}).kind === "verify" ? qsTr("Checks in %1 s").arg(backend.countdown.seconds)
                  : backend.pausedUntil ? qsTr("Automations paused") : backend.autoLearning ? qsTr("Wiki updates automatically") : qsTr("Wiki updates when you choose") } } }
          NavItem { pageId: "settings"; label: qsTr("Settings"); icon: "settings"; current: win.page === "settings"; Layout.topMargin: 6; onActivated: win.open(pageId) }
          NavItem { pageId: "help"; label: qsTr("Help & shortcuts"); icon: "help"; onActivated: win.helpVisible = true }
        }
      }
      Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: theme.edge }
      ColumnLayout {
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
        // The top bar: where you are, and the actions every page shares.
        RowLayout {
          Layout.fillWidth: true; Layout.margins: 24; Layout.bottomMargin: 18; spacing: 10
          ColumnLayout { Layout.fillWidth: true; spacing: 4
            Caption { text: win.captions[win.page] || "" }
            Label { objectName: "pageTitle"; text: win.personal && win.page === "wiki" ? qsTr("Your personal wiki") : win.titles[win.page] || ""; color: theme.ink; font.family: Qt.fontFamilies().includes("Cinzel") ? "Cinzel" : "Georgia"; font.pixelSize: 25; Layout.fillWidth: true; elide: Text.ElideRight } }
          Button { text: win.personal ? qsTr("Refresh archive") : backend.showCapturing ? qsTr("Capturing…") : qsTr("Capture now"); flat: true; enabled: win.ready && !backend.showCapturing && !backend.workBusy; visible: win.page !== "projects"; onClicked: backend.sync()
            ToolTip.visible: hovered; ToolTip.delay: 700; ToolTip.text: qsTr("Look for new saved conversations now (Space)") }
          PromptPresetBox { objectName: "buildPresetBox"; kind: "build"; visible: ["home", "wiki", "sources"].indexOf(win.page) >= 0; enabled: win.ready }
          // Build wiki always uses the model chosen for it; automatic builds use their own.
          PrimaryButton { objectName: "buildWikiButton"; readonly property bool stopping: backend.buildProgress.active || backend.modelBusy || backend.knowledgeProgress.active
            text: stopping ? qsTr("Cancel") : qsTr("Build wiki · %1").arg(win.providerName(backend.learningProvider)); visible: win.page !== "projects"
            enabled: win.ready && (stopping || (backend.stats.pending > 0 && win.toolReady(backend.learningProvider, "build"))); onClicked: stopping ? backend.cancel() : backend.learn(backend.learningProvider)
            ToolTip.visible: hovered && !stopping; ToolTip.delay: 700; ToolTip.text: qsTr("%1 messages waiting to be learned").arg(backend.stats.pending || 0) }
        }
        Line {}
        RowLayout {
          visible: !win.personal && (win.codePage || win.memoryPage) && !win.document.length
          Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; Layout.topMargin: 6
          Repeater {
            model: win.codePage
              ? [{page: "index", label: qsTr("Files")}, {page: "functionWiki", label: qsTr("Symbols")}, {page: "functionIndex", label: qsTr("Scan symbols")}]
              : [{page: "wiki", label: qsTr("Wiki")}, {page: "transcriptAnalysis", label: qsTr("Conversation findings")}]
            delegate: TabButton {
              required property var modelData
              objectName: "section_" + modelData.page
              text: modelData.label
              checked: win.page === modelData.page
              onClicked: win.open(modelData.page)
            }
          }
          Item { Layout.fillWidth: true }
        }
        Rectangle {
          objectName: "buildProgressBar"; visible: !!backend.buildProgress.active
          Layout.fillWidth: true; implicitHeight: progressColumn.implicitHeight + 20; color: theme.inset
          ColumnLayout { id: progressColumn; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 6
            Label { objectName: "buildProgressText"; Layout.fillWidth: true; color: theme.ink; font.pixelSize: 12; elide: Text.ElideRight
              text: (backend.buildProgress.automatic ? qsTr("Automatic wiki build with %1") : qsTr("Building the wiki with %1")).arg(backend.buildProgress.provider || "")
                + " · " + qsTr("%1 of %2 messages").arg((backend.buildProgress.done || 0).toLocaleString(Qt.locale(), 'f', 0)).arg((backend.buildProgress.total || 0).toLocaleString(Qt.locale(), 'f', 0))
                + " · " + qsTr("round %1").arg((backend.buildProgress.rounds || 0) + 1) }
            ProgressBar { objectName: "buildProgressValue"; Layout.fillWidth: true; from: 0; to: Math.max(1, backend.buildProgress.total || 0); value: backend.buildProgress.done || 0 }
          }
        }
        // Pages load the first time they are opened and then stay, so they keep
        // their scroll position and what was typed.
        Item {
          Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0; Layout.margins: 24; Layout.topMargin: 18
          StackLayout {
            id: host; anchors.fill: parent; visible: win.document.length === 0
            currentIndex: Math.max(0, win.pages.indexOf(win.page))
            Loader { active: !!win.visited.home; sourceComponent: HomePage { onRead: function(t, x) { win.showDocument(t, x) } onNavigate: function(p) { win.open(p) } onFollow: function(url) { win.follow(url, "") } } }
            Loader { active: !!win.visited.wiki; sourceComponent: WikiPage { onRead: function(t, x) { win.showDocument(t, x) } onNavigate: function(p) { win.open(p) } } }
            Loader { active: !!win.visited.index; sourceComponent: ProjectPage { onRead: function(t, x) { win.showDocument(t, x) } } }
            Loader { active: !!win.visited.functionIndex; sourceComponent: FunctionIndexPage {
              onRead: function(t, x) { win.showDocument(t, x) }
              onNavigate: function(p) { win.open(p) }
              onShowScanIssues: {
                win.open("output")
                terminalLoader.item.issuesOnly = true
              }
            } }
            Loader { active: !!win.visited.functionWiki; sourceComponent: FunctionWikiPage { onNavigate: function(p) { win.open(p) } onRead: function(t, x) { win.showDocument(t, x) } } }
            Loader { active: !!win.visited.transcriptAnalysis; sourceComponent: TranscriptAnalysisPage { onRead: function(t, x) { win.showDocument(t, x) } } }
            Loader { active: !!win.visited.ask; sourceComponent: AskSearchPage { onRead: function(t, x) { win.showDocument(t, x) } onFollow: function(url, archive) { win.follow(url, archive) } } }
            Loader { active: !!win.visited.sources; sourceComponent: SourcesPage { onRead: function(t, x) { win.showDocument(t, x) } } }
            Loader { id: terminalLoader; active: !!win.visited.output; sourceComponent: ModelOutputPage {} }
            Loader { active: !!win.visited.settings; sourceComponent: win.personal ? personalSettings : projectSettings }
            Loader { active: !!win.visited.projects; sourceComponent: ProjectsPage { onOpened: win.open("home"); onSetUp: win.open("setup") } }
          }
          // A wiki page, conversation, or file opened from any page.
          ColumnLayout {
            objectName: "documentView"; anchors.fill: parent; visible: win.document.length > 0
            RowLayout { Layout.fillWidth: true
              Button { text: qsTr("← Back"); flat: true; onClicked: win.documentBack() }
              Label { text: win.documentTitle; color: theme.muted; Layout.fillWidth: true; elide: Text.ElideRight } }
            ScrollView { id: documentScroll; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
              TextArea { text: win.document.length > 0 ? backend.markdown(win.document, theme.accent) : ""; textFormat: TextEdit.RichText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: theme.ink; font.pixelSize: 14; background: null; onLinkActivated: function(link) { win.follow(link) } } }
          }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: statusText.font.pixelSize + 24; color: theme.panel
          Label { id: statusText; anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: 24
            text: backend.status || qsTr("Choose a project folder to get started."); textFormat: Text.PlainText
            color: theme.muted; font.pixelSize: 11; maximumLineCount: 1; elide: Text.ElideRight
            HoverHandler { id: statusHover }
            ToolTip.visible: statusHover.hovered && statusText.truncated
            ToolTip.text: statusText.text
          }
        }
      }
    }
    // The setup guide covers the window until the project is connected, and
    // whenever it is opened again from Settings.
    Loader {
      anchors.fill: parent; active: win.setup; visible: active
      sourceComponent: SetupWizard { onFinished: win.open(backend.personalWiki ? "wiki" : "home"); onOpenPage: function(p) { win.open(p) } }
    }
    Rectangle { visible: win.helpVisible; anchors.fill: parent; color: "#ed101411"
      MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }
      Rectangle { anchors.centerIn: parent; width: Math.min(560, win.width - 60); height: helpColumn.implicitHeight + 56; color: "#1c271d"; border.color: theme.edge; radius: theme.radius
        MouseArea { anchors.fill: parent }
        Column { id: helpColumn; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 28; spacing: 18
          Row { spacing: 12
            Label { text: qsTr("Help & shortcuts"); color: theme.ink; font.pixelSize: 24 }
            Label { objectName: "appVersion"; anchors.baseline: parent.children[0].baseline; text: qsTr("Lore %1").arg(backend.version || "?"); color: theme.muted; font.pixelSize: 13 } }
          Label { text: qsTr("Resume shows where you left off. Memory holds what Lore learned. Search and Conversations show the evidence behind it."); color: theme.muted; width: parent.width; wrapMode: Text.Wrap }
          Label { text: qsTr("Ctrl+O   Your projects\nCtrl+K   Search\nSpace    Capture now\nF11      Toggle full screen\nEscape   Close page or help\n?        Show this help\nQ        Quit"); color: theme.ink; font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"; font.pixelSize: 13; lineHeight: 1.5 }
          Row { spacing: 8
            PrimaryButton { objectName: "checkUpdatesButton"; text: qsTr("Check for updates"); onClicked: backend.openReleases() }
            Button { objectName: "helpSetupButton"; text: qsTr("Setup guide"); onClicked: { win.helpVisible = false; win.open("setup") } } }
        }
      }
    }
    Rectangle {
      id: notificationToast
      objectName: "notificationToast"
      visible: backend.error.length > 0
      anchors.top: parent.top; anchors.topMargin: 12
      anchors.horizontalCenter: parent.horizontalCenter
      width: Math.min(440, parent.width - 32)
      height: toastRow.implicitHeight + 16
      color: "#242b23"; border.color: "#847358"; radius: theme.radius
      Accessible.role: Accessible.AlertMessage
      Accessible.name: backend.error
      onVisibleChanged: if (visible) toastEnter.restart()

      transform: Translate { id: toastOffset }
      NumberAnimation { id: toastEnter; target: toastOffset; property: "y"; from: -12; to: 0; duration: 160; easing.type: Easing.OutCubic }
      RowLayout {
        id: toastRow
        anchors.fill: parent; anchors.margins: 8; anchors.leftMargin: 14; spacing: 10
        Rectangle { width: 6; height: 6; radius: 3; color: "#d6ad74" }
        Label {
          Layout.fillWidth: true
          text: backend.error; textFormat: Text.PlainText
          color: theme.ink; font.pixelSize: 13; wrapMode: Text.Wrap
          maximumLineCount: 4; elide: Text.ElideRight
        }
        ToolButton {
          objectName: "dismissNotification"
          text: "×"; font.pixelSize: 19
          Accessible.name: qsTr("Dismiss")
          onClicked: backend.clearError()
        }
      }
    }
  }
  Dialog { id: quitDialog; anchors.centerIn: parent; modal: true; title: qsTr("Cancel learning and quit?"); standardButtons: Dialog.Yes | Dialog.No
    Label { text: qsTr("Learning is still running. Captured conversations are already saved."); width: 360; wrapMode: Text.Wrap }
    onAccepted: { backend.cancel(); Qt.quit() }
  }
}
