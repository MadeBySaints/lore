import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
  id: root
  objectName: "functionIndexPage"
  signal read(string title, string text)
  signal navigate(string page)
  signal showScanIssues()
  clip: true
  contentWidth: availableWidth
  readonly property var job: backend.knowledgeProgress
  readonly property bool scanning: job.kind === "functions"

  ColumnLayout {
    width: root.availableWidth
    spacing: 12
    Card {
      heading: qsTr("Function index")
      Body { text: qsTr("Document functions, methods, and named React components. The selected model locates calls and references to state, files, settings, and other dependencies in indexed source.") }
      Body { text: qsTr("Supported files include C/C++, QML, JavaScript and TypeScript, Python, GDScript, Rust, Go, and Ruby and Rails.") }
      KnowledgeModelOptions { id: modelChoice; task: "functions"; enabled: !backend.busy }
      Body {
        visible: modelChoice.provider !== "local"
        text: qsTr("The selected provider will receive excerpts of your source files.")
      }
      CheckBox { id: rerun; text: qsTr("Rescan unchanged files"); enabled: !backend.busy }
      RowLayout {
        Layout.fillWidth: true
        PrimaryButton {
          objectName: "scanFunctionsButton"
          text: rerun.checked ? qsTr("Rescan all source files") : qsTr("Scan project functions")
          enabled: modelChoice.ready && !backend.busy && !backend.capturing
          onClicked: backend.startFunctionIndex(modelChoice.provider, rerun.checked)
        }
        Button { visible: root.scanning; text: qsTr("Stop"); onClicked: backend.cancel() }
        Button { visible: root.scanning; text: qsTr("Open Terminal"); onClicked: root.navigate("output") }
        Button { visible: !root.scanning && backend.functions.length > 0; text: qsTr("Open function wiki"); onClicked: root.navigate("functionWiki") }
        Button {
          text: qsTr("Open Markdown index")
          visible: backend.functions.length > 0
          onClicked: root.read(qsTr("Function index"), backend.readPage(".lore/functions/index.md"))
        }
      }
    }
    Card {
      heading: qsTr("Coverage")
      Body { text: qsTr("Source folder") + "\n" + backend.source; textFormat: Text.PlainText }
      Body {
        readonly property int stale: backend.functions.filter(function(entry) { return entry.stale }).length
        text: qsTr("%1 of %2 eligible source files scanned · %3 documented functions")
          .arg(backend.indexedFunctionFiles).arg(backend.eligibleFunctionFiles).arg(backend.functions.length - stale)
          + (stale ? qsTr(" · %1 outdated functions").arg(stale) : "")
      }
      Body {
        visible: root.scanning
        text: root.job.current || qsTr("Preparing the file list…")
      }
      ProgressBar {
        visible: root.scanning
        Layout.fillWidth: true
        indeterminate: !root.job.total
        to: Math.max(1, root.job.total || 0)
        value: (root.job.done || 0) + (root.job.failed || 0)
      }
      Body {
        visible: root.scanning && !!root.job.total
        text: qsTr("%1 of %2 files completed").arg(root.job.done).arg(root.job.total)
          + (root.job.failed ? qsTr(" · %1 skipped").arg(root.job.failed) : "")
      }
      Button {
        objectName: "viewFunctionScanIssues"
        visible: backend.liveIssues.some(function(issue) { return issue.kind === "skippedFile" })
        text: qsTr("View scan issues")
        onClicked: root.showScanIssues()
      }
      Body { text: qsTr("A scan picks up new files, changed files, and earlier results that still need dependency documentation. Finished files are saved.") }
    }
  }
}
