Set files = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
project = files.GetParentFolderName(WScript.ScriptFullName)
exe = files.BuildPath(project, "app\ICEPUNK.exe")
If files.FileExists(exe) Then
    shell.CurrentDirectory = project
    shell.Run Chr(34) & exe & Chr(34), 1, False
Else
    MsgBox "Build the application first. See prototype\README.md.", 48, "ICEPUNK"
End If
