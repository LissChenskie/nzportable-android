param([int]$Seconds = 120, [string]$OutDir = "a51-profile")
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
adb shell getprop ro.product.model | Out-File "$OutDir/device.txt"
adb shell getprop ro.hardware | Out-File "$OutDir/device.txt" -Append
adb shell getprop ro.product.cpu.abilist | Out-File "$OutDir/device.txt" -Append
adb logcat -c
adb shell am start -n org.nzportable.android/.LauncherActivity
Write-Host "Start a map, play for $Seconds seconds, then keep this window open."
Start-Sleep -Seconds $Seconds
adb logcat -d -v threadtime | Out-File "$OutDir/logcat.txt" -Encoding utf8
adb shell dumpsys thermalservice | Out-File "$OutDir/thermal.txt" -Encoding utf8
adb shell dumpsys meminfo org.nzportable.android:game | Out-File "$OutDir/memory.txt" -Encoding utf8
adb shell dumpsys battery | Out-File "$OutDir/battery.txt" -Encoding utf8
Write-Host "Saved device, game logs, thermal, memory and battery snapshots to $OutDir."
