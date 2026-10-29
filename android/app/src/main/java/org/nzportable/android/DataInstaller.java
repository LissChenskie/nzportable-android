// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;
import java.io.*;
import java.util.zip.*;

// Pure Java so the ZIP boundary can be tested on the host as well as on Android.
public final class DataInstaller {
    private static final long MAX_BYTES=1024L*1024*1024;
    private static final int MAX_FILES=30000;
    public static boolean ready(File game){return new File(game,"nzp/progs.dat").isFile()&&new File(game,".installed").isFile();}
    public static void install(InputStream source, File filesDir) throws IOException {
        File stage=new File(filesDir,"game-staging"),game=new File(filesDir,"game"),old=new File(filesDir,"game-previous");
        if(!game.exists()&&ready(old)&&!old.renameTo(game))throw new IOException("Cannot recover previous installation");
        remove(stage);if(!stage.mkdirs())throw new IOException("Cannot create game staging directory");
        boolean committed=false;
        try {
            String root=stage.getCanonicalPath()+File.separator;long total=0;int files=0;
            byte[] buffer=new byte[64*1024];
            try(ZipInputStream zip=new ZipInputStream(new BufferedInputStream(source))){
                ZipEntry entry;
                while((entry=zip.getNextEntry())!=null){
                    if(++files>MAX_FILES)throw new IOException("Too many ZIP entries");
                    String name=entry.getName().replace('\\','/');
                    File target=new File(stage,name);
                    if(name.startsWith("/")||name.indexOf(':')>=0||!target.getCanonicalPath().startsWith(root))throw new IOException("Unsafe ZIP path");
                    // The official PC ZIP also contains a Linux executable; only game data belongs in the app.
                    if(!name.startsWith("nzp/")){int n;while((n=zip.read(buffer))!=-1){total+=n;if(total>MAX_BYTES)throw new IOException("Game ZIP exceeds 1 GiB");}zip.closeEntry();continue;}
                    if(entry.isDirectory()){if(!target.isDirectory()&&!target.mkdirs())throw new IOException("Cannot create directory");continue;}
                    File parent=target.getParentFile();if(!parent.isDirectory()&&!parent.mkdirs())throw new IOException("Cannot create parent");
                    try(OutputStream out=new BufferedOutputStream(new FileOutputStream(target))){int n;
                        while((n=zip.read(buffer))!=-1){total+=n;if(total>MAX_BYTES)throw new IOException("Game ZIP exceeds 1 GiB");out.write(buffer,0,n);}}
                    zip.closeEntry();
                }
            }
            if(!new File(stage,"nzp/progs.dat").isFile())throw new IOException("ZIP must contain nzp/progs.dat (use a full PC release)");
            try(FileWriter marker=new FileWriter(new File(stage,".installed"))){marker.write("1\n");}
            remove(old);
            if(game.exists()&&!game.renameTo(old))throw new IOException("Cannot preserve existing game data");
            if(!stage.renameTo(game)){if(old.exists())old.renameTo(game);throw new IOException("Cannot activate game data");}
            committed=true;remove(old);
        } finally {if(!committed)remove(stage);}
    }
    private static void remove(File f) throws IOException {
        if(!f.exists())return;File[] children=f.listFiles();if(children!=null)for(File child:children)remove(child);
        if(!f.delete())throw new IOException("Cannot remove "+f.getName());
    }
    private DataInstaller(){}
}
