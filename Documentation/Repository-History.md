# Repository history and release metadata

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
