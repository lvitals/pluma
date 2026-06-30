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

status=$(LC_ALL=C git -C "$repo" status --porcelain=v2 --branch --untracked-files=all)
printf '%s\n' "$status" | grep -F '1 .M '
printf '%s\n' "$status" | grep -F '? untracked.txt'

git -C "$repo" add -- "tracked file.txt" untracked.txt
status=$(LC_ALL=C git -C "$repo" status --porcelain=v2 --branch --untracked-files=all)
printf '%s\n' "$status" | grep -F '1 M. '
printf '%s\n' "$status" | grep -F '1 A. '

git -C "$repo" reset -q HEAD -- "tracked file.txt"
git -C "$repo" commit -qm add-untracked
git -C "$repo" restore --worktree -- "tracked file.txt"
test -z "$(git -C "$repo" status --porcelain)"

git -C "$repo" branch feature
git -C "$repo" tag v1-test
git -C "$repo" log --oneline -1 | grep -F 'add-untracked'
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
