#!/bin/sh
set -eu

repo="${MESON_BUILD_ROOT:-/tmp}/pluma-git-test-$$"
trap 'rm -rf "$repo"' EXIT HUP INT TERM

git init -q "$repo"
git -C "$repo" config user.name "Pluma Test"
git -C "$repo" config user.email "pluma@example.invalid"

printf 'initial\n' > "$repo/tracked file.txt"
git -C "$repo" add -- "tracked file.txt"
git -C "$repo" commit -qm initial

printf 'modified\n' >> "$repo/tracked file.txt"
printf 'untracked\n' > "$repo/untracked.txt"

status=$(LC_ALL=C git -C "$repo" -c core.quotePath=false status --porcelain=v2 --branch --untracked-files=all)
printf '%s\n' "$status" | grep -F '1 .M '
printf '%s\n' "$status" | grep -F '? untracked.txt'

git -C "$repo" add -- "tracked file.txt" untracked.txt
status=$(LC_ALL=C git -C "$repo" -c core.quotePath=false status --porcelain=v2 --branch --untracked-files=all)
printf '%s\n' "$status" | grep -F '1 M. '
printf '%s\n' "$status" | grep -F '1 A. '

git -C "$repo" reset -q HEAD -- "tracked file.txt"
git -C "$repo" commit -qm add-untracked
git -C "$repo" restore --worktree -- "tracked file.txt"
test -z "$(git -C "$repo" status --porcelain)"

# Porcelain v2 must preserve spaces, leading dashes and Unicode names.
printf 'space\n' > "$repo/path with spaces.txt"
printf 'dash\n' > "$repo/-leading-dash.txt"
printf 'unicode\n' > "$repo/café-文.txt"
git -C "$repo" add -- "path with spaces.txt" "-leading-dash.txt" "café-文.txt"
status=$(LC_ALL=C git -C "$repo" -c core.quotePath=false status --porcelain=v2 --branch --untracked-files=all)
printf '%s\n' "$status" | grep -F 'path with spaces.txt'
printf '%s\n' "$status" | grep -F -- '-leading-dash.txt'
printf '%s\n' "$status" | grep -F 'café-文.txt'
git -C "$repo" commit -qm unusual-paths

# A single hunk can be staged and unstaged without affecting the other hunk.
seq 1 20 > "$repo/hunks.txt"
git -C "$repo" add -- hunks.txt
git -C "$repo" commit -qm hunk-base
sed -i '2s/.*/changed-first/' "$repo/hunks.txt"
sed -i '19s/.*/changed-last/' "$repo/hunks.txt"
git -C "$repo" diff -- hunks.txt > "$repo/all.patch"
awk 'BEGIN { h=0 } /^@@/ { h++; if (h == 2) exit } { print }' "$repo/all.patch" > "$repo/first.patch"
git -C "$repo" apply --cached -- "$repo/first.patch"
git -C "$repo" diff --cached -- hunks.txt | grep -F 'changed-first'
if git -C "$repo" diff --cached -- hunks.txt | grep -Fq 'changed-last'; then
  echo "staging one hunk also staged another hunk" >&2
  exit 1
fi
git -C "$repo" apply --cached --reverse -- "$repo/first.patch"
test -z "$(git -C "$repo" diff --cached -- hunks.txt)"
git -C "$repo" apply --reverse -- "$repo/first.patch"
if git -C "$repo" diff -- hunks.txt | grep -Fq 'changed-first'; then
  echo "discarding one hunk did not restore that hunk" >&2
  exit 1
fi
git -C "$repo" diff -- hunks.txt | grep -F 'changed-last'
git -C "$repo" restore -- hunks.txt
rm -f "$repo/all.patch" "$repo/first.patch"

git -C "$repo" branch feature
git -C "$repo" tag v1-test
git -C "$repo" log --oneline -1 | grep -F 'hunk-base'
git -C "$repo" branch --format='%(refname:short)' | grep -Fx feature
git -C "$repo" tag --list | grep -Fx v1-test

# Stage-all-and-commit includes tracked and untracked changes in one commit.
printf 'commit all\n' >> "$repo/path with spaces.txt"
printf 'new file\n' > "$repo/commit-all.txt"
git -C "$repo" add -A
git -C "$repo" commit -qm stage-all-and-commit
test -z "$(git -C "$repo" status --porcelain)"
git -C "$repo" show --format= --name-only HEAD | grep -Fx 'commit-all.txt'

# Amend replaces HEAD instead of creating an additional commit.
commit_count=$(git -C "$repo" rev-list --count HEAD)
printf 'amended\n' >> "$repo/commit-all.txt"
git -C "$repo" add -- commit-all.txt
amend_message=$(printf 'amended-commit\n\namended body')
git -C "$repo" commit --amend -qm "$amend_message"
test "$(git -C "$repo" rev-list --count HEAD)" -eq "$commit_count"
test "$(git -C "$repo" log -1 --format=%s)" = amended-commit
git -C "$repo" log -1 --format=%B | grep -Fx 'amended body'
git -C "$repo" show HEAD:commit-all.txt | grep -Fx amended

