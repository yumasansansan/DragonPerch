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


def main() -> int:
    text = WORKFLOW.read_text(encoding="utf-8")

    # Only what is under `jobs:`. The `on:` block above it has keys at the same
    # indentation -- `push:`, `schedule:` -- and they are not jobs.
    _, _, body = text.partition("\njobs:\n")
    if not body:
        print(f"{WORKFLOW}: no jobs: block found", file=sys.stderr)
        return 2

    jobs = set(re.findall(r"^  ([A-Za-z][A-Za-z0-9_-]*):$", body, re.MULTILINE))
    if RELEASE_JOB not in jobs:
        print(f"{WORKFLOW}: no job called {RELEASE_JOB}", file=sys.stderr)
        return 2

    needs_line = re.search(r"^    needs: \[(.*)\]$", body, re.MULTILINE)
    if needs_line is None:
        print(f"{WORKFLOW}: {RELEASE_JOB} has no `needs: [...]` line", file=sys.stderr)
        return 2

    needs = {name.strip() for name in needs_line.group(1).split(",") if name.strip()}
    ungated = jobs - needs - {RELEASE_JOB}

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
