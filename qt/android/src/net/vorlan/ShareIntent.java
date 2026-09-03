package net.vorlan;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

// Recibe archivos/imágenes compartidos a Vorlan (menú "Compartir" del sistema),
// los copia a la caché y devuelve sus rutas para enviarlos.
public class ShareIntent {

    private static final String TAG = "Vorlan";

    // Texto/enlace compartido (lo rellena take() cuando no vienen archivos).
    private static volatile String pendingText = "";

    // Devuelve (y consume) el texto/enlace compartido; "" si no hay.
    public static String takeText() {
        String t = pendingText;
        pendingText = "";
        return t == null ? "" : t;
    }

    // Devuelve las rutas de los archivos compartidos separadas por '\n' ("" si no hay).
    public static String take(Activity a) {
        try {
            Intent it = a.getIntent();
            if (it == null) { Log.i(TAG, "take: intent null"); return ""; }
            String action = it.getAction();
            Log.i(TAG, "take: action=" + action + " type=" + it.getType());
            if (action == null) return "";

            ArrayList<Uri> uris = new ArrayList<>();
            if (Intent.ACTION_SEND.equals(action)) {
                Uri u = it.getParcelableExtra(Intent.EXTRA_STREAM);
                if (u != null) uris.add(u);
            } else if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
                ArrayList<Uri> list = it.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
                if (list != null) uris.addAll(list);
            } else {
                return "";
            }
            Log.i(TAG, "take: uris=" + uris.size());

            // Apps de streaming (YouTube, navegador…) comparten un ENLACE, no un
            // archivo: viene en EXTRA_TEXT. Se guarda para enviarlo como texto.
            if (uris.isEmpty()) {
                CharSequence t = it.getCharSequenceExtra(Intent.EXTRA_TEXT);
                pendingText = (t != null) ? t.toString().trim() : "";
                Log.i(TAG, "take: texto compartido=" + pendingText.length() + " chars");
            }

            // Marcar consumida para no reprocesar al reanudar/rotar.
            it.setAction(null);
            it.removeExtra(Intent.EXTRA_STREAM);
            it.removeExtra(Intent.EXTRA_TEXT);
            if (uris.isEmpty()) return "";

            ContentResolver cr = a.getContentResolver();
            File dir = new File(a.getCacheDir(), "shared");
            dir.mkdirs();
            StringBuilder sb = new StringBuilder();
            for (Uri u : uris) {
                String path = copyShared(cr, dir, u);   // archivo O carpeta
                if (path != null) {
                    if (sb.length() > 0) sb.append("\n");
                    sb.append(path);
                    Log.i(TAG, "take: copiado -> " + path);
                } else {
                    Log.w(TAG, "take: no se pudo copiar " + u);
                }
            }
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }

    // Copia una URI compartida (archivo O carpeta) a `dir`. Devuelve la ruta o null.
    private static String copyShared(ContentResolver cr, File dir, Uri u) {
        try {
            boolean isTree = DocumentsContract.isTreeUri(u);
            String mime = mimeOf(cr, u, isTree);
            Log.i(TAG, "copyShared uri=" + u + " tree=" + isTree + " mime=" + mime);

            boolean isDir = isTree || DocumentsContract.Document.MIME_TYPE_DIR.equals(mime);
            if (isDir) {
                String name = safe(folderName(cr, u, isTree));
                if (name == null) name = "carpeta";
                File folder = new File(dir, name);
                folder.mkdirs();
                copyTree(cr, u, folder);
                return folder.getAbsolutePath();
            }
            String name = safe(displayName(cr, u));
            if (name == null) name = "archivo-" + System.currentTimeMillis();
            File dst = new File(dir, name);
            copyStream(cr, u, dst);
            return dst.exists() ? dst.getAbsolutePath() : null;
        } catch (Exception e) {
            Log.w(TAG, "copyShared error " + u, e);
            return null;
        }
    }

