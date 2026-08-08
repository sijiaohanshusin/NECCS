param(
    [string]$Port = "COM9",
    [int]$Baud = 115200
)

$serial = [System.IO.Ports.SerialPort]::new($Port, $Baud, "None", 8, "One")
$serial.ReadTimeout = 200

try {
    $serial.Open()
    Write-Host "Serial monitor: $Port @ $Baud (Ctrl+C to stop)"
    while ($true) {
        $text = $serial.ReadExisting()
        if ($text) {
            Write-Host -NoNewline $text
        }
        Start-Sleep -Milliseconds 20
    }
}
finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}
