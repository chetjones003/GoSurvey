# GoSurvey Privacy Policy

**Effective date: October 22, 2026**

This policy describes information handled by the GoSurvey Windows application and the GoSurvey website at [gosurveycad.com](https://gosurveycad.com/). It reflects the current application and site implementation.

## Information the desktop application sends

### Usage data

GoSurvey sends a small usage ping over HTTPS once on every launch. The first successful ping for an installation is marked `install`; later launches are marked `active`. Each ping contains a randomly generated installation ID, event type, GoSurvey version, release channel, and operating system. When you are signed in, it also contains the email address associated with that signed-in session. When you are signed out, the email field is empty.

The installation ID is generated randomly and is not derived from your computer, hardware, user name, or drawing files. The ping is automatic; GoSurvey currently has no in-app setting to turn it off. The application does not include drawing contents, drawing names, or drawing paths in this usage ping.

The ping is received by a GoSurvey-operated Cloudflare Worker and stored in a Cloudflare D1 database. The database stores the fields above along with a server timestamp, date, and country-level location derived by Cloudflare. The Worker does not store the request IP address, and request logging is disabled in its current configuration. Cloudflare still processes network request metadata, which can include an IP address, to deliver the service.

### Startup and crash diagnostics

If GoSurvey fails during startup or encounters a crash, it automatically attempts to send a diagnostic report to a separate route on the same GoSurvey-operated Cloudflare Worker. The application does this when it can reach the internet; there is no in-app opt-out setting. A report can include the GoSurvey version and release channel, the failure type, technical error details, stack addresses and module paths, and the local path of the diagnostic log. A local path can contain your Windows account name or other machine-specific directory names. Reports are limited to 16 KB before sending.

Startup and crash reports are stored in separate tables in the Cloudflare D1 database. The application also appends startup and crash details to local log files in its GoSurvey user-data folder. Review diagnostic text before sharing it yourself through GitHub; the app's “Send This Report” action opens a prefilled GitHub issue for you to review and submit.

## Sign-in and account information

GoSurvey sign-in is handled by Auth0 in your system browser. You can sign in with Google, Microsoft, or an email and password account configured through Auth0. GoSurvey does not receive or store your password. Auth0 processes sign-in information under its own privacy terms and, for social sign-in, the selected provider's terms.

The application stores the refresh token in Windows Credential Manager. Short-lived access tokens are held in memory. After sign-in, GoSurvey sends an access token and the account email to its account service to look up the account tier. The account service stores Auth0's stable account identifier, email, tier, and account creation time in a separate Cloudflare D1 database. GoSurvey does not currently provide an in-app account deletion control; contact the project using the address below to request help.

## Information kept on your device

GoSurvey keeps application preferences and the random installation ID in a local `gosurvey-user.json` file under your GoSurvey user-data folder. Sign-in refresh credentials are kept separately in Windows Credential Manager. Startup and crash diagnostic logs are stored locally in that folder. Your drawings are saved to locations you choose; ordinary local editing does not upload drawing files to GoSurvey. If you use the online map feature, GoSurvey requests map tiles from the USGS National Map service. Those requests reveal the requested map area to that service and may be subject to its own logging and privacy practices. Retrieved tiles may be cached locally.

## Website and update services

The GoSurvey website is hosted by Cloudflare. The page requests release information from GitHub's public API and loads web fonts from Google Fonts. Cloudflare, GitHub, and Google receive the network information needed to provide those services, such as your IP address and browser request data, and handle it under their own privacy terms.

The desktop application checks GitHub Releases for update information and downloads an installer from GitHub when you choose to update. GitHub receives the request information needed to provide those services. GoSurvey does not receive your drawing files through update checks.

## Retention and requests

The current telemetry and account database configurations do not define an automatic deletion schedule. Records remain in their respective Cloudflare D1 databases until the operator deletes them or removes the database. Local preferences and diagnostic logs remain on your device until you delete them. Auth0, GitHub, Google, Cloudflare, and the USGS may retain information according to their own policies and settings.

For privacy questions or requests about GoSurvey-managed records, contact [chet@gosurveycad.com](mailto:chet@gosurveycad.com).

## Changes to this policy

This policy may be updated when GoSurvey's data handling changes. The effective date at the top will be revised when a new version is published.
