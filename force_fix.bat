@echo off
echo Forcing manifest fix...
powershell -Command "Start-Process powershell -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -Command \"& {$m=@'
{
    \"file_format_version\": \"1.0.0\",
    \"api_layer\": {
        \"name\": \"XR_APILAYER_MSFS_HandOverlay\",
        \"library_path\": \"C:\\ProgramData\\OpenXR\\1\\api_layers\\explicit.d\\MSFSHandOverlay_Layer.dll\",
        \"api_version\": \"1.0\",
        \"implementation_version\": \"1\",
        \"description\": \"MSFS Hand Overlay\",
        \"functions\": {
            \"xrNegotiateLoaderApiLayerInterface\": \"xrNegotiateLoaderApiLayerInterface\"
        },
        \"disable_environment\": \"DISABLE_XR_APILAYER_MSFS_HandOverlay\"
    }
}
'@; $m | Out-File -FilePath 'C:\ProgramData\OpenXR\1\api_layers\explicit.d\MSFSHandOverlay_api_layer.json' -Encoding UTF8 -Force; Write-Host 'Fixed!'; Read-Host 'Press Enter'}\"'"
