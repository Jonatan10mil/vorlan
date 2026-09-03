package net.vorlan;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.provider.Settings;

public class StoragePermission {

    public static boolean isGranted() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
            return true;
        return Environment.isExternalStorageManager();
    }

    public static void request(Context ctx) {
        if (isGranted()) return;
        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + ctx.getPackageName()));
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        ctx.startActivity(intent);
    }

    // Consulta MediaStore para detectar si existe una carpeta con ese nombre
    // en Download/Vorlan/.  Devuelve true si hay al menos un archivo con
    // relative_path LIKE '%/Vorlan/<name>/%' o exactamente '%/Vorlan/<name>'.
    public static boolean existsInDownloads(Context ctx, String name) {
        if (name == null || name.isEmpty()) return false;
        try {
            ContentResolver resolver = ctx.getContentResolver();
            Uri uri = MediaStore.Downloads.EXTERNAL_CONTENT_URI;
            String selection = "relative_path LIKE ? OR relative_path LIKE ?";
            String likeSub = "%/Vorlan/" + name + "/%";
            String likeExact = "%/Vorlan/" + name;
            String[] args = {likeSub, likeExact};
            Cursor cursor = resolver.query(uri, new String[]{"_id"}, selection, args, null);
            if (cursor != null) {
                boolean found = cursor.getCount() > 0;
                cursor.close();
                return found;
            }
        } catch (Exception e) {
            // SecurityException si no hay permiso, etc.
        }
        return false;
    }
}