# A failed hook must prevent the commit and leave the staged contents recoverable.
printf 'hook failure\n' >> "$repo/commit-all.txt"
git -C "$repo" add -A
printf '#!/bin/sh\nexit 1\n' > "$repo/.git/hooks/pre-commit"
chmod +x "$repo/.git/hooks/pre-commit"
if git -C "$repo" commit -m should-fail >/dev/null 2>&1; then
  echo "pre-commit hook failure did not stop commit" >&2
  exit 1
fi
git -C "$repo" diff --cached --quiet && {
  echo "failed commit did not preserve staged changes" >&2
  exit 1
}
rm -f "$repo/.git/hooks/pre-commit"
git -C "$repo" reset -q HEAD -- .
git -C "$repo" restore -- .

# Sign-off uses Git's configured identity and adds the standard trailer.
printf 'signoff\n' >> "$repo/commit-all.txt"
git -C "$repo" add -- commit-all.txt
git -C "$repo" commit --signoff -qm signed-off-commit
git -C "$repo" log -1 --format=%B | grep -Fx 'Signed-off-by: Pluma Test <pluma@example.invalid>'

# A configured signing failure must not create a commit or lose staged changes.
printf 'signature failure\n' >> "$repo/commit-all.txt"
git -C "$repo" add -- commit-all.txt
signed_head=$(git -C "$repo" rev-parse HEAD)
git -C "$repo" config gpg.program false
if git -C "$repo" commit -S -m should-not-sign >/dev/null 2>&1; then
  echo "signed commit unexpectedly succeeded without a signer" >&2
  exit 1
fi
test "$(git -C "$repo" rev-parse HEAD)" = "$signed_head"
git -C "$repo" diff --cached --quiet && {
  echo "failed signed commit did not preserve staged changes" >&2
  exit 1
}
git -C "$repo" config --unset gpg.program
git -C "$repo" reset -q HEAD -- .
git -C "$repo" restore -- .

printf 'stash me\n' >> "$repo/tracked file.txt"
git -C "$repo" stash push -qm test-stash
git -C "$repo" stash list | grep -F test-stash
git -C "$repo" stash show --patch --stat 'stash@{0}' | grep -F 'stash me'
git -C "$repo" stash pop -q
git -C "$repo" restore -- "tracked file.txt"

git -C "$repo" switch -q feature
printf 'feature\n' > "$repo/tracked file.txt"
git -C "$repo" commit -qam feature-change
git -C "$repo" switch -q master
printf 'master\n' > "$repo/tracked file.txt"
git -C "$repo" commit -qam master-change
if git -C "$repo" merge feature >/dev/null 2>&1; then
  echo "merge fixture did not conflict" >&2
  exit 1
fi
git -C "$repo" status --porcelain=v2 | grep -F 'u UU '
git -C "$repo" merge --abort
test -z "$(git -C "$repo" status --porcelain)"

# File history follows renames and keeps paths with spaces intact.
git -C "$repo" mv -- 'path with spaces.txt' 'renamed path with spaces.txt'
git -C "$repo" commit -qm rename-for-history
file_history=$(git -C "$repo" log --follow --format=%s -- 'renamed path with spaces.txt')
printf '%s\n' "$file_history" | grep -Fx rename-for-history
printf '%s\n' "$file_history" | grep -Fx unusual-paths

# File/reference and two-reference comparisons remain path-safe.
git -C "$repo" rev-parse --verify --quiet --end-of-options 'feature^{commit}' >/dev/null
git -C "$repo" rev-parse --verify --quiet --end-of-options 'v1-test^{commit}' >/dev/null
if git -C "$repo" rev-parse --verify --quiet --end-of-options 'missing-reference^{commit}' >/dev/null; then
  echo "invalid comparison reference unexpectedly resolved" >&2
  exit 1
fi
git -C "$repo" diff HEAD~1 -- 'renamed path with spaces.txt' | grep -F 'space'
git -C "$repo" diff feature master -- 'tracked file.txt' | grep -F 'master'

remote="${repo}-remote.git"
git init --bare -q "$remote"
git -C "$repo" remote add origin "$remote"
git -C "$repo" push -qu origin master
git -C "$repo" remote -v | grep -F origin

# Amending a published commit requires an explicit force-with-lease update.
git -C "$repo" commit --amend -qm published-amend
if git -C "$repo" push origin master >/dev/null 2>&1; then
  echo "normal push unexpectedly accepted rewritten history" >&2
  exit 1
fi
git -C "$repo" push --force-with-lease -q origin master
test "$(git -C "$repo" rev-parse HEAD)" = "$(git --git-dir="$remote" rev-parse refs/heads/master)"
rm -rf "$remote"
