.pragma library

function format(value) {
  if (!value) return ""
  const date = new Date(value)
  return isNaN(date.getTime()) ? "" : Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss")
}

function friendly(value, today, yesterday) {
  if (!value) return ""
  const date = new Date(value)
  if (isNaN(date.getTime())) return ""
  const current = new Date()
  const previous = new Date(current.getFullYear(), current.getMonth(), current.getDate() - 1)
  const day = Qt.formatDate(date, "yyyy-MM-dd")
  const time = Qt.formatTime(date, "h:mm AP")
  if (day === Qt.formatDate(current, "yyyy-MM-dd")) return today.arg(time)
  if (day === Qt.formatDate(previous, "yyyy-MM-dd")) return yesterday.arg(time)
  return Qt.formatDateTime(date, "ddd, MMM d, yyyy · h:mm AP")
}
