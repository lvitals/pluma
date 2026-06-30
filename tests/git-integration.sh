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

printf 'stash me\n' >> "$repo/tracked file.txt"
git -C "$repo" stash push -qm test-stash
git -C "$repo" stash list | grep -F test-stash
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

remote="${repo}-remote.git"
git init --bare -q "$remote"
git -C "$repo" remote add origin "$remote"
git -C "$repo" push -qu origin master
git -C "$repo" remote -v | grep -F origin
rm -rf "$remote"
