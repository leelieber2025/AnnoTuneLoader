' Double-click this file. Windows PowerShell draws the window. Nothing else is installed.
Set shell = CreateObject("Wscript.Shell")
Set files = CreateObject("Scripting.FileSystemObject")
folder = files.GetParentFolderName(WScript.ScriptFullName)
command = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File """ & folder & "\loader.ps1"""
shell.Run command, 0, False
