# Issue tracker: GitHub

Issues and specs for this repo live in GitHub Issues. Use the `gh` CLI from this checkout; the repository is `chetjones003/GoSurvey`.

- Create: `gh issue create --title "..." --body-file <path> --label ready-for-agent`
- Read: `gh issue view <number> --json number,title,body,labels,comments`
- List: `gh issue list --state open --json number,title,body,labels,comments`
- Comment: `gh issue comment <number> --body "..."`
- Label: `gh issue edit <number> --add-label "..."`

PRs are not an issue-triage surface. Skills that publish a spec should create a GitHub issue and apply `ready-for-agent`.
