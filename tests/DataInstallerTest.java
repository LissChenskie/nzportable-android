// SPDX-License-Identifier: GPL-2.0-or-later
import java.io.*;
import java.nio.file.*;
import java.util.zip.*;
import org.nzportable.android.DataInstaller;
public class DataInstallerTest {
    static byte[] zip(String path) throws Exception{ByteArrayOutputStream bytes=new ByteArrayOutputStream();
        try(ZipOutputStream out=new ZipOutputStream(bytes)){out.putNextEntry(new ZipEntry(path));out.write(new byte[]{1,2,3});out.closeEntry();}return bytes.toByteArray();}
    static void expectRejected(File root,String path) throws Exception{try{DataInstaller.install(new ByteArrayInputStream(zip(path)),root);throw new AssertionError("Accepted "+path);}catch(IOException expected){}
        if(!DataInstaller.ready(new File(root,"game")))throw new AssertionError("Failed import lost installed game");}
    public static void main(String[] args) throws Exception {
        File root=Files.createTempDirectory("nzp-install-test").toFile();
        DataInstaller.install(new ByteArrayInputStream(zip("nzp/progs.dat")),root);
        if(!DataInstaller.ready(new File(root,"game")))throw new AssertionError("Valid install failed");
        expectRejected(root,"../escape");expectRejected(root,"nzp/../../escape");expectRejected(root,"/tmp/escape");expectRejected(root,"C:\\escape");expectRejected(root,"nzp/readme.txt");
        if(new File(root.getParentFile(),"escape").exists())throw new AssertionError("Zip slip");
        if(args.length>0){try(InputStream in=new FileInputStream(args[0])){DataInstaller.install(in,root);}
            if(!DataInstaller.ready(new File(root,"game")))throw new AssertionError("Official data failed");}
        System.out.println("Installer path validation, rollback, replacement and official ZIP tests passed");
    }
}
