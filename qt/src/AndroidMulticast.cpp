#include "AndroidMulticast.h"

#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QCoreApplication>
#include <QDebug>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
QJniObject g_multicastLock;   // se mantiene vivo mientras el lock esté adquirido
QJniObject g_wifiLock;        // WiFi activo con pantalla apagada (evita ahorro de energía)
QJniObject g_wakeLock;        // CPU despierta (timers de descubrimiento / transferencias)

QJniObject androidContext()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    return QNativeInterface::QAndroidApplication::context();
#else
    return QNativeInterface::QAndroidApplication::context();
#endif
}
}

namespace AndroidMulticast {

void acquire()
{
    if (g_multicastLock.isValid())
        return;

    QJniObject context = androidContext();
    if (!context.isValid()) {
        qWarning() << "[AndroidMulticast] contexto no válido";
        return;
    }

    QJniObject serviceName = QJniObject::fromString(QStringLiteral("wifi"));
    QJniObject wifiManager = context.callObjectMethod(
        "getSystemService",
        "(Ljava/lang/String;)Ljava/lang/Object;",
        serviceName.object<jstring>());
    if (!wifiManager.isValid()) {
        qWarning() << "[AndroidMulticast] WifiManager no disponible";
        return;
    }

    QJniObject tag = QJniObject::fromString(QStringLiteral("vorlan"));
    g_multicastLock = wifiManager.callObjectMethod(
        "createMulticastLock",
        "(Ljava/lang/String;)Landroid/net/wifi/WifiManager$MulticastLock;",
        tag.object<jstring>());
    if (!g_multicastLock.isValid()) {
        qWarning() << "[AndroidMulticast] no se pudo crear el MulticastLock";
        return;
    }
    g_multicastLock.callMethod<void>("setReferenceCounted", "(Z)V", jboolean(false));
    g_multicastLock.callMethod<void>("acquire");
    qInfo() << "[AndroidMulticast] MulticastLock adquirido";

    // WifiLock FULL_HIGH_PERF (=3): mantiene el WiFi a plena potencia con la pantalla
    // apagada, evitando que el ahorro de energía corte multicast/UDP (el dispositivo
    // dejaba de verse al apagar la pantalla).
    if (!g_wifiLock.isValid()) {
        const jint WIFI_MODE_FULL_HIGH_PERF = 3;
        g_wifiLock = wifiManager.callObjectMethod(
            "createWifiLock",
            "(ILjava/lang/String;)Landroid/net/wifi/WifiManager$WifiLock;",
            WIFI_MODE_FULL_HIGH_PERF, tag.object<jstring>());
        if (g_wifiLock.isValid()) {
            g_wifiLock.callMethod<void>("setReferenceCounted", "(Z)V", jboolean(false));
            g_wifiLock.callMethod<void>("acquire");
            qInfo() << "[AndroidMulticast] WifiLock adquirido";
        }
    }

    // WakeLock parcial: la CPU sigue corriendo con la pantalla apagada para que los
    // anuncios de descubrimiento y las transferencias en curso no se detengan.
    if (!g_wakeLock.isValid()) {
        QJniObject powerName = QJniObject::fromString(QStringLiteral("power"));
        QJniObject pm = context.callObjectMethod(
            "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;",
            powerName.object<jstring>());
        if (pm.isValid()) {
            const jint PARTIAL_WAKE_LOCK = 1;
            QJniObject wlTag = QJniObject::fromString(QStringLiteral("vorlan:net"));
            g_wakeLock = pm.callObjectMethod(
                "newWakeLock", "(ILjava/lang/String;)Landroid/os/PowerManager$WakeLock;",
                PARTIAL_WAKE_LOCK, wlTag.object<jstring>());
            if (g_wakeLock.isValid()) {
                g_wakeLock.callMethod<void>("setReferenceCounted", "(Z)V", jboolean(false));
                g_wakeLock.callMethod<void>("acquire");
                qInfo() << "[AndroidMulticast] WakeLock parcial adquirido";
            }
        }
    }
}

void release()
{
    if (g_wakeLock.isValid()) {
        if (g_wakeLock.callMethod<jboolean>("isHeld"))
            g_wakeLock.callMethod<void>("release");
        g_wakeLock = QJniObject();
    }
    if (g_wifiLock.isValid()) {
        if (g_wifiLock.callMethod<jboolean>("isHeld"))
            g_wifiLock.callMethod<void>("release");
        g_wifiLock = QJniObject();
    }
    if (!g_multicastLock.isValid())
        return;
    if (g_multicastLock.callMethod<jboolean>("isHeld"))
        g_multicastLock.callMethod<void>("release");
    g_multicastLock = QJniObject();
}

} // namespace AndroidMulticast

#else  // no-Android: no-ops

namespace AndroidMulticast {
void acquire() {}
void release() {}
}

#endif
