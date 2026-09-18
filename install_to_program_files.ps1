# Installs the MSFSHandOverlay OpenXR API layer to Program Files and enables it (like OpenKneeboard does).
# Safe to re-run on a PC that already has it installed.

# Self-elevate if not already running as Administrator.
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Requesting Administrator privileges..."
    Start-Process powershell -Verb RunAs -ArgumentList "-NoExit", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`""
    exit
}

# Always run from the script's own folder so the relative build\bin\Release paths work
# regardless of where this was launched from (e.g. don't run this from inside the build folder).
Set-Location (Split-Path -Parent $MyInvocation.MyCommand.Path)

$dest = "C:\Program Files\MSFSHandOverlay"
Write-Host "Installing to $dest ..."
New-Item -ItemType Directory -Force -Path $dest | Out-Null

# Retry copies: reading the source over a network/cloud-synced drive (e.g. Google Drive)
# can hit transient "semaphore timeout" errors on the first attempt.
function Copy-WithRetry {
    param([string]$Source, [string]$Destination, [int]$MaxAttempts = 5)
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            Copy-Item -Path $Source -Destination $Destination -Force -ErrorAction Stop
            return
        } catch {
            if ($attempt -eq $MaxAttempts) { throw }
            Write-Host "  Copy of '$Source' failed (attempt $attempt/$MaxAttempts): $($_.Exception.Message)"
            Write-Host "  Retrying in 2 seconds..."
            Start-Sleep -Seconds 2
        }
    }
}

if (-not (Test-Path "build\bin\Release\MSFSHandOverlay_Layer.dll")) {
    Write-Host "[ERROR] Could not find build\bin\Release\MSFSHandOverlay_Layer.dll relative to this script."
    Write-Host "        Make sure this script still lives next to the 'build' folder, and that it has been built."
    Read-Host "Press Enter to exit"
    exit 1
}

Copy-WithRetry "build\bin\Release\MSFSHandOverlay_Layer.dll" $dest
Copy-WithRetry "build\bin\Release\openxr_loader.dll" "$dest\openxr_loader.dll"

# Manifest: built with ConvertTo-Json so backslashes/paths are always escaped correctly
# (a hand-rolled here-string previously produced invalid JSON that the loader silently ignored).
# library_path is relative to the manifest's own folder, matching other known-working layers.
$manifestObj = [ordered]@{
    file_format_version = "1.0.0"
    api_layer            = [ordered]@{
        name                    = "XR_APILAYER_MSFS_HandOverlay"
        library_path            = ".\MSFSHandOverlay_Layer.dll"
        api_version             = "1.0"
        implementation_version  = "1"
        description             = "MSFS Hand Overlay"
        functions               = @{
            xrNegotiateLoaderApiLayerInterface = "xrNegotiateLoaderApiLayerInterface"
        }
        disable_environment     = "DISABLE_XR_APILAYER_MSFS_HandOverlay"
    }
}
# IMPORTANT: Out-File -Encoding UTF8 writes a UTF-8 BOM in Windows PowerShell, and the OpenXR
# loader's JSON parser does not skip it - it fails with "failed to parse ... Line 1, Column 1"
# and silently drops the whole layer (confirmed via OpenXR-Loader debug output). Write BOM-less
# UTF-8 explicitly instead.
$manifestJson = $manifestObj | ConvertTo-Json -Depth 5
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText("$dest\MSFSHandOverlay_api_layer.json", $manifestJson, $utf8NoBom)

# Register AND enable the layer.
# IMPORTANT: this registry DWORD is a DISABLE flag, not an enable flag.
#   0         = enabled / on
#   non-zero  = disabled / off
# This is the exact same value that SteamVR Settings > OpenXR > "Manage OpenXR API Layers"
# reads and writes, so setting it to 0 here is equivalent to toggling it "On" in that dialog.
$regPath = "HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit"
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name "$dest\MSFSHandOverlay_api_layer.json" -Value 0 -PropertyType DWord -Force | Out-Null

# Clean up a stale registration from an older explicit-layer install method, if present.
Remove-ItemProperty -Path $regPath -Name "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" -ErrorAction SilentlyContinue

Write-Host "`n[OK] Installed and ENABLED in Program Files."

$activeRuntime = (Get-ItemProperty "HKLM:\SOFTWARE\Khronos\OpenXR\1" -ErrorAction SilentlyContinue).ActiveRuntime
if ($activeRuntime -match "SteamVR") {
    Write-Host "[OK] SteamVR is the active OpenXR runtime ($activeRuntime)."
} else {
    Write-Host "[WARNING] Active OpenXR runtime is currently: $activeRuntime"
    Write-Host "          If that's not SteamVR, start SteamVR at least once (it usually claims this automatically),"
    Write-Host "          or set it manually via SteamVR Settings > OpenXR > 'Set SteamVR as OpenXR Runtime'."
}

Write-Host ""
Write-Host "Next steps on this PC:"
Write-Host "  1. Start SteamVR. In SteamVR Settings > OpenXR > 'Manage OpenXR API Layers', confirm 'MSFS Hand Overlay' shows On"
Write-Host "     (it's toggled on already by this installer, but this dialog is the quick way to double check or flip it)."
Write-Host "  2. Camera indices in config\settings.ini (LeftCameraIndex / RightCameraIndex) are PC-specific and DO NOT carry"
Write-Host "     over from another machine - USB enumeration order varies per PC and even per USB port. Run"
Write-Host "     build\bin\Release\MSFSHandOverlay.exe from a terminal and check the console: a correct camera reports"
Write-Host "     'opened: 640x480' (or your configured resolution); a wrong index may report a very different resolution"
Write-Host "     (e.g. a VR headset's own built-in tracking camera) or fail to open entirely. Try indices 0, 1, 2, 3... until"
Write-Host "     both Left and Right report the expected resolution."
Write-Host "  3. Launch MSFS, enter VR, and verify the hand overlay appears."
