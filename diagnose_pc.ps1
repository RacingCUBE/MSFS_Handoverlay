Write-Output "=== ActiveRuntime ==="
Get-ItemProperty "HKLM:\SOFTWARE\Khronos\OpenXR\1" -ErrorAction SilentlyContinue | Select-Object ActiveRuntime

Write-Output "`n=== Implicit ApiLayers ==="
$k = Get-Item "HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit" -ErrorAction SilentlyContinue
if ($k) { foreach ($n in $k.GetValueNames()) { Write-Output "  '$n' = $($k.GetValue($n))" } } else { Write-Output "  (key not found)" }

Write-Output "`n=== Explicit ApiLayers ==="
$k2 = Get-Item "HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit" -ErrorAction SilentlyContinue
if ($k2) { foreach ($n in $k2.GetValueNames()) { Write-Output "  '$n' = $($k2.GetValue($n))" } } else { Write-Output "  (key not found)" }

Write-Output "`n=== C:\Program Files\MSFSHandOverlay ==="
Get-ChildItem "C:\Program Files\MSFSHandOverlay" -ErrorAction SilentlyContinue | Select-Object Name, Length, LastWriteTime

Write-Output "`n=== C:\ProgramData\OpenXR\1\api_layers\explicit.d ==="
Get-ChildItem "C:\ProgramData\OpenXR\1\api_layers\explicit.d" -ErrorAction SilentlyContinue | Select-Object Name, Length, LastWriteTime

Write-Output "`n=== MSFS process + install path ==="
$fs = Get-Process FlightSimulator -ErrorAction SilentlyContinue
if ($fs) {
    Write-Output "  Running, Path: $($fs.Path)"
    Write-Output "  Started: $($fs.StartTime)"
    Write-Output "  Loaded modules matching MSFSHandOverlay/openxr:"
    $fs.Modules | Where-Object { $_.ModuleName -match "MSFSHandOverlay|openxr" } | Select-Object ModuleName, FileName
} else {
    Write-Output "  FlightSimulator.exe not currently running"
}

Write-Output "`n=== MSFSHandOverlay.exe process ==="
Get-Process MSFSHandOverlay -ErrorAction SilentlyContinue | Select-Object Id, Path, StartTime

Write-Output "`n=== DISABLE_XR_APILAYER_MSFS_HandOverlay env var ==="
Write-Output "  Process: $([Environment]::GetEnvironmentVariable('DISABLE_XR_APILAYER_MSFS_HandOverlay','Process'))"
Write-Output "  User: $([Environment]::GetEnvironmentVariable('DISABLE_XR_APILAYER_MSFS_HandOverlay','User'))"
Write-Output "  Machine: $([Environment]::GetEnvironmentVariable('DISABLE_XR_APILAYER_MSFS_HandOverlay','Machine'))"
