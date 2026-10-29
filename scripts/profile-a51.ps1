# Collect genuine device measurements; never substitute simulator data for A51.
param(
    [ValidateRange(10,3600)][int]$Seconds = 180,
    [ValidateRange(2,60)][int]$Interval = 10,
    [string]$OutDir = "a51-profile",
    [switch]$NoLaunch
)
$ErrorActionPreference = "Stop"
$adb = Get-Command adb -ErrorAction Stop
$device = (& adb get-state 2>&1 | Out-String).Trim()
if ($device -ne "device") { throw "Connect a trusted Android device (adb get-state -> device)." }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
function Save-ADB([string]$Relative, [string[]]$Arguments) {
    $path = Join-Path $OutDir $Relative
    (& adb @Arguments 2>&1 | Out-String) | Out-File -FilePath $path -Encoding utf8
}
Save-ADB "device.txt" @("shell","getprop")
Save-ADB "screen-size.txt" @("shell","wm","size")
Save-ADB "screen-density.txt" @("shell","wm","density")
Save-ADB "refresh.txt" @("shell","dumpsys","display")
Save-ADB "memory-before.txt" @("shell","dumpsys","meminfo","org.nzportable.android:game")
Save-ADB "battery-before.txt" @("shell","dumpsys","battery")
Save-ADB "thermal-before.txt" @("shell","dumpsys","thermalservice")
if (-not $NoLaunch) {
    & adb shell am start -n org.nzportable.android/.LauncherActivity | Out-Null
}
Write-Host "Configure VIDEO OPTIONS -> PERFORMANCE LOG = ENABLED before measuring."
Write-Host "Use the same map, camera angle, native render scale, FPS cap, graphics quality and gameplay."
Write-Host "Collecting $Seconds seconds. CPU / thermal / battery samples every $Interval seconds."

# Sampling is intentionally low-rate to avoid turning instrumentation into
# a source of extra power consumption and poor frame pacing.
$records = @()
$clock = [System.Diagnostics.Stopwatch]::StartNew()
$next = 0
while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
    if ($clock.Elapsed.TotalSeconds -ge $next) {
        $stamp = Get-Date -Format "yyyy-MM-ddTHH:mm:ss.fffK"
        $index = "{0:D4}" -f $next
        $snap = Join-Path $OutDir "sample-$index.txt"
        @("TIMESTAMP=$stamp", "ELAPSED_S=$([math]::Round($clock.Elapsed.TotalSeconds,2))") |
            Out-File $snap -Encoding utf8
        "=== BATTERY ===" | Out-File $snap -Encoding utf8 -Append
        (& adb shell dumpsys battery 2>&1) | Out-File $snap -Encoding utf8 -Append
        "=== THERMAL ===" | Out-File $snap -Encoding utf8 -Append
        (& adb shell dumpsys thermalservice 2>&1) | Out-File $snap -Encoding utf8 -Append
        "=== MEMORY ===" | Out-File $snap -Encoding utf8 -Append
        (& adb shell dumpsys meminfo org.nzportable.android:game 2>&1) |
            Out-File $snap -Encoding utf8 -Append
        "=== CPU (top) ===" | Out-File $snap -Encoding utf8 -Append
        (& adb shell top -b -n 1 2>&1) | Out-File $snap -Encoding utf8 -Append
        $records += $snap
        $next += $Interval
    }
    Start-Sleep -Milliseconds 500
}
$clock.Stop()
Save-ADB "logcat.txt" @("logcat","-d","-v","threadtime")
Save-ADB "gfxinfo.txt" @("shell","dumpsys","gfxinfo","org.nzportable.android:game","framestats")
Save-ADB "memory-after.txt" @("shell","dumpsys","meminfo","org.nzportable.android:game")
Save-ADB "battery-after.txt" @("shell","dumpsys","battery")
Save-ADB "thermal-after.txt" @("shell","dumpsys","thermalservice")
Write-Host "Saved $($records.Count) CPU/thermal/memory/battery snapshots to $OutDir."
Write-Host "Compare real before/after logs:"
Write-Host "python scripts/compare_android_perf.py old/logcat.txt $OutDir/logcat.txt"
Write-Host "GPU frame time/power are unavailable unless measured with device-supported tools."
