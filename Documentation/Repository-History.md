# Repository history and release metadata

## November 2026 privacy cleanup

The diagnostics capture directory was removed from every published branch and
tag, including historical versions. Removing it only from the current tree
did not remove a device identifier from older commits. The prior repository
is retained privately for recovery; a clean public repository uses this same
URL so old cached repository objects are not carried into the replacement.

Application source, upstream attribution and license notices are preserved.
Older release binaries and their checksums are retained unchanged. Those
binaries may report a pre-cleanup commit identifier; their release notes point
to the corresponding sanitized source tag. Version fixed.8 and later identify
their sanitized source directly in the executable and build manifest.

Existing clones should be replaced with a fresh clone after preserving any
unpublished work. Do not merge old history back into the public repository.
Private recovery archives are not part of the published source or releases.
This cleanup cannot revoke copies already downloaded by third parties.

## Earlier identity cleanup

On 2026-09-13, this fork's commit author and committer fields were replaced with
the maintainer's public handle and GitHub no-reply email. The `main` branch and
both existing release tags were rewritten. Upstream source attribution and
license notices were preserved.

All application source and regression tests are unchanged by this cleanup.
Documentation references were updated to the rewritten history. Windows
packages are rebuilt from the replacement release tags so their embedded Git
revision and `BUILD-MANIFEST.json` identify the published source correctly.
Use each release's current SHA256 file to verify its download. The dependency
source archive is unchanged.

The hardware acceptance record refers to the originally installed executable
and retains its checksum. A metadata rebuild changes the executable's revision
string and checksum; it does not constitute another physical hardware test.
Existing installations retain the same fixes and do not need an application
update solely because Git author metadata changed.

## Repository migration

On 2026-09-14, the sanitized history, both release tags, and all release assets
were copied to a new independent GitHub repository. The previous repository
was deleted, and the replacement was renamed to retain the same public URL.
The release files were verified byte for byte; their checksums and source
revisions are unchanged by this migration.

Historical GitHub Actions run records cannot be transferred between
repositories. Their logs and build artifacts were archived separately before
deletion. Release notes retain the validation results, while subsequent CI
runs belong to the replacement repository.

## Existing clones

Commit identifiers and the two release-tag targets changed. For a clone with
no unpublished work, use a fresh clone. Preserve unpublished work separately
and apply only its intended changes to the new history. Do not merge or push
the old branch or tags back into this repository: that would restore the old
metadata.

The rewrite removes the old identities from the published branch and tags.
It cannot erase copies already downloaded by others or guarantee removal of
GitHub's cached commit views. See [GitHub's history-cleanup guidance](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository).
