# GoSurvey Windows installer exit codes

GoSurvey's Windows installer is built with Inno Setup. It uses Inno Setup's standard
process exit codes; it does not define custom codes for individual Microsoft Store
installation scenarios.

| Exit code | Meaning |
| ---: | --- |
| `0` | Setup completed successfully. |
| `1` | Setup failed to initialize. |
| `2` | The user cancelled before installation started. |
| `3` | A fatal error occurred while preparing to install. |
| `4` | A fatal error occurred during installation. |
| `5` | The user cancelled during installation, or selected Abort in an Abort/Retry/Ignore dialog. |
| `6` | Setup was terminated by the debugger. |
| `7` | Setup determined that installation cannot proceed. |
| `8` | Setup determined that installation cannot proceed until the system is restarted. |

Any nonzero code means setup did not complete. Codes `6` and codes not listed here are not
expected during normal Microsoft Store installation. Inno Setup may add codes in future versions.

## Microsoft Store scenario mapping

Partner Center's standard scenarios are broader than the information Inno Setup returns. The
installer does not have a distinct exit code for every scenario, so do not assign fabricated or
duplicate codes to make the list appear complete.

| Partner Center scenario | Inno Setup code(s) | Notes |
| --- | ---: | --- |
| Installation cancelled by user | `2`, `5` | Cancellation before setup starts versus during installation. |
| Application already exists | — | The installer supports upgrading an existing GoSurvey installation; it does not return a separate “already exists” code. |
| Installation already in progress | — | There is no dedicated Inno Setup exit code for this condition. |
| Disk space is full | `4` | A fatal installation error; Inno Setup does not identify disk-full separately in its process exit code. |
| Reboot required | — | Code `8` means setup cannot proceed until restart, not that a completed installation requires restart. |
| Network failure | — | The installer bundles its runtime dependency and does not download files during installation. |
| Package rejected during installation | — | Windows security policy may block setup before it can return a process exit code. |
| Installation successful | `0` | Setup completed successfully. |

For complete code semantics, see [Inno Setup's Setup Exit Codes documentation](https://jrsoftware.org/ishelp/topic_setupexitcodes.htm).
