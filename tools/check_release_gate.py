#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that the release job waits for every other job in the workflow.

GitHub Actions has no way to say "needs: everything". A job is gated by the names written
in its `needs:` list and by nothing else, so adding a job and forgetting to add it there
produces a workflow that looks careful and publishes anyway. That happened: the static
analysis job was added, clang-tidy failed, and a nightly release went out regardless,
because the jobs that built the packages had all passed and nothing was watching the one
that had not.

A comment would not have caught it -- there was one. This runs first in the release job and
fails it before anything is published.
"""

import pathlib
import re
import sys

RELEASE_JOB = "nightly"
WORKFLOW = pathlib.Path(".github/workflows/ci.yml")

JOB_HEADING = re.compile(r"^  ([A-Za-z][A-Za-z0-9_-]*):$", re.MULTILINE)


def job_blocks(body: str) -> dict[str, str]:
    """Every job's own text, split at the two-space-indented keys that name them.

    Each job is looked at on its own rather than the file being searched as a whole. The
    first version did the latter -- one regex for `needs:` across everything -- and a job
    that has a `needs:` line of its own placed above the release job was enough to make it
    report on the wrong list entirely. Given a workflow whose release job had quietly lost
    `analyse`, it answered that everything was gated and exited 0.
    """
    headings = [(match.start(), match.group(1)) for match in JOB_HEADING.finditer(body)]
    return {
        name: body[start : headings[index + 1][0] if index + 1 < len(headings) else len(body)]
        for index, (start, name) in enumerate(headings)
    }


def declared_needs(block: str) -> set[str] | None:
    """The job's `needs:`, in either spelling YAML allows, or None if it has none."""
    flow = re.search(r"^    needs: \[(.*)\]$", block, re.MULTILINE)
    if flow is not None:
        return {name.strip() for name in flow.group(1).split(",") if name.strip()}

    listed = re.search(r"^    needs:\n((?:      - .+\n)+)", block, re.MULTILINE)
    if listed is not None:
        return {line.strip(" -\n") for line in listed.group(1).splitlines() if line.strip()}

    single = re.search(r"^    needs: ([A-Za-z][A-Za-z0-9_-]*)$", block, re.MULTILINE)
    if single is not None:
        return {single.group(1)}

    return None


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")

    # Only what is under `jobs:`. The `on:` block above it has keys at the same
    # indentation -- `push:`, `schedule:` -- and they are not jobs.
    _, _, body = text.partition("\njobs:\n")
    if not body:
        print(f"{WORKFLOW}: no jobs: block found", file=sys.stderr)
        return 2

    jobs = job_blocks(body)
    if RELEASE_JOB not in jobs:
        print(f"{WORKFLOW}: no job called {RELEASE_JOB}", file=sys.stderr)
        return 2

    needs = declared_needs(jobs[RELEASE_JOB])
    if needs is None:
        print(f"{WORKFLOW}: {RELEASE_JOB} has no `needs:` line", file=sys.stderr)
        return 2

    unknown = needs - set(jobs)
    if unknown:
        print(
            f"{WORKFLOW}: {RELEASE_JOB} needs {', '.join(sorted(unknown))}, which "
            f"{'is not a job' if len(unknown) == 1 else 'are not jobs'} in this workflow.",
            file=sys.stderr,
        )
        return 2

    ungated = set(jobs) - needs - {RELEASE_JOB}
    if ungated:
        print(
            f"{WORKFLOW}: {RELEASE_JOB} does not wait for: {', '.join(sorted(ungated))}.\n"
            f"Add them to its `needs:` list, or this release can be published while they "
            f"are failing.",
            file=sys.stderr,
        )
        return 1

    print(f"{RELEASE_JOB} waits for all {len(needs)} other job(s): {', '.join(sorted(needs))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
