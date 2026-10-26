# Private release PDB archive

The release workflow archives the full PDB from the same build as the shipped executable. It stores
the file in the private `gosurvey-crash-dumps` R2 bucket under `symbols/`, separate from the
`crash-dumps/` prefix used for uploaded user crash dumps. PDBs are not included in installer inputs,
GitHub release assets, Pages assets, or downloaded by GoSurvey.

## GitHub Actions setup

Add these repository Actions secrets before running a beta or new stable release:

- `CLOUDFLARE_ACCOUNT_ID`
- `R2_ACCESS_KEY_ID`
- `R2_SECRET_ACCESS_KEY`

Create an R2 API token with **Object Read & Write** access limited to the `gosurvey-crash-dumps`
bucket. Use that token's S3 access key ID and secret key. The workflow fails on an eligible release
if a secret is missing or the PDB cannot be validated or uploaded. Feature-branch builds and stable
runs blocked by the existing version gate do not archive.

The object key is:

```text
symbols/<version>/<12-character-commit>/<32-hex-PDB-GUID>,<age>.pdb
```

The version and PDB identity match the `version:`, `commit:`, and `pdb:` fields in GoSurvey crash
reports. The workflow compares the executable's embedded CodeView GUID/age with the PDB before
uploading it. The PDB archive is not subject to the 30-day `crash-dumps/` lifecycle rule.

## Retrieve a PDB for crash triage

Use the report's `version:`, `commit:`, and `pdb:` values with an authorized, bucket-scoped R2 S3
credential. Keep the credentials in environment variables or a secure local AWS profile; do not put
them in command history or commit them.

```powershell
$env:AWS_ACCESS_KEY_ID = '<bucket-scoped R2 access key>'
$env:AWS_SECRET_ACCESS_KEY = '<bucket-scoped R2 secret key>'
aws s3 cp `
  's3://gosurvey-crash-dumps/symbols/<version>/<commit>/<GUID>,<age>.pdb' `
  '.\GoSurvey.pdb' `
  --endpoint-url 'https://<CLOUDFLARE_ACCOUNT_ID>.r2.cloudflarestorage.com' `
  --region auto
```
