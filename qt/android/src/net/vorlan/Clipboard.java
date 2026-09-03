package net.vorlan;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

// Acceso nativo al portapapeles de Android (el QClipboard de Qt no lee imágenes).
public class Clipboard {

    // Si el portapapeles contiene una imagen (p.ej. una captura), la copia a la
    // caché y devuelve la ruta del archivo; si no, devuelve "".
    public static String stageImage(Context ctx) {
        InputStream in = null;
        OutputStream out = null;
        try {
            ClipboardManager cm =
                (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null || !cm.hasPrimaryClip()) return "";
            ClipData clip = cm.getPrimaryClip();
            if (clip == null || clip.getItemCount() == 0) return "";
            Uri uri = clip.getItemAt(0).getUri();
            if (uri == null) return "";   // no hay contenido (solo texto)
            ContentResolver cr = ctx.getContentResolver();
            String type = cr.getType(uri);
            // Cualquier URI no-texto (imagen, archivo copiado…) se envía como archivo.
            if (type != null && type.startsWith("text/")) return "";
            String ext = "png";
            if (type != null && type.contains("/")) {
                ext = type.substring(type.indexOf('/') + 1);
                if (ext.equals("jpeg")) ext = "jpg";
                if (ext.isEmpty() || ext.length() > 5) ext = "bin";
            }
            File dir = new File(ctx.getCacheDir(), "clip");
            dir.mkdirs();
            File dst = new File(dir, "captura-" + System.currentTimeMillis() + "." + ext);
            in = cr.openInputStream(uri);
            if (in == null) return "";
            out = new FileOutputStream(dst);
            byte[] buf = new byte[262144];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.flush();
            return dst.getAbsolutePath();
        } catch (Exception e) {
            return "";
        } finally {
            try { if (in != null) in.close(); } catch (Exception e) {}
            try { if (out != null) out.close(); } catch (Exception e) {}
        }
    }

    // Texto del portapapeles ("" si no hay).
    public static String text(Context ctx) {
        try {
            ClipboardManager cm =
                (ClipboardManager) ctx.getSystemService(Context.CLIPBOARD_SERVICE);
            if (cm == null || !cm.hasPrimaryClip()) return "";
            ClipData clip = cm.getPrimaryClip();
            if (clip == null || clip.getItemCount() == 0) return "";
            CharSequence t = clip.getItemAt(0).coerceToText(ctx);
            return t == null ? "" : t.toString();
        } catch (Exception e) {
            return "";
        }
    }
}
