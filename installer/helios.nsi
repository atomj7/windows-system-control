Unicode True
Name "Helios"
OutFile "Helios-Setup.exe"
InstallDir "$LOCALAPPDATA\Helios"
RequestExecutionLevel user
SetCompressor /SOLID lzma
InstallDirRegKey HKCU "Software\Helios" "InstallDir"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Helios"
  ExecWait '"$SYSDIR\taskkill.exe" /F /IM helios.exe /T'
  Sleep 500
  SetOutPath "$INSTDIR"
  File "payload\helios.exe"
  File "payload\WebView2Loader.dll"
  File "payload\Helios.cmd"
  File /r "payload\ui"

  CreateDirectory "$SMPROGRAMS\Helios"
  CreateShortCut "$SMPROGRAMS\Helios\Helios.lnk" "$INSTDIR\helios.exe" "--hardware" "$INSTDIR\helios.exe" 0 SW_SHOWNORMAL
  CreateShortCut "$DESKTOP\Helios.lnk" "$INSTDIR\helios.exe" "--hardware" "$INSTDIR\helios.exe" 0 SW_SHOWNORMAL

  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Helios" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "DisplayName" "Helios"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "Publisher" "Helios"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "DisplayVersion" "0.3.2"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios" "NoRepair" 1
SectionEnd

Section "Uninstall"
  ExecWait '"$SYSDIR\taskkill.exe" /F /IM helios.exe /T'
  Sleep 400
  Delete "$DESKTOP\Helios.lnk"
  Delete "$SMPROGRAMS\Helios\Helios.lnk"
  RMDir "$SMPROGRAMS\Helios"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Helios"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Helios"
SectionEnd
