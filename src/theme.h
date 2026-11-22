#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // The app's fixed palette, shared by every page instead of repeated hex values.
    Q_PROPERTY(QString background MEMBER m_background CONSTANT)
    Q_PROPERTY(QString panel MEMBER m_panel CONSTANT)
    Q_PROPERTY(QString card MEMBER m_card CONSTANT)
    Q_PROPERTY(QString inset MEMBER m_inset CONSTANT)
    Q_PROPERTY(QString well MEMBER m_well CONSTANT)
    Q_PROPERTY(QString edge MEMBER m_edge CONSTANT)
    Q_PROPERTY(QString hover MEMBER m_hover CONSTANT)
    Q_PROPERTY(QString selected MEMBER m_selected CONSTANT)
    Q_PROPERTY(QString ink MEMBER m_ink CONSTANT)
    Q_PROPERTY(QString soft MEMBER m_soft CONSTANT)
    Q_PROPERTY(QString muted MEMBER m_muted CONSTANT)
    Q_PROPERTY(QString warning MEMBER m_warning CONSTANT)
    Q_PROPERTY(QString caution MEMBER m_caution CONSTANT)
    Q_PROPERTY(QString disabled MEMBER m_disabled CONSTANT)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }

    static QString readAccent(const QString &path);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_background = "#0d0b14", m_panel = "#15121f", m_card = "#1c1829", m_inset = "#241f35", m_well = "#0d0b14";
    QString m_edge = "#342e4a", m_hover = "#241f35", m_selected = "#2d2742";
    QString m_ink = "#ece6f7", m_soft = "#c8bfd9", m_muted = "#9a91b3", m_warning = "#ff9aa4", m_caution = "#ffb53a", m_disabled = "#342e4a";
    QString m_directory;
    QString m_accent = "#f2c35b";
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 14;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
