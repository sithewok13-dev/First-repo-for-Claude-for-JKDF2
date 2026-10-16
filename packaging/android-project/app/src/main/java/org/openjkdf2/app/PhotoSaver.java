package org.openjkdf2.app;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;

import java.io.OutputStream;

/**
    Added: the camera's photos into the device's gallery
    (src/Platform/SDL2/platServices_sdl.c calls savePng from its photo-writing
    thread). Each one goes to Pictures/OpenJKDF2 through MediaStore, which
    needs no permission on Android 10 and newer. Anywhere it can't (Android 9
    and older, or any error) it answers false, and the game writes the photo
    to its own Pictures folder instead.
*/
public class PhotoSaver {
    public static boolean savePng(Context context, byte[] png, String name) {
        if (context == null || png == null || Build.VERSION.SDK_INT < 29) {
            return false;
        }
        ContentResolver resolver = context.getApplicationContext().getContentResolver();
        Uri uri = null;
        try {
            ContentValues values = new ContentValues();
            values.put(MediaStore.Images.Media.DISPLAY_NAME, name + ".png");
            values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
            values.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/OpenJKDF2");
            values.put(MediaStore.Images.Media.IS_PENDING, 1);
            uri = resolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
            if (uri == null) {
                return false;
            }
            OutputStream out = resolver.openOutputStream(uri);
            if (out == null) {
                resolver.delete(uri, null, null);
                return false;
            }
            try {
                out.write(png);
            } finally {
                out.close();
            }
            values.clear();
            values.put(MediaStore.Images.Media.IS_PENDING, 0);
            resolver.update(uri, values, null, null);
            return true;
        } catch (Exception e) {
            if (uri != null) {
                try {
                    resolver.delete(uri, null, null);
                } catch (Exception e2) {
                    // (nothing more to do)
                }
            }
            return false;
        }
    }
}
