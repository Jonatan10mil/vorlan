package net.vorlan;

import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

// Abre una URI content:// con la app predeterminada (ACTION_VIEW), calculando
// el tipo MIME a partir de la extensión (con caso especial para .apk, que si no
// no se ofrece al instalador de paquetes).
public class Opener {

    // Abre la carpeta de descargas: si hay una carpeta SAF elegida (content://tree)
    // la abre en el explorador; si no, abre la pantalla de Descargas del sistema.
    public static void openFolder(Context ctx, String treeUri) {
        try {
            Intent i = new Intent(Intent.ACTION_VIEW);
            if (treeUri != null && treeUri.startsWith("content://")) {
                // Carpeta SAF elegida por el usuario (ya concedida): para que el gestor
                // ABRA DENTRO hay que pasar la URI de DOCUMENTO de la raíz del árbol,
                // no la URI de árbol cruda (que abría Descargas).
                Uri tree = Uri.parse(treeUri);
                Uri docUri = DocumentsContract.buildDocumentUriUsingTree(
                        tree, DocumentsContract.getTreeDocumentId(tree));
                i.setDataAndType(docUri, "vnd.android.document/directory");
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } else {
                // Abrir DIRECTAMENTE Download/Vorlan (donde se guardan los recibidos),
                // no la raíz de Descargas. Una URI de ÁRBOL hace que el explorador
                // navegue DENTRO de la carpeta (la de documento abría el padre).
                Uri vTree = DocumentsContract.buildTreeDocumentUri(
                        "com.android.externalstorage.documents", "primary:Download/Vorlan");
                Uri docUri = DocumentsContract.buildDocumentUriUsingTree(
                        vTree, "primary:Download/Vorlan");
                i.setDataAndType(docUri, "vnd.android.document/directory");
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            }
            // Selector: muestra TODOS los gestores de archivos instalados (no solo el
            // del sistema) para que el usuario elija con cuál abrir la carpeta.
            Intent chooser = Intent.createChooser(i, "Abrir carpeta con");
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(chooser);
        } catch (Exception e) {
            try {
                Intent d = new Intent("android.intent.action.VIEW_DOWNLOADS");
                d.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(d);
            } catch (Exception ignored) {}
        }
    }

    // Comparte un archivo recibido con otras apps (menú "Compartir" del sistema).
    public static void shareContent(Context ctx, String s) {
        try {
            Uri uri;
            if (s.startsWith("content://")) {
                uri = Uri.parse(s);
            } else {
                java.io.File f = new java.io.File(s.startsWith("file://")
                        ? Uri.parse(s).getPath() : s);
                uri = androidx.core.content.FileProvider.getUriForFile(
                        ctx, ctx.getPackageName() + ".qtprovider", f);
            }
            String mime = mimeFor(ctx, uri, displayName(ctx, uri));
            Intent i = new Intent(Intent.ACTION_SEND);
            i.setType(mime != null ? mime : "*/*");
            i.putExtra(Intent.EXTRA_STREAM, uri);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            Intent chooser = Intent.createChooser(i, "Compartir");
            chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            ctx.startActivity(chooser);
        } catch (Exception e) {
            // sin nada que hacer
        }
    }

    public static void openContent(Context ctx, String uriStr) {
        try {
            Uri uri = Uri.parse(uriStr);
            String name = displayName(ctx, uri);
            String mime = mimeFor(ctx, uri, name);

            Intent i = new Intent(Intent.ACTION_VIEW);
            i.setDataAndType(uri, mime);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
            try {
                ctx.startActivity(i);
            } catch (Exception notFound) {
                Intent chooser = Intent.createChooser(i, "Abrir con");
                chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                ctx.startActivity(chooser);
            }
        } catch (Exception e) {
            // sin nada que hacer
        }
    }

    // Nombre real (display name) de un content:// — para mostrar en Recibidos el
    // nombre que MediaStore acabó usando (p.ej. "imagen (1).png").
    public static String nameOf(Context ctx, String uriStr) {
        try { return displayName(ctx, Uri.parse(uriStr)); }
        catch (Exception e) { return ""; }
    }

    private static String displayName(Context ctx, Uri uri) {
        String name = "";
        Cursor c = null;
        try {
            c = ctx.getContentResolver().query(
                uri, new String[]{ OpenableColumns.DISPLAY_NAME }, null, null, null);
            if (c != null && c.moveToFirst())
                name = c.getString(0);
        } catch (Exception e) {
            // ignorar
        } finally {
            if (c != null) c.close();
        }
        return name == null ? "" : name;
    }

    private static String mimeFor(Context ctx, Uri uri, String name) {
        String lower = name.toLowerCase();
        if (lower.endsWith(".apk"))
            return "application/vnd.android.package-archive";

        String ext = "";
        int dot = lower.lastIndexOf('.');
        if (dot >= 0) ext = lower.substring(dot + 1);

        String mime = null;
        if (!ext.isEmpty())
            mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
        if (mime == null || mime.isEmpty())
            mime = ctx.getContentResolver().getType(uri);
        if (mime == null || mime.isEmpty() || mime.equals("application/octet-stream"))
            mime = "*/*";
        return mime;
    }
}
