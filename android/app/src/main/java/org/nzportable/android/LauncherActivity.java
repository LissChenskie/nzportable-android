// SPDX-License-Identifier: GPL-2.0-or-later
package org.nzportable.android;
import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.*;
import android.graphics.Color;
import java.io.*;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class LauncherActivity extends Activity {
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private TextView status;private Button play,importZip;private boolean busy;
    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        LinearLayout box=new LinearLayout(this);box.setOrientation(LinearLayout.VERTICAL);box.setPadding(40,24,40,24);box.setBackgroundColor(Color.rgb(17,23,27));
        TextView title=new TextView(this);title.setText("NZPortable");title.setTextSize(32);title.setTextColor(Color.WHITE);box.addView(title);
        status=new TextView(this);status.setTextColor(Color.LTGRAY);status.setTextSize(17);box.addView(status);
        play=new Button(this);play.setText("Играть");box.addView(play);play.setOnClickListener(v->startActivity(new Intent(this,GameActivity.class)));
        importZip=new Button(this);importZip.setText("Импортировать игровые данные из ZIP");box.addView(importZip);
        importZip.setOnClickListener(v->{Intent pick=new Intent(Intent.ACTION_OPEN_DOCUMENT);pick.setType("application/zip");pick.addCategory(Intent.CATEGORY_OPENABLE);startActivityForResult(pick,7);});
        TextView graphicsLabel=new TextView(this);graphicsLabel.setText("Графический API (применяется при запуске)");graphicsLabel.setTextColor(Color.WHITE);graphicsLabel.setTextSize(17);box.addView(graphicsLabel);
        Spinner graphics=new Spinner(this);
        ArrayAdapter<String> choices=new ArrayAdapter<>(this,android.R.layout.simple_spinner_dropdown_item,
            new String[]{"Авто (проверенный OpenGL ES 2.0)","Vulkan 1.1 (тестовый)","OpenGL ES 2.0"});
        graphics.setAdapter(choices);box.addView(graphics);
        File preference=new File(getFilesDir(),"game/nzp/graphics-api.txt");
        try(BufferedReader reader=new BufferedReader(new FileReader(preference))){
            int selected=Integer.parseInt(reader.readLine().trim());if(selected>=0&&selected<3)graphics.setSelection(selected);
        }catch(IOException|RuntimeException ignored){}
        Button apply=new Button(this);apply.setText("Применить графический API");box.addView(apply);
        apply.setOnClickListener(v->{
            if(!DataInstaller.ready(new File(getFilesDir(),"game"))){status.setText("Сначала импортируйте игровые данные.");return;}
            File temporary=new File(preference.getPath()+".tmp");
            try(FileOutputStream output=new FileOutputStream(temporary)){
                output.write((graphics.getSelectedItemPosition()+"\n").getBytes("UTF-8"));output.getFD().sync();
            }catch(IOException error){status.setText("Не удалось сохранить API: "+error.getMessage());return;}
            if(!temporary.renameTo(preference)){status.setText("Не удалось применить графический API.");return;}
            new File(preference.getParentFile(),"vulkan-failed").delete();
            new File(preference.getParentFile(),"vulkan-starting").delete();
            status.setText("API сохранён. Нажмите «Играть». При ошибке Vulkan будет использован OpenGL ES 2.0.");
        });
        setContentView(box);refresh();
        if(!DataInstaller.ready(new File(getFilesDir(),"game"))){
            try{InputStream packaged=getAssets().open("bootstrap/game-data.zip");install(packaged);}
            catch(IOException e){status.setText("Импортируйте полный PC-релиз NZPortable: ZIP должен содержать папку nzp и progs.dat.");}
        }
    }
    private void refresh(){boolean ready=DataInstaller.ready(new File(getFilesDir(),"game"));play.setEnabled(ready&&!busy);importZip.setEnabled(!busy);
        if(!busy)status.setText(ready?"Игра готова. Поддерживаются сенсорное управление и геймпад.":"Для запуска нужны игровые данные NZPortable.");}
    private void install(InputStream stream){busy=true;refresh();status.setText("Подготовка игровых данных. Это выполняется один раз…");
        worker.execute(()->{String error=null;try{DataInstaller.install(stream,getFilesDir());}catch(IOException e){error=e.getMessage();}
            final String result=error;runOnUiThread(()->{busy=false;refresh();if(result!=null)status.setText("Ошибка установки: "+result);});});}
    @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);
        if(request==7&&result==RESULT_OK&&data!=null&&data.getData()!=null){try{InputStream input=getContentResolver().openInputStream(data.getData());
            if(input!=null)install(input);}catch(IOException e){status.setText("Не удалось открыть ZIP: "+e.getMessage());}}}
    @Override protected void onDestroy(){worker.shutdown();super.onDestroy();}
}
