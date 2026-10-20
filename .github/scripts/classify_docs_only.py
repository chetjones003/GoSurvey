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


def classify_docs_only():
    labels = json.loads(os.environ["PR_LABELS"])
    if "docs-only" not in labels:
        return False

    repository = os.environ["REPOSITORY"]
    pr_number = os.environ["PR_NUMBER"]
    api_base = f"https://api.github.com/repos/{repository}/pulls/{pr_number}"
    remaining_files = int(github_get(api_base)["changed_files"])
    if remaining_files == 0 or remaining_files > 3000:
        return False

    saw_file = False
    page = 1
    while True:
        files = github_get(f"{api_base}/files?per_page=100&page={page}")
        saw_file = saw_file or bool(files)
        for item in files:
            if not is_documentation_path(item["filename"]):
                return False
            old_path = item.get("previous_filename")
            if old_path and not is_documentation_path(old_path):
                return False

        remaining_files -= len(files)
        if len(files) < 100:
            break
        page += 1

    return saw_file and remaining_files == 0


def main():
    docs_only = classify_docs_only()
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
        output.write(f"docs_only={str(docs_only).lower()}\n")
    print(f"Validated docs-only classification: {docs_only}")


if __name__ == "__main__":
    main()
