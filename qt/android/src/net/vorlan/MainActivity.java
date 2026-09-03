package net.vorlan;

import android.content.Intent;

import org.qtproject.qt.android.bindings.QtActivity;

// Actividad estándar de Qt. Lo único propio es onNewIntent: cuando llega un intent
// "Compartir" con la app ya abierta, actualiza el intent para que getIntent() lo
// devuelva y se procese el archivo/texto compartido.
//
// NOTA: NO se toca el ciclo de vida (nada de finish()/killProcess).
// El proceso se mantiene vivo en segundo plano (android.app.background_running
// = true en el manifest) para que al minimizar y reabrir Qt restaure la vista al
// instante, sin mostrar el splash otra vez ni colgarse.
public class MainActivity extends QtActivity {

    // ¿Se abrió la app tocando la notificación? La UI lo consulta para saltar a
    // la pestaña "Recibidos".
    private static volatile boolean sOpenReceived = false;

    public static boolean takeOpenReceived() {
        boolean v = sOpenReceived;
        sOpenReceived = false;
        return v;
    }

    private static void checkOpenReceived(Intent i) {
        if (i != null && TransferService.ACTION_OPEN_RECEIVED.equals(i.getAction()))
            sOpenReceived = true;
    }

    @Override
    public void onCreate(android.os.Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        checkOpenReceived(getIntent());   // app cerrada: se abre desde la notificación
    }

    @Override
    protected void onNewIntent(Intent intent) {
        checkOpenReceived(intent);        // app ya abierta: llega el intent aquí
        setIntent(intent);
        super.onNewIntent(intent);
    }
}
