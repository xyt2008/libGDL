package com.rex.utils;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import java.io.File;

/**
 * @date 14-5-20 2:11 PM
 * @author VisionIn
 */
public class ApkUtil {

    public static void install(Context context,String filePath)
    {
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.setDataAndType(Uri.fromFile(new File(filePath)), "application/vnd.android.package-archive");
        context.startActivity(intent);
    }

    public static void unInstall(Context context,String packageString)
    {
        Uri packageURI = Uri.parse("package:"+packageString); //com.demo.CanvasCancel
        Intent uninstallIntent = new Intent(Intent.ACTION_DELETE, packageURI);
        context.startActivity(uninstallIntent);
    }
}
