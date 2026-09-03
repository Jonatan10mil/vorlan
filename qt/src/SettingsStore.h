#pragma once

#include <QSettings>
#include <QString>
#include <QUrl>

// Único punto de acceso a la configuración persistida (QSettings).
// Centraliza las claves y los valores por defecto para que no queden
// dispersos por TransferManager (SRP: responsabilidad = persistencia).
class SettingsStore
{
public:
    // --- Lectura (con valor por defecto) ---
    bool autoAccept() const           { return get("autoAccept", false).toBool(); }
    QString downloadDir() const       {
        QString v = get("downloadDir").toString();
        // Normaliza valores viejos guardados como "file:///..." (desktop)
        // Android usa "content://", ese se deja tal cual.
        if (v.startsWith(QLatin1String("file://"))) {
            const QString local = QUrl(v).toLocalFile();
            if (!local.isEmpty())
                v = local;
        }
        return v;
    }
    QString themeMode() const         { return get("themeMode", "system").toString(); }  // "system"=seguir al SO
    QString accentColor() const       { return get("accentColor", "#57a63a").toString(); }
    QString webPin() const            { return get("webPin").toString(); }
    bool webPinEnabled() const        { return get("webPinEnabled", !webPin().isEmpty()).toBool(); }
    bool showFileNames() const        { return get("showFileNames", true).toBool(); }
    bool closeToTray() const          { return get("closeToTray", false).toBool(); }
    bool notificationsEnabled() const { return get("notificationsEnabled", true).toBool(); }
    bool discoverable() const         { return get("discoverable", true).toBool(); }
    QString language() const          { return get("language", "").toString(); }   // ""=sistema
    bool encrypt() const              { return get("encrypt", false).toBool(); }   // cifrar envíos (TLS)
    bool webTls() const               { return get("webTls", false).toBool(); }    // HTTPS en modo web
    QString avatarImage() const       { return get("avatarImage").toString(); }
    QString deviceName() const        { return get("deviceName").toString(); }

    // --- Escritura ---
    void setAutoAccept(bool v)           { set("autoAccept", v); }
    void setDownloadDir(const QString &v){ set("downloadDir", v); }
    void setThemeMode(const QString &v)  { set("themeMode", v); }
    void setAccentColor(const QString &v){ set("accentColor", v); }
    void setWebPin(const QString &v)     { set("webPin", v); }
    void setWebPinEnabled(bool v)        { set("webPinEnabled", v); }
    void setShowFileNames(bool v)        { set("showFileNames", v); }
    void setCloseToTray(bool v)          { set("closeToTray", v); }
    void setNotificationsEnabled(bool v) { set("notificationsEnabled", v); }
    void setDiscoverable(bool v)         { set("discoverable", v); }
    void setLanguage(const QString &v)   { set("language", v); }
    void setEncrypt(bool v)              { set("encrypt", v); }
    void setWebTls(bool v)               { set("webTls", v); }
    void setAvatarImage(const QString &v){ set("avatarImage", v); }
    void setDeviceName(const QString &v) { set("deviceName", v); }

    void removeAvatarImage()             { settings().remove("avatarImage"); }

private:
    // QSettings cacheado por hilo (evita abrir el backend en cada get/set)
    static QSettings &settings()
    {
        thread_local QSettings s;
        return s;
    }
    static QVariant get(const char *key, const QVariant &def = QVariant())
    { return settings().value(QLatin1String(key), def); }
    static void set(const char *key, const QVariant &value)
    { settings().setValue(QLatin1String(key), value); settings().sync(); }
};
