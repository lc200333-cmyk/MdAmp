param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
# The GUI subsystem has no reliable console on all Windows runners. Keep the
# native test exit code visible in CTest logs without changing its assertions.
$probe = Start-Process -FilePath $Executable -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru -Wait
Write-Host ('MdAmp native window smoke exit: ' + $probe.ExitCode)
exit $probe.ExitCode
