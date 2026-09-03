package net.vorlan;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

// Guarda archivos dentro de un árbol de carpetas elegido por el usuario
// (Storage Access Framework), creando las subcarpetas necesarias.
public class SafStorage {

    private static String mimeFromName(String name) {
        String ext = "";
        int dot = name.lastIndexOf('.');
        if (dot >= 0) ext = name.substring(dot + 1).toLowerCase();
        String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext);
        return mime != null ? mime : "application/octet-stream";
    }

    // --- ENVÍO: resolver un content:// (archivo o carpeta SAF) a rutas reales ---
    // Qt (FileDialog/FolderDialog) devuelve URIs content:// que el emisor, basado en
    // el sistema de archivos, no sabe leer. Copiamos el archivo/carpeta a la caché
    // (preservando la estructura) y devolvemos la ruta real, que ya se envía normal.

    // Vacía la caché de salida (llamar una vez antes de copiar los elementos de un envío).
    public static void clearOutgoing(Context ctx) {
        try { deleteRecursive(new File(ctx.getCacheDir(), "outgoing")); } catch (Exception ignored) {}
    }

    // Copia un content:// (archivo o árbol de carpeta) a la caché. Devuelve la ruta
    // real (archivo o carpeta) o "" si falla.
    public static String copyOutgoing(Context ctx, String uriStr) {
        try {
            ContentResolver cr = ctx.getContentResolver();
            Uri uri = Uri.parse(uriStr);
            File outRoot = new File(ctx.getCacheDir(), "outgoing");
            outRoot.mkdirs();
            if (DocumentsContract.isTreeUri(uri)) {       // carpeta (árbol SAF)
                String treeDocId = DocumentsContract.getTreeDocumentId(uri);
                Uri docUri = DocumentsContract.buildDocumentUriUsingTree(uri, treeDocId);
                String name = safe(queryName(cr, docUri), "carpeta");
                File dst = new File(outRoot, name);
                dst.mkdirs();
                copyTree(cr, uri, treeDocId, dst);
                return dst.getAbsolutePath();
            } else {                                         // archivo suelto
                String name = safe(queryName(cr, uri), "archivo-" + System.currentTimeMillis());
                File dst = uniqueFile(outRoot, name);   // varios con el mismo nombre → no pisarse
                // Si no se pudo leer la URI, no devolver una ruta rota (se omite el ítem).
                if (!copyStream(cr, uri, dst)) { dst.delete(); return ""; }
                return dst.getAbsolutePath();
            }
        } catch (Exception e) {
            return "";
        }
    }

    // "nombre.ext", "nombre (1).ext"… dentro de dir (para no pisar homónimos).
    private static File uniqueFile(File dir, String name) {
        File f = new File(dir, name);
        if (!f.exists()) return f;
        int dot = name.lastIndexOf('.');
        String stem = (dot > 0) ? name.substring(0, dot) : name;
        String ext = (dot > 0) ? name.substring(dot) : "";
        for (int n = 1; n < 10000; n++) {
            f = new File(dir, stem + " (" + n + ")" + ext);
            if (!f.exists()) return f;
        }
        return f;
    }

    private static void copyTree(ContentResolver cr, Uri treeUri, String parentDocId, File destDir) {
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        Cursor c = null;
        try {
            c = cr.query(childrenUri, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE}, null, null, null);
            while (c != null && c.moveToNext()) {
                String childId = c.getString(0);
                String childName = safe(c.getString(1), null);
                if (childName == null) continue;
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) {
                    File sub = new File(destDir, childName);
                    sub.mkdirs();
                    copyTree(cr, treeUri, childId, sub);
                } else {
                    Uri fileUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, childId);
                    copyStream(cr, fileUri, new File(destDir, childName));
                }
            }
        } catch (Exception ignored) {
        } finally {
            if (c != null) c.close();
        }
    }

    // Devuelve true si la copia se completó (un archivo vacío legítimo también es true).
    private static boolean copyStream(ContentResolver cr, Uri src, File dst) {
        InputStream in = null; OutputStream out = null;
        try {
            in = cr.openInputStream(src);
            if (in == null) return false;
            out = new FileOutputStream(dst);
            byte[] buf = new byte[262144];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.flush();
            return true;
        } catch (Exception e) {
            return false;
        } finally {
            try { if (in != null) in.close(); } catch (Exception e) {}
            try { if (out != null) out.close(); } catch (Exception e) {}
        }
    }

    private static String queryName(ContentResolver cr, Uri uri) {
        Cursor c = null;
        try {
            c = cr.query(uri, new String[]{ DocumentsContract.Document.COLUMN_DISPLAY_NAME },
                    null, null, null);
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) {
        } finally {
            if (c != null) c.close();
        }
        return null;
    }

    private static String safe(String name, String fallback) {
        if (name == null || name.isEmpty()) return fallback;
        return name.replaceAll("[/\\\\:*?\"<>|\\x00]", "_").trim();
    }

    private static void deleteRecursive(File f) {
        if (f == null || !f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null) for (File k : kids) deleteRecursive(k);
        f.delete();
    }

    // Guarda srcPath dentro del árbol treeUriStr respetando relPath (subcarpetas
    // separadas por '/'). Devuelve la URI del archivo creado, o null si falla.
    public static String saveToTree(Context ctx, String treeUriStr,
                                    String relPath, String srcPath) {
        InputStream in = null;
        OutputStream out = null;
        try {
            ContentResolver cr = ctx.getContentResolver();
            Uri treeUri = Uri.parse(treeUriStr);
            String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri dirUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, treeDocId);

            String[] parts = relPath.split("/");
            // crear/recorrer las subcarpetas
            for (int i = 0; i < parts.length - 1; i++) {
                if (parts[i].isEmpty()) continue;
                Uri child = findChild(cr, treeUri, dirUri, parts[i]);
                if (child == null)
                    child = DocumentsContract.createDocument(cr, dirUri,
                            DocumentsContract.Document.MIME_TYPE_DIR, parts[i]);
                if (child == null) return null;
                dirUri = child;
            }
            String fileName = parts[parts.length - 1];
            if (fileName.isEmpty()) return null;

            // Si ya existe, NO sobrescribir: usar un nombre único "nombre (n).ext".
            if (findChild(cr, treeUri, dirUri, fileName) != null)
                fileName = uniqueChildName(cr, treeUri, dirUri, fileName);

            Uri fileUri = DocumentsContract.createDocument(cr, dirUri,
                    mimeFromName(fileName), fileName);
            if (fileUri == null) return null;

            in = new FileInputStream(srcPath);
            out = cr.openOutputStream(fileUri);
            if (out == null) return null;
            byte[] buf = new byte[262144];
            int n;
            while ((n = in.read(buf)) > 0)
                out.write(buf, 0, n);
            out.flush();
            return fileUri.toString();
        } catch (Exception e) {
            return null;
        } finally {
            try { if (in != null) in.close(); } catch (Exception ignored) {}
            try { if (out != null) out.close(); } catch (Exception ignored) {}
        }
    }

    // Busca un hijo por nombre bajo parentDocUri; devuelve su Uri de documento o null.
    private static Uri findChild(ContentResolver cr, Uri treeUri,
                                 Uri parentDocUri, String name) {
        String parentDocId = DocumentsContract.getDocumentId(parentDocUri);
        Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentDocId);
        Cursor c = null;
        try {
            c = cr.query(childrenUri, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null);
            while (c != null && c.moveToNext()) {
                if (name.equals(c.getString(1)))
                    return DocumentsContract.buildDocumentUriUsingTree(treeUri, c.getString(0));
            }
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.close();
        }
        return null;
    }

    // Devuelve un nombre único bajo dirUri: "nombre (1).ext", "nombre (2).ext"…
    private static String uniqueChildName(ContentResolver cr, Uri treeUri, Uri dirUri, String name) {
        int dot = name.lastIndexOf('.');
        String stem = (dot > 0) ? name.substring(0, dot) : name;
        String ext = (dot > 0) ? name.substring(dot) : "";
        int n = 1;
        String candidate;
        do {
            candidate = stem + " (" + n + ")" + ext;
            n++;
        } while (findChild(cr, treeUri, dirUri, candidate) != null && n < 10000);
        return candidate;
    }

    // ¿Existe una carpeta con ese nombre dentro del árbol SAF?
    public static boolean folderExistsInTree(Context ctx, String treeUriStr, String folderName) {
        if (treeUriStr == null || treeUriStr.isEmpty() || folderName == null || folderName.isEmpty())
            return false;
        try {
            ContentResolver cr = ctx.getContentResolver();
            Uri treeUri = Uri.parse(treeUriStr);
            String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri dirUri = DocumentsContract.buildDocumentUriUsingTree(treeUri, treeDocId);
            return findChild(cr, treeUri, dirUri, folderName) != null;
        } catch (Exception e) {
            return false;
        }
    }
}
