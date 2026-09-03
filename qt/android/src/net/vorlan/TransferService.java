package net.vorlan;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

// Servicio en primer plano: muestra el progreso de la transferencia en la
// barra de notificaciones y mantiene la app viva con la pantalla apagada.
public class TransferService extends Service {
    public static final String CHANNEL = "vorlan_transfer";
    public static final int NOTIF_ID = 1;
    public static final int RESULT_ID = 2;

    private static volatile String pendingTitle = "VorLAN";
    private static volatile String pendingText = "";
    private static volatile boolean pendingIdle = false;
    // ¿El servicio ya está en primer plano? Evita re-arrancarlo una y otra vez
    // (varios startForegroundService seguidos dejaban comandos sin completar → ANR).
    private static volatile boolean sForeground = false;
    // ¿Hay una transferencia REALMENTE en curso (enviando/recibiendo)? Distinto del
    // modo "idle/listo para recibir". La usa MainActivity para decidir si puede
    // destruirse al pasar a segundo plano (reapertura limpia) o debe seguir viva.
    public static volatile boolean sTransferActive = false;

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null) {
            pendingTitle = intent.getStringExtra("title");
            pendingText = intent.getStringExtra("text");
            pendingIdle = intent.getBooleanExtra("idle", false);
        }
        ensureChannel(this, NotificationManager.IMPORTANCE_LOW);
        try {
            Notification n = buildNotif(this, pendingTitle, pendingText, pendingIdle ? -2 : -1);
            // Android 10+ exige indicar el TIPO de servicio en primer plano. Sin él,
            // en targetSdk 34+ startForeground lanza excepción y el sistema considera
            // que el servicio nunca arrancó → ANR "executing service".
            if (Build.VERSION.SDK_INT >= 29)
                startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC);
            else
                startForeground(NOTIF_ID, n);
            sForeground = true;
        } catch (Exception e) {
            // Android puede rechazar el FGS (restricciones de segundo plano, límites
            // de dataSync…). No debe tumbar la app: limpiar y parar el servicio.
            sForeground = false;
            try {
                if (Build.VERSION.SDK_INT >= 24) stopForeground(Service.STOP_FOREGROUND_REMOVE);
                else stopForeground(true);
            } catch (Exception ignored) {}
            try { stopSelf(); } catch (Exception ignored) {}
        }
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        sForeground = false;
        super.onDestroy();
    }

    // Modo "listo para recibir": notificación fija sin barra de progreso que
    // mantiene vivo el proceso (descubrimiento + TCP) con la app cerrada.
    public static void startIdle(Context ctx) {
        try {
            sTransferActive = false;   // modo reposo: ya no hay transferencia en curso
            pendingTitle = "VorLAN"; pendingText = "Listo para recibir"; pendingIdle = true;
            // Ya en primer plano: basta con refrescar la notificación. Re-arrancar el
            // servicio repetidamente encolaba comandos que no se completaban (ANR).
            if (sForeground) {
                updateProgress(ctx, pendingTitle, pendingText, -2);
                return;
            }
            Intent i = new Intent(ctx, TransferService.class);
            i.putExtra("title", "VorLAN");
            i.putExtra("text", "Listo para recibir");
            i.putExtra("idle", true);
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i);
            else ctx.startService(i);
        } catch (Exception e) {
            // No se pudo (app en segundo plano, etc.): no crashear.
        }
    }

    private static void ensureChannel(Context ctx, int importance) {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager nm =
                (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
            if (nm != null) {
                NotificationChannel ch = new NotificationChannel(CHANNEL, "Transferencias", importance);
                ch.setShowBadge(false);
                nm.createNotificationChannel(ch);
            }
        }
    }

    // Al tocar la notificación: abrir la app en la pestaña "Recibidos".
    static final String ACTION_OPEN_RECEIVED = "net.vorlan.OPEN_RECEIVED";
    private static PendingIntent openReceivedIntent(Context ctx) {
        Intent i = new Intent(ctx, MainActivity.class);
        i.setAction(ACTION_OPEN_RECEIVED);
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) flags |= PendingIntent.FLAG_IMMUTABLE;
        return PendingIntent.getActivity(ctx, 1, i, flags);
    }

    private static int notifIcon(Context ctx) {
        int id = ctx.getResources().getIdentifier("ic_notif", "drawable", ctx.getPackageName());
        return id != 0 ? id : android.R.drawable.stat_sys_upload;
    }

    private static Notification buildNotif(Context ctx, String title, String text, int percent) {
        int icon = notifIcon(ctx);
        Notification.Builder b = (Build.VERSION.SDK_INT >= 26)
            ? new Notification.Builder(ctx, CHANNEL)
            : new Notification.Builder(ctx);
        b.setContentTitle(title != null ? title : "VorLAN")
         .setContentText(text != null ? text : "")
         .setSmallIcon(icon)
         .setColor(0xFF57A63A)
         .setOngoing(true)
         .setOnlyAlertOnce(true)
         .setContentIntent(openReceivedIntent(ctx));
        if (percent >= 0)
            b.setProgress(100, percent, false);      // determinado
        else if (percent == -1)
            b.setProgress(0, 0, true);               // indeterminado (en curso)
        // percent <= -2: sin barra (modo "listo para recibir")
        return b.build();
    }

    // ---- Llamados desde C++/JNI ----

    public static void startService(Context ctx, String title, String text) {
        try {
            sTransferActive = true;    // transferencia realmente en curso
            pendingTitle = title; pendingText = text; pendingIdle = false;
            if (sForeground) {          // ya activo: solo actualizar el texto
                updateProgress(ctx, title, text, -1);
                return;
            }
            Intent i = new Intent(ctx, TransferService.class);
            i.putExtra("title", title);
            i.putExtra("text", text);
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i);
            else ctx.startService(i);
        } catch (Exception e) {
            // No crashear si el sistema rechaza el inicio del servicio.
        }
    }

    // Actualiza la barra de progreso vía NotificationManager (no depende de que
    // la instancia del servicio ya exista → la barra avanza de forma fiable).
    public static void updateProgress(Context ctx, String title, String text, int percent) {
        NotificationManager nm =
            (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        ensureChannel(ctx, NotificationManager.IMPORTANCE_LOW);
        nm.notify(NOTIF_ID, buildNotif(ctx, title, text, percent));
    }

    public static void stopService(Context ctx) {
        sForeground = false;
        sTransferActive = false;
        try { ctx.stopService(new Intent(ctx, TransferService.class)); } catch (Exception ignored) {}
    }

    // Notificación breve de resultado (no persistente, se cierra al tocarla).
    public static void showResult(Context ctx, String title, String text) {
        NotificationManager nm =
            (NotificationManager) ctx.getSystemService(Context.NOTIFICATION_SERVICE);
        if (nm == null) return;
        ensureChannel(ctx, NotificationManager.IMPORTANCE_DEFAULT);
        Notification.Builder b = (Build.VERSION.SDK_INT >= 26)
            ? new Notification.Builder(ctx, CHANNEL)
            : new Notification.Builder(ctx);
        b.setContentTitle(title != null ? title : "VorLAN")
         .setContentText(text != null ? text : "")
         .setSmallIcon(notifIcon(ctx))
         .setColor(0xFF57A63A)
         .setAutoCancel(true)
         .setContentIntent(openReceivedIntent(ctx));
        nm.notify(RESULT_ID, b.build());
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}
