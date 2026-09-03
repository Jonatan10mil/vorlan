package net.vorlan;

import android.app.Activity;
import android.os.Build;
import android.view.View;
import android.view.WindowInsetsController;

// Ajusta el color de los iconos de la barra de estado/navegación para que
// contrasten con el fondo de la app (que se dibuja detrás, edge-to-edge).
public class SystemBars {

    // lightBackground=true  → app en tema CLARO → iconos OSCUROS.
    // lightBackground=false → app en tema OSCURO → iconos CLAROS (blancos).
    public static void apply(final Activity a, final boolean lightBackground) {
        if (a == null) return;
        a.runOnUiThread(new Runnable() {
            @Override public void run() {
                try {
                    View decor = a.getWindow().getDecorView();
                    if (Build.VERSION.SDK_INT >= 30) {
                        WindowInsetsController c = a.getWindow().getInsetsController();
                        if (c != null) {
                            int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                                     | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
                            c.setSystemBarsAppearance(lightBackground ? mask : 0, mask);
                        }
                    } else {
                        int flags = decor.getSystemUiVisibility();
                        if (lightBackground) {
                            flags |= View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
                            if (Build.VERSION.SDK_INT >= 26)
                                flags |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                        } else {
                            flags &= ~View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
                            if (Build.VERSION.SDK_INT >= 26)
                                flags &= ~View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                        }
                        decor.setSystemUiVisibility(flags);
                    }
                } catch (Exception e) { /* ignorar */ }
            }
        });
    }
}
