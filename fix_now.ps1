# PowerShell script to fix manifest and copy DLL
Write-Host "Force fixing manifest and DLL..." -ForegroundColor Yellow

# Check if running as admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: Must run as Administrator!" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Kill SteamVR
Write-Host "[1] Killing SteamVR processes..."
Get-Process -Name "vrmonitor","vrserver","vrdashboard","vrcompositor" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Copy DLL
Write-Host "[2] Copying DLL (forced)..."
$source = "build\bin\Release\MSFSHandOverlay_Layer.dll"
$dest = "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_Layer.dll"
Copy-Item -Path $source -Destination $dest -Force

# Create manifest with forward slashes (won't get autocorrected)
Write-Host "[3] Writing manifest with forward slashes..."
$manifest = @'
{
    "file_format_version": "1.0.0",
    "api_layer": {
        "name": "XR_APILAYER_MSFS_HandOverlay",
        "library_path": "./MSFSHandOverlay_Layer.dll",
        "api_version": "1.0",
        "implementation_version": "1",
        "description": "MSFS Hand Overlay",
        "functions": {
            "xrNegotiateLoaderApiLayerInterface": "xrNegotiateLoaderApiLayerInterface"
        },
        "disable_environment": "DISABLE_XR_APILAYER_MSFS_HandOverlay"
    }
}
'@
$manifest | Out-File -FilePath "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" -Encoding UTF8 -Force

Write-Host "[4] Verifying..."
Get-Content "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json" | Select-String "library_path"

Write-Host "`n[OK] Fixed! Now test:" -ForegroundColor Green
Write-Host "  del C:\Temp\MSFS*.log"
Write-Host "  Launch MSFS"
Read-Host "`nPress Enter to exit"
