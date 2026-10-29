#!/usr/bin/env python3
"""Launch a disposable AVD and test the APK in the same process/network sandbox."""
import argparse
import pathlib
import re
import subprocess
import time
import xml.etree.ElementTree as ET

def main():
    p=argparse.ArgumentParser();p.add_argument('--sdk',required=True,type=pathlib.Path);p.add_argument('--apk',required=True,type=pathlib.Path);p.add_argument('--avd',default='nzp-test');p.add_argument('--out',type=pathlib.Path,default=pathlib.Path('build/emulator-smoke'))
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);adb=a.sdk/'platform-tools/adb'
    def call(*args,check=True):return subprocess.run([str(adb),*args],check=check,stdout=subprocess.PIPE,stderr=subprocess.STDOUT).stdout.decode(errors='replace')
    log=(a.out/'emulator.log').open('w');emu=subprocess.Popen([str(a.sdk/'emulator/emulator'),'-avd',a.avd,'-no-window','-no-audio','-no-boot-anim','-accel','off','-cores','2','-gpu','swiftshader_indirect','-memory','2048','-no-snapshot','-no-metrics'],stdout=log,stderr=log)
    try:
        call('start-server');deadline=time.monotonic()+300
        while time.monotonic()<deadline:
            if emu.poll() is not None:raise RuntimeError('Emulator exited: '+(a.out/'emulator.log').read_text()[-2000:])
            if call('shell','getprop','sys.boot_completed',check=False).strip()=='1':break
            time.sleep(5)
        else:raise RuntimeError('Emulator boot timeout')
        print('Emulator booted',flush=True);print(call('install','-r',str(a.apk.resolve())),flush=True)
        call('logcat','-c');print(call('shell','am','start','-n','org.nzportable.android/.LauncherActivity'),flush=True)
        for _ in range(60):
            result=call('shell','run-as','org.nzportable.android','ls','files/game/.installed',check=False)
            if 'files/game/.installed' in result and 'No such' not in result:break
            time.sleep(3)
        else:raise RuntimeError('Game data installation timeout')
        print('Game data installed',flush=True)
        for _ in range(4):
            call('shell','uiautomator','dump','/sdcard/nzp-ui.xml');ui=call('shell','cat','/sdcard/nzp-ui.xml')
            if "System UI isn't responding" not in ui:break
            print('TCG System UI is slow; dismissing its Wait dialog',flush=True)
            for node in ET.fromstring(ui).iter('node'):
                if node.attrib.get('resource-id')=='android:id/aerr_wait':
                    x0,y0,x1,y1=map(int,re.findall(r'\d+',node.attrib['bounds']));call('shell','input','tap',str((x0+x1)//2),str((y0+y1)//2));break
            time.sleep(5)
        (a.out/'launcher.xml').write_text(ui)
        (a.out/'launcher.log').write_text(call('logcat','-d','-v','threadtime'))
        root=ET.fromstring(ui)
        for node in root.iter('node'):
            if node.attrib.get('text','').lower()=='играть':
                x0,y0,x1,y1=map(int,re.findall(r'\d+',node.attrib['bounds']));call('shell','input','tap',str((x0+x1)//2),str((y0+y1)//2));break
        else:raise RuntimeError('Play button not found')
        time.sleep(20)
        with (a.out/'menu.png').open('wb') as image:subprocess.run([str(adb),'exec-out','screencap','-p'],stdout=image,check=True)
        (a.out/'menu.log').write_text(call('logcat','-d','-v','threadtime'))
        print('Menu screenshot and log saved',flush=True)
        # Exercise a real map via the engine console, not a mock renderer scene.
        call('shell','input','keyevent','68');call('shell','input','text','map%sndu');call('shell','input','keyevent','66');time.sleep(25)
        with (a.out/'map.png').open('wb') as image:subprocess.run([str(adb),'exec-out','screencap','-p'],stdout=image,check=True)
        (a.out/'game.log').write_text(call('logcat','-d','-v','threadtime'))
        process=call('shell','pidof','org.nzportable.android:game',check=False).strip();print('Game process: '+process,flush=True)
        if not process:raise RuntimeError('Game process exited')
        call('shell','input','keyevent','3');time.sleep(2);call('shell','am','start','-n','org.nzportable.android/.LauncherActivity');time.sleep(2)
        (a.out/'lifecycle.log').write_text(call('logcat','-d','-v','threadtime'))
        print('Smoke test completed; review screenshots and logs',flush=True)
    finally:
        emu.terminate()
        try:emu.wait(timeout=10)
        except subprocess.TimeoutExpired:emu.kill()
        log.close()
if __name__=='__main__':main()
