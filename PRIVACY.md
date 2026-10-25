# GoSurvey Privacy Policy

**Effective date: October 25, 2026**

This policy describes information handled by the GoSurvey Windows application and the GoSurvey website at [gosurveycad.com](https://gosurveycad.com/). It reflects the current application and site implementation.

## Information the desktop application sends

### Usage data

GoSurvey sends a small usage ping over HTTPS on a background thread. The first ping for an installation is marked `install`; later launches are marked `active`. Each ping contains a randomly generated installation ID, event type, GoSurvey version, release channel, and operating system. It contains no email address or other account identity, user name, computer name, local path, drawing information, or hardware fingerprint. The ping never blocks the app. If it cannot reach the service, nothing is shown and the app continues normally. There is no in-app setting to turn it off.

The installation ID is generated randomly and is not derived from your computer, hardware, user name, or drawing files. The ping is automatic; GoSurvey currently has no in-app setting to turn it off. The application does not include drawing contents, drawing names, or drawing paths in this usage ping.

The ping is received by a GoSurvey-operated Cloudflare Worker and stored in a Cloudflare D1 database. The database stores the fields above along with a server timestamp, date, and country-level location derived by Cloudflare. The Worker does not store the request IP address, and request logging is disabled in its current configuration. Cloudflare still processes network request metadata, which can include an IP address, to deliver the service.

### Startup and crash diagnostics

If GoSurvey fails during startup or encounters a crash, it automatically attempts to send a diagnostic report to a separate route on the same GoSurvey-operated Cloudflare Worker. The application does this when it can reach the internet; there is no in-app opt-out setting. A report can include the GoSurvey version and release channel, the failure type, technical error details, stack addresses and module paths, and the local path of the diagnostic log. A local path can contain your Windows account name or other machine-specific directory names. Reports are limited to 16 KB before sending.

Startup and crash reports are stored in separate tables in the Cloudflare D1 database. The application also appends startup and crash details to local log files in its GoSurvey user-data folder. Review diagnostic text before sharing it yourself through GitHub; the app's “Send This Report” action opens a prefilled GitHub issue for you to review and submit.

For a runtime crash, GoSurvey also attempts to create a stack-focused Windows minidump. A minidump is a snapshot of some application memory and can include parts of an open drawing or other personal information that happened to be in memory. When available and no larger than 10 MB, the dump is uploaded automatically with the text report. If it cannot be created or is larger than 10 MB, GoSurvey still sends the text report without the dump. Uploaded dump files are kept in private Cloudflare R2 storage and expire after 30 days; the text report remains in D1 under the current database retention configuration.

## Information kept on your device

GoSurvey keeps application preferences and the random installation ID in a local `gosurvey-user.json` file under your GoSurvey user-data folder. Previous GoSurvey versions may have saved a sign-in refresh token in Windows Credential Manager; the current application no longer reads or uses it. Startup and crash diagnostic logs are stored locally in the user-data folder. Your drawings are saved to locations you choose; ordinary local editing does not upload drawing files to GoSurvey. If you use the online map feature, GoSurvey requests map tiles from the USGS National Map service. Those requests reveal the requested map area to that service and may be subject to its own logging and privacy practices. Retrieved tiles may be cached locally.

Earlier GoSurvey versions also offered account sign-in and sent account details to a separate tier lookup service. The current application no longer signs in or sends new account lookups. Account records already submitted by earlier versions may remain in that service; the project has no confirmed automatic deletion schedule for those records. Contact the address below with a request about GoSurvey-managed records.

## Website and update services

The GoSurvey website is hosted by Cloudflare. The page requests release information from GitHub's public API and loads web fonts from Google Fonts. Cloudflare, GitHub, and Google receive the network information needed to provide those services, such as your IP address and browser request data, and handle it under their own privacy terms.

The desktop application checks GitHub Releases for update information and downloads an installer from GitHub when you choose to update. GitHub receives the request information needed to provide those services. GoSurvey does not receive your drawing files through update checks.

## Retention and requests

The current telemetry database configuration does not define an automatic deletion schedule. Records remain in the Cloudflare D1 database until the operator deletes them or removes the database. Local preferences and diagnostic logs remain on your device until you delete them. GitHub, Google Fonts, Cloudflare, and the USGS may retain information according to their own policies and settings.

For privacy questions or requests about GoSurvey-managed records, contact [chet@gosurveycad.com](mailto:chet@gosurveycad.com).

## Changes to this policy

This policy may be updated when GoSurvey's data handling changes. The effective date at the top will be revised when a new version is published.
