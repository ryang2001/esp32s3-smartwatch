# Enable Windows Mobile Hotspot (2.4GHz) and print SSID/passphrase/state
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

$profile = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]::GetInternetConnectionProfile()
if (-not $profile) { Write-Output "ERROR: no internet connection profile"; exit 1 }

$mgr = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]::CreateFromConnectionProfile($profile)
$cfg = $mgr.GetCurrentAccessPointConfiguration()

Write-Output ("SSID:       " + $cfg.Ssid)
Write-Output ("Passphrase: " + $cfg.Passphrase)
Write-Output ("State:      " + $mgr.TetheringOperationalState)
Write-Output ("ClientCount:" + $mgr.ClientCount)

# Try to force 2.4GHz band (Windows 11 newer builds only; ignore if unsupported)
try {
    $bandProp = $cfg.PSObject.Properties['Band']
    if ($bandProp) {
        Write-Output ("Band(now):  " + $cfg.Band)
        # TetheringWiFiBand: 0=Auto 1=WiFi2_4GHz 2=WiFi5_0GHz
        if ($cfg.Band -ne 1) {
            $cfg.Band = 1
            Await ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]::ConfigureAccessPointAsync($cfg)) ([Object]) | Out-Null
            Write-Output "Band set to 2.4GHz"
        }
    } else {
        Write-Output "Band: (property not available on this build - set it manually in Settings if hotspot is 5GHz)"
    }
} catch { Write-Output ("Band config failed: " + $_.Exception.Message) }

if ($mgr.TetheringOperationalState -ne 'On') {
    $op = Await ($mgr.StartTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
    Write-Output ("StartTethering: status=" + $op.Status + " additionalErrorMessage=" + $op.AdditionalErrorMessage)
} else {
    Write-Output "Hotspot already ON"
}
