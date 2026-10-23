"""Set the GitHub Actions docs_only output only for verified documentation-only PRs."""

import json
import os
from posixpath import splitext
from urllib.request import Request, urlopen


API_HEADERS = {
    "Accept": "application/vnd.github+json",
    "Authorization": f"Bearer {os.environ['GH_TOKEN']}",
    "X-GitHub-Api-Version": "2022-11-28",
}
ALLOWED_SUFFIXES = {".md", ".mdx", ".rst", ".adoc", ".text"}
PLAIN_TEXT_ROOTS = ("docs/", "spec/", "workshop/", "verification/")


def is_documentation_path(path):
    normalized = path.replace("\\", "/").lower()
    suffix = splitext(normalized)[1]
    if suffix == ".txt":
        return normalized.startswith(PLAIN_TEXT_ROOTS)
    return suffix in ALLOWED_SUFFIXES


def github_get(url):
    request = Request(url, headers=API_HEADERS)
    with urlopen(request) as response:
        return json.load(response)


def classify_docs_only_result():
    labels = json.loads(os.environ["PR_LABELS"])
    if "docs-only" not in labels:
        return False, "the docs-only label is absent"

    repository = os.environ["REPOSITORY"]
    pr_number = os.environ["PR_NUMBER"]
    api_base = f"https://api.github.com/repos/{repository}/pulls/{pr_number}"
    expected_files = int(github_get(api_base)["changed_files"])
    remaining_files = expected_files
    if expected_files == 0 or expected_files > 3000:
        return False, f"changed-file count {expected_files} is outside the supported range 1..3000"

    saw_file = False
    page = 1
    while True:
        files = github_get(f"{api_base}/files?per_page=100&page={page}")
        saw_file = saw_file or bool(files)
        for item in files:
            if not is_documentation_path(item["filename"]):
                return False, f"non-documentation path: {item['filename']}"
            old_path = item.get("previous_filename")
            if old_path and not is_documentation_path(old_path):
                return False, f"rename includes non-documentation path: {old_path}"

        remaining_files -= len(files)
        if len(files) < 100:
            break
        page += 1

    if not saw_file:
        return False, "GitHub returned no changed files"
    if remaining_files != 0:
        listed_files = expected_files - remaining_files
        return False, f"changed-file listing count did not match the PR count ({listed_files} listed, expected {expected_files})"
    return True, "docs-only label and all changed paths validated"


def classify_docs_only():
    """Return only the policy result for callers that do not need diagnostics."""
    return classify_docs_only_result()[0]


def main():
    try:
        docs_only, reason = classify_docs_only_result()
    except Exception as error:  # fail closed: run CI and explain why the label was not trusted
        docs_only = False
        reason = f"classification failed ({type(error).__name__}: {error})"
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"docs_only={str(docs_only).lower()}\n")
    decision = "skip CI" if docs_only else "run CI"
    print(f"Docs-only classification: {docs_only}; {decision}: {reason}")


if __name__ == "__main__":
    main()
