param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string[]]$AtCommand = @("AT", "AT+GMR", "AT+CWMODE?"),
    [switch]$Interactive,
    [switch]$ListPorts,
    [int]$InitWaitSeconds = 6,
    [int]$ResponseWaitSeconds = 3
)

$ErrorActionPreference = "Stop"

function Get-PortNameFromText([string]$Text) {
    if (-not $Text) {
        return $null
    }
    $match = [regex]::Match($Text, "\(COM\d+\)")
    if ($match.Success) {
        return $match.Value.Trim("(", ")")
    }
    $match = [regex]::Match($Text, "\bCOM\d+\b")
    if ($match.Success) {
        return $match.Value
    }
    return $null
}

function Get-AvailableSerialPorts {
    $ports = Get-CimInstance Win32_PnPEntity |
        Where-Object {
            $_.PNPClass -eq "Ports" -or
            $_.Name -match "COM\d+" -or
            $_.PNPDeviceID -match "VID_303A|VID_1A86|VID_10C4|VID_0403"
        } |
        ForEach-Object {
            $name = Get-PortNameFromText $_.Name
            if (-not $name) {
                $name = Get-PortNameFromText $_.DeviceID
            }
            if ($name) {
                [pscustomobject]@{
                    Port        = $name
                    Name        = $_.Name
                    PNPDeviceID = $_.PNPDeviceID
                    Status      = $_.Status
                }
            }
        }

    return @($ports | Sort-Object Port -Unique)
}

function Resolve-DefaultPort {
    $ports = Get-AvailableSerialPorts
    if ($ports.Count -eq 0) {
        throw "No serial ports were found."
    }

    $preferred = $ports |
        Where-Object {
            $_.Name -match "CH343|USB-Enhanced-SERIAL|USB.*Serial|USB.*SERIAL|Espressif|JTAG|CDC" -or
            $_.PNPDeviceID -match "VID_303A|VID_1A86|VID_10C4|VID_0403"
        } |
        Where-Object {
            $_.Name -notmatch "Bluetooth" -and $_.PNPDeviceID -notmatch "BTHENUM"
        } |
        Select-Object -First 1

    if ($preferred) {
        return $preferred.Port
    }

    $nonBluetooth = $ports |
        Where-Object { $_.Name -notmatch "Bluetooth" -and $_.PNPDeviceID -notmatch "BTHENUM" } |
        Select-Object -First 1

    if ($nonBluetooth) {
        return $nonBluetooth.Port
    }

    return $ports[0].Port
}

function Read-SerialText($Serial, [double]$Seconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    $builder = [System.Text.StringBuilder]::new()

    while ([DateTime]::UtcNow -lt $deadline) {
        try {
            $chunk = $Serial.ReadExisting()
            if ($chunk.Length -gt 0) {
                [void]$builder.Append($chunk)
            }
            else {
                Start-Sleep -Milliseconds 30
            }
        }
        catch [TimeoutException] {
            Start-Sleep -Milliseconds 30
        }
    }

    return $builder.ToString()
}

function Send-AtCommand($Serial, [string]$Command, [int]$WaitSeconds) {
    $trimmed = $Command.Trim()
    if (-not $trimmed) {
        return
    }

    Write-Host ""
    Write-Host ">>> $trimmed"
    $Serial.DiscardInBuffer()
    $Serial.Write("$trimmed`r`n")
    $response = Read-SerialText $Serial $WaitSeconds
    if ($response) {
        Write-Host $response
    }
    else {
        Write-Host "(no response)"
    }
}

if ($ListPorts) {
    Get-AvailableSerialPorts | Format-Table -AutoSize
    exit 0
}

if (-not $Port) {
    $Port = Resolve-DefaultPort
}

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 200
$serial.WriteTimeout = 1000
$serial.DtrEnable = $false
$serial.RtsEnable = $false

try {
    $serial.Open()
    Write-Host "Opened $Port @ $Baud"
    Write-Host "Waiting $InitWaitSeconds second(s) for boot and SDIO AT init..."

    $initial = Read-SerialText $serial $InitWaitSeconds
    if ($initial) {
        Write-Host ""
        Write-Host "--- initial output ---"
        Write-Host $initial
    }

    if ($Interactive) {
        Write-Host ""
        Write-Host "Interactive mode. Type 'exit' or 'quit' to close."
        while ($true) {
            $line = Read-Host "AT"
            if ($line -match "^(exit|quit)$") {
                break
            }
            Send-AtCommand $serial $line $ResponseWaitSeconds
        }
    }
    else {
        foreach ($command in $AtCommand) {
            Send-AtCommand $serial $command $ResponseWaitSeconds
        }
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