    // MIME de la URI: getType(), y si falla, la columna del documento (usando la URI
    // de documento correcta cuando es un árbol SAF).
    private static String mimeOf(ContentResolver cr, Uri u, boolean isTree) {
        try { String m = cr.getType(u); if (m != null && !m.isEmpty()) return m; } catch (Exception ignored) {}
        Uri docUri = u;
        try {
            if (isTree) docUri = DocumentsContract.buildDocumentUriUsingTree(
                    u, DocumentsContract.getTreeDocumentId(u));
        } catch (Exception ignored) {}
        Cursor c = null;
        try {
            c = cr.query(docUri, new String[]{ DocumentsContract.Document.COLUMN_MIME_TYPE },
                    null, null, null);
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        } finally { if (c != null) c.close(); }
        return null;
    }

    // Nombre de la carpeta (para árbol, del documento raíz).
    private static String folderName(ContentResolver cr, Uri u, boolean isTree) {
        Uri docUri = u;
        try {
            if (isTree) docUri = DocumentsContract.buildDocumentUriUsingTree(
                    u, DocumentsContract.getTreeDocumentId(u));
        } catch (Exception ignored) {}
        String n = displayName(cr, docUri);
        return (n == null || n.isEmpty()) ? null : n;
    }

    // Copia recursiva del árbol de una carpeta. `node` puede ser una URI de árbol
    // raíz (content://.../tree/ID), una de documento-en-árbol (.../tree/ID/document/ID2)
    // o una de documento suelto (.../document/ID). Calcula el id de documento correcto.
    private static void copyTree(ContentResolver cr, Uri node, File destDir) {
        boolean viaTree = DocumentsContract.isTreeUri(node);
        Uri childrenUri;
        try {
            if (viaTree) {
                String idStr = node.toString();
                String parentDocId = idStr.contains("/document/")
                        ? DocumentsContract.getDocumentId(node)     // subcarpeta
                        : DocumentsContract.getTreeDocumentId(node); // raíz del árbol
                childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(node, parentDocId);
            } else {
                childrenUri = DocumentsContract.buildChildDocumentsUri(
                        node.getAuthority(), DocumentsContract.getDocumentId(node));
            }
        } catch (Exception e) { return; }
        Cursor c = null;
        try {
            c = cr.query(childrenUri, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null);
            while (c != null && c.moveToNext()) {
                String childId = c.getString(0);
                String childName = safe(c.getString(1));
                if (childName == null) continue;
                Uri childUri = viaTree
                        ? DocumentsContract.buildDocumentUriUsingTree(node, childId)
                        : DocumentsContract.buildDocumentUri(node.getAuthority(), childId);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) {
                    File sub = new File(destDir, childName);
                    sub.mkdirs();
                    copyTree(cr, childUri, sub);
                } else {
                    copyStream(cr, childUri, new File(destDir, childName));
                }
            }
        } catch (Exception ignored) {
        } finally { if (c != null) c.close(); }
    }

    private static void copyStream(ContentResolver cr, Uri src, File dst) {
        InputStream in = null; OutputStream os = null;
        try {
            in = cr.openInputStream(src);
            if (in == null) return;
            os = new FileOutputStream(dst);
            byte[] buf = new byte[262144];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            os.flush();
        } catch (Exception ignored) {
        } finally {
            try { if (in != null) in.close(); } catch (Exception e) {}
            try { if (os != null) os.close(); } catch (Exception e) {}
        }
    }

    private static String safe(String name) {
        if (name == null || name.isEmpty()) return null;
        return name.replaceAll("[/\\\\:*?\"<>|\\x00]", "_").trim();
    }

    private static String displayName(ContentResolver cr, Uri u) {
        String name = null;
        Cursor c = null;
        try {
            c = cr.query(u, new String[]{ OpenableColumns.DISPLAY_NAME }, null, null, null);
            if (c != null && c.moveToFirst()) name = c.getString(0);
        } catch (Exception e) {
        } finally {
            if (c != null) c.close();
        }
        if (name == null || name.isEmpty())
            name = "archivo-" + System.currentTimeMillis();
        return name.replaceAll("[/\\\\:*?\"<>|\\x00]", "_").trim();
    }
}
