# Windows signing — preparation, not performed

No certificate is included, purchased, or assumed. Builds are unsigned unless explicit signing hooks
are configured. Public distribution should use the publisher's real signing service/certificate.

## Order

1. Build the frozen Python engine and native JUCE executable.
2. Run packaged tests.
3. Sign shipped native PE files: `ICEPUNK.exe`, `engine/icepunk-engine.exe`, engine `.dll`/`.pyd`
   files, and any future VST3 binary. Keep third-party licensing notices in the payload.
4. Inno Setup compiles and signs its generated uninstaller and setup executable.
5. Verify signatures, compute final checksums, and test the signed installer on a clean machine.

Install Windows SDK SignTool on the **build machine**. Example for a certificate already in the
certificate store (substitute the publisher's actual SHA-1 thumbprint and timestamp service):

```powershell
signtool sign /sha1 "CERTIFICATE_THUMBPRINT" /fd SHA256 /tr "https://YOUR-RFC3161-TIMESTAMP-SERVICE" /td SHA256 "path\ICEPUNK.exe"
signtool verify /pa /all /v "path\ICEPUNK.exe"
```

Use `/sm` if the certificate is in the machine store. Hardware/cloud signers may need their own
provider flags. Do not put private keys, token PINs or PFX passwords in this repository or command logs.

## Pipeline hooks

`ICEPUNK_SIGN_SCRIPT` is an optional PowerShell script taking one path. For a directory, sign its
`.exe`, `.dll` and `.pyd` files recursively; for a file, sign that installer. Throw on any failure.
The hook runs on the complete staged payload before Inno compilation and on the installer afterward.

For the generated uninstaller, configure Inno's named signer too. Example environment setup:

```powershell
$env:ICEPUNK_SIGN_SCRIPT = 'D:\signing\Sign-IcePunk.ps1'
$env:ICEPUNK_INNO_SIGN_COMMAND = '"C:\path\signtool.exe" sign /sha1 THUMBPRINT /fd SHA256 /tr https://TIMESTAMP-SERVICE /td SHA256 $f'
.\build_windows.bat
```

The single-quoted string preserves Inno's literal `$f` placeholder. The build script supplies
`/Sicepunk=...` and enables `SignedUninstaller=yes` only when this variable is set. A hook alone does
not sign the uninstaller. For a payload-only hook plus Inno signing, make the hook verify/skip already
signed setup files rather than adding an unintended second signature.

Without either hook, compilation remains possible for local testing, and no signing claim is made.
Code signing does not guarantee immediate SmartScreen reputation; test the real download/install flow.

References: [Microsoft SignTool](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/signtool),
[Inno SignTool directive](https://jrsoftware.org/ishelp/topic_setup_signtool.htm).
