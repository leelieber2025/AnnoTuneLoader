' Double-click this file. Windows PowerShell draws the window. Nothing else is installed.
Set shell = CreateObject("Wscript.Shell")
Set files = CreateObject("Scripting.FileSystemObject")
folder = files.GetParentFolderName(WScript.ScriptFullName)
script = folder & "\loader.ps1"
powershell = shell.ExpandEnvironmentStrings("%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe")

If Not files.FileExists(script) Then
    MsgBox "Put loader.ps1 in the same folder as AnnoTuneLoader.vbs.", 16, "AnnoTune Loader"
    WScript.Quit 1
End If
If Not files.FileExists(powershell) Then
    MsgBox "Windows PowerShell was not found.", 16, "AnnoTune Loader"
    WScript.Quit 1
End If

command = """" & powershell & """ -STA -NoProfile -ExecutionPolicy Bypass -File """ & script & """"
shell.Run command, 1, True
