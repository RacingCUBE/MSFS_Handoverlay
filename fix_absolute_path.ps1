# Fix manifest to use absolute path
$manifest = @'
{
    "file_format_version": "1.0.0",
    "api_layer": {
        "name": "XR_APILAYER_MSFS_HandOverlay",
        "library_path": "C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_Layer.dll",
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
Write-Host "Fixed! Now test MSFS again (with MSFSHandOverlay.exe running)"
Read-Host "Press Enter"
