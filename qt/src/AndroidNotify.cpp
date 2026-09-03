#include "AndroidNotify.h"

#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QJniObject>
#include <QJniEnvironment>
#include <QCoreApplication>
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
constexpr const char *kService = "net/vorlan/TransferService";

QString titleFor(bool sending)
{
    return sending ? QStringLiteral("⬆ Enviando") : QStringLiteral("⬇ Recibiendo");
}
}

namespace AndroidNotify {

void requestPermission()
{
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 33)
        return;   // POST_NOTIFICATIONS solo existe en Android 13+
    QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid())
        return;
    QJniEnvironment env;
    jclass stringClass = env->FindClass("java/lang/String");
    QJniObject perm = QJniObject::fromString(
        QStringLiteral("android.permission.POST_NOTIFICATIONS"));
    jobjectArray arr = env->NewObjectArray(1, stringClass, perm.object<jstring>());
    activity.callMethod<void>("requestPermissions", "([Ljava/lang/String;I)V", arr, 0);
    env->DeleteLocalRef(arr);
}

void start(bool sending, const QString &text)
{
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        kService, "startService",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        ctx.object(),
        QJniObject::fromString(titleFor(sending)).object<jstring>(),
        QJniObject::fromString(text).object<jstring>());
}

void startIdle()
{
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        kService, "startIdle", "(Landroid/content/Context;)V", ctx.object());
}

void update(bool sending, const QString &text, int percent)
{
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        kService, "updateProgress",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;I)V",
        ctx.object(),
        QJniObject::fromString(titleFor(sending)).object<jstring>(),
        QJniObject::fromString(text).object<jstring>(),
        percent);
}

void stop()
{
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        kService, "stopService", "(Landroid/content/Context;)V", ctx.object());
}

void result(const QString &title, const QString &text)
{
    QJniObject ctx = QNativeInterface::QAndroidApplication::context();
    if (!ctx.isValid())
        return;
    QJniObject::callStaticMethod<void>(
        kService, "showResult",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        ctx.object(),
        QJniObject::fromString(title).object<jstring>(),
        QJniObject::fromString(text).object<jstring>());
}

} // namespace AndroidNotify

#else  // ---- no-Android: no-ops ----

namespace AndroidNotify {
void requestPermission() {}
void start(bool, const QString &) {}
void startIdle() {}
void update(bool, const QString &, int) {}
void stop() {}
void result(const QString &, const QString &) {}
}

#endif
