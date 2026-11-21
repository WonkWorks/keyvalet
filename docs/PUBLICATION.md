# Public source and firmware publication preparation — GH3

Copyright © 2026 Evan W. Cash (WonkWorks). KeyValet-owned documentation is
GPL-3.0-or-later; see [LICENSE](../LICENSE). This is an engineering compliance
record, not legal advice. **GH3 preparation: READY WITH CONDITIONS; public distribution is not yet approved.**

## Identity and preserved evidence

KeyValet — An open-source project by WonkWorks. Each association independently
deploys TowneCore and owns its operational trust, credentials and device setup.
Public firmware downloads require no GitHub authentication, OAuth, service
account, GCS bucket or access to a developer's Google Cloud project.

| Concept | Preserved value |
| --- | --- |
| Physical hardware | PCB v0.4.2 / ESP32-C6-WROOM-1-N8, 8 MiB |
| Embedded firmware version | `0.5.0-m5` from `version.txt`; unchanged development identity |
| Accepted M11C source checkpoint | `be41564249abb3e410a2db4d57d31e7b64dd5724` |
| P0 artifact checkpoint | `c5df4bcc5a49599e25da67f4aad990de40666c1e` |
| Distribution identity | `keyvalet-m11c-p0-closeout-be41564249ab` |

The nine [approved artifacts](../releases/keyvalet-m11c-p0-closeout-be41564249ab/)
are unchanged. Their `release.json` records the honest pre-checkpoint dirty build
identity and the distinction between accepted repair functionality and pending
closeout-image physical flashing acceptance. Do not relabel or rebuild them to
make their version or Git descriptor look newer. See [factory installation](FACTORY_INSTALLATION.md).

## Binary-only dependencies: adopted additional permission

The completed licensing investigation verified six Espressif object archives in
retained application memory sections: `libnet80211.a`, `libpp.a`, `libcore.a`,
`libespnow.a`, `libmesh.a` and `libphy.a`. Their ESP32-C6 target and exact upstream
revisions are identified in [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md).
Other installed radio archives are not automatically incorporated or covered.

The pinned Wi-Fi/PHY terms permit Apache-2.0 Object-form redistribution but do
not supply implementation source. Apache-2.0 source is
[GPLv3-compatible](https://apache.org/licenses/GPL-compatibility.html); that fact
alone does not resolve the combined image's Corresponding Source obligations.
No applicable upstream GPL permission or proven System Library exclusion was
established. The [Espressif maintainer discussion](https://github.com/espressif/esp32-wifi-lib/issues/1)
explicitly distinguishes object redistribution from GPL source obligations.

[LICENSE-EXCEPTION.md](../LICENSE-EXCEPTION.md) is the reviewed narrow GPLv3
Section 7 additional permission, explicitly adopted by the copyright holder on
November 21, 2026. **Adoption is not legal certification or publication approval.**
It addresses rights over KeyValet-owned material only; it does not grant permission for third parties,
relicense vendor objects, or make their unavailable implementation source open
source. The assembled image must not be described as wholly GPL-licensed or
wholly source-available.

The adopted permission identifies libraries by upstream project, revision, ESP32-C6 target
and archive name rather than incidental SDK install paths. Modified KeyValet
firmware can use the specified vendor inputs; other targets, revisions, archives
or modified vendor inputs are outside the adopted grant. Normal linker
relocation/section selection is included. No arbitrary closed-source dependency
exception is granted.

The holder confirmed authority over the KeyValet-owned material and explicitly
adopted the exact reviewed permission. The narrow waiver does not settle release
compliance with every other component's terms. Third-party notices and complete
required KeyValet source/build/install
materials and any applicable Installation Information remain necessary. New
contributors need not extend the permission to their contributions; do not
assume it overrides another holder's GPL conditions. This is an adopted licensing policy,
not a legal compliance certification. No new System Library conclusion is needed
or asserted by the permission.

References: [GPLv3 §7](https://www.gnu.org/licenses/gpl-3.0.html#section7) and
[GNU additional-permission guidance](https://www.gnu.org/licenses/gpl-faq.en.html#GPLIncompatibleLibs).
The standardized GPL text and existing third-party notices remain unmodified.

## Corresponding-source and installation delivery

Under the adopted permission, only the specified libraries' unavailable
implementation source is excused under the holder's KeyValet GPL conditions.
No source requirement for any other incorporated component is waived. Adoption
alone does not complete release/source-delivery review.

Before binary publication, provide equally accessible source for the actual
approved application and bootloader, including owned sources, the local HTTPS
component patch, CMake/build-identity/packaging scripts, partition table, SDK
configuration and all required nonexcluded dependency sources. Identify the
SDK/toolchain versions and submodule revisions, retain notices and maintain
source availability alongside the binary. A bare project source ZIP or generic
upstream link is not a completed source-delivery arrangement.

The exact nonsecret production configuration is now preserved separately in
[`releases/source/keyvalet-m11c-p0-closeout-be41564249ab/`](../releases/source/keyvalet-m11c-p0-closeout-be41564249ab/README.md).
Its README identifies tracked materials, external source locations, verification
results and remaining delivery conditions. `source.json` pins the SDK/toolchain,
all SDK submodules, configuration digest and 88 project/build-input hashes.
All 35 project compilation inputs match accepted M11C; 738 SDK application and
103 SDK bootloader inputs match their pinned Git objects. The SDK's public CA
bundle and normal generated translation units are recreatable inputs, not device
certificates or private commissioning material. No required local source/config
input was found missing; no firmware was rebuilt.

Do not publish the whole build directory, ELF/map/object files, local credentials,
flash dumps or installed toolchain internal configuration. The latter contains
credential-bearing internal URLs and is not a publication artifact; this source
record includes only nonsecret version/provenance fields and public upstream
references. Generated files recreatable from source need not be copied. Exact
timestamp/debug-path-dependent binary hashes are not a clean-build promise.

Before distribution, assemble and review an equally accessible source/notices
arrangement for application and bootloader, including all applicable upstream
source/header notices and pinned runtime sources. A clear maintained source
retrieval arrangement may use upstream hosting under GPLv3 §6(d); keep source
available for as long as binaries are offered. Verify those downloads and review
the final public tree/release sidecars. The checked-in source record is not a
claim that a public corresponding-source package is already hosted or that every
legal obligation has been independently certified.

For a qualifying GPLv3 User Product transaction, supply Installation Information
needed to install and execute modified firmware. The existing USB ROM loader,
disabled Secure Boot/flash encryption baseline, partition/write plans, physical
controls and independent provisioning are the intended user-modifiable model.
Recipients must be able to use their own firmware and association-owned trust;
no publisher authorization key or proprietary service should be required.
Do not publish operational private keys. See [build/install instructions](FACTORY_INSTALLATION.md)
and [GPLv3 §6](https://www.gnu.org/licenses/gpl-3.0.html).

## GH3 current-tree preparation

The four SDK-dependent host tests now require `IDF_PATH` from the existing
ESP-IDF environment, without a workstation fallback. They diagnose missing SDK
sources, host compiler and (for TLS identity) OpenSSL and host PIC mbedTLS
libraries. Activate the pinned SDK as described in the factory guide; this is
not a second SDK installer. For example, after SDK activation:

```sh
python3 -B tests/config_http_test.py
python3 -B tests/journal_json_test.py
python3 -B tests/ws_receive_test.py
python3 -B tests/tls_identity_test.py --mbedtls-build /tmp/keyvalet-m11c-mbedtls
```

The last argument is a host-test dependency directory, not firmware. If absent,
prepare host libraries from the pinned SDK's mbedTLS sources, not a replacement
SDK or firmware build (choose an unused output directory):

```sh
cmake -S "$IDF_PATH/components/mbedtls/mbedtls" -B /tmp/keyvalet-m11c-mbedtls \
  -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake --build /tmp/keyvalet-m11c-mbedtls
```

The tracked-tree audit includes sources, documentation, fixtures, notices, the
nine release assets and corresponding-source record. A residual bench SSID was
replaced in two tests with `example-network`. Remaining private-range addresses,
example domains, dummy passwords and TLS rejection strings in tests are synthetic
fixtures, not operational setup. Immutable binaries contain PEM parser markers;
these are not stored device keys or certificate objects. Engineering hashes,
original dirty descriptors, historical build-directory names and legal attribution
are deliberately retained. Historical build commands are evidence, not required
private resources for a fresh clone; use the portable factory/source guide.

Ignored credentials, dumps, local headers, SDK/build directories and backup files
are outside the candidate tracked tree. `.gitignore` is not a history scrubber or
an authorization to publish an unfiltered workspace. This audit does not certify
untracked files, unreachable Git objects, installed tools or the private history
as public-safe. No firmware source/configuration or accepted binary is changed.

## One-time public-history transition — future authorization only

**Do not execute this procedure during GH3 preparation.** A later local GH3
checkpoint and explicit history-transition approval are required. Repository
creation, pushes, tags and Releases require separate publication authorization.
Run each gated block separately; stop on any failed command or unexpected output.
Commands assume Bash and the existing KeyValet checkout. No second repository,
active export branch, backup service or synchronization process is needed.

### 1. Preconditions and private retention location

There will be one ongoing development codebase: this checkout on WonkServ,
using public `main` after transition and GitHub for source and versioned firmware
Releases. The hardware branch's ancestry contains private operational identifiers;
a clean tip does not hide its parents. Original M11C/P0/GH2 commit IDs remain
private provenance, not publicly resolvable ancestry. A hash in a document does
not create Git reachability.

Before running any write below:

- Review and commit the authorized GH3 changes on `hardware/keyvalet-v0.4.2` under
  separate checkpoint approval. Require an empty `git status --porcelain=v1`,
  including no untracked publication inputs. Re-run the offline tests, tracked-tree
  sensitive-data audit, all nine artifact comparisons and source-input hash checks.
- Approve the exact snapshot SHA/tree and public author **and committer** identity.
  Do not inherit a private configured email. Replace the email placeholder below
  only with the explicitly approved public address; do not use a fabricated one.
- Inspect `git for-each-ref` and `git remote -v`. Expected heads are the hardware
  branch and `master`; no `main`, tags, remote or `refs/private/keyvalet/*` exists.
  Investigate any difference instead of overwriting refs. Unrelated local tool
  refs are not publication inputs and must never be pushed.
- Reconfirm original M11C/P0/GH2 objects and all nine assets. Do not rebuild,
  regenerate manifests or relabel the accepted package.

The designated bundle directory is
`/home/ecash/dev/keyvalet/backups/private-git-history/`, within the existing
WonkServ development hierarchy and excluded by `/backups/` in `.gitignore`.
It is private recovery storage, **never a GitHub asset**. On November 21, 2026,
the operator confirmed direct inspection of
`/home/ecash/.config/backrest/config.json`: the configured system backup plan
includes `/home/ecash` and excludes `.cache`, `.vscode-server`, `.espressif`,
`go/pkg`, `dev/*/build` and `**/node_modules`. The designated directory is within
that scope and is not excluded: **verified configured coverage** under WonkServ's
existing backup lifecycle. This does **not** establish that a completed backup
containing the directory or a restore has been tested. No privileged configuration
changes or new backup infrastructure are needed or authorized. Local bundle
verification below proves recoverability from that file, not successful off-host
backup or survival of a WonkServ disk loss. Confirm completed backup retention and
restore evidence before relying on it for disaster recovery. Do not delete the
bundle or frozen recovery refs.

In one operator shell, establish and record these values in the existing private
handoff record (including the eventual bundle digest and public root SHA):

```sh
cd /home/ecash/dev/keyvalet
set -euo pipefail
test "$(git branch --show-current)" = hardware/keyvalet-v0.4.2
test -z "$(git status --porcelain=v1)"
test -z "$(git remote)"
test -z "$(git for-each-ref --format='%(refname)' refs/heads/main refs/private/keyvalet refs/tags)"
snapshot_commit=$(git rev-parse HEAD)
snapshot_tree=$(git rev-parse HEAD^{tree})
private_master=$(git rev-parse refs/heads/master)
for checkpoint in be41564249abb3e410a2db4d57d31e7b64dd5724 \
  c5df4bcc5a49599e25da67f4aad990de40666c1e \
  7f0dcb44d1b01c36388bce5c4bfa3b7015664595; do
  git cat-file -e "$checkpoint^{commit}"
  git merge-base --is-ancestor "$checkpoint" "$snapshot_commit"
done
bundle_dir=/home/ecash/dev/keyvalet/backups/private-git-history
bundle="$bundle_dir/keyvalet-prepublic-$snapshot_commit.bundle"
git check-ignore backups/private-git-history/location-check.bundle
```

Require the ignore check to report the designated path. Review the recorded
snapshot against the approved GH3 checkpoint; HEAD must include GH3, not merely
GH2. Stop if a shell variable was lost; reload recorded values, not a new HEAD.

### 2. Create, verify and exercise private recovery before retiring heads

Only after explicit transition approval:

```sh
umask 077
mkdir -p "$bundle_dir"
test ! -e "$bundle"
git bundle create "$bundle" refs/heads/hardware/keyvalet-v0.4.2 refs/heads/master
git bundle verify "$bundle"
git bundle list-heads "$bundle"
sha256sum "$bundle"
```

Require a complete, self-contained bundle with **no prerequisites** and exactly
two heads: hardware at `$snapshot_commit`, master at `$private_master`. Record
its SHA-256 privately and compare it again before recovery. A digest alone does
not prove recoverability. Do not use `--all`: tool refs/unreachable objects and
ignored workstation files are deliberately outside this engineering-history
archive. If other accepted history is discovered outside these heads, stop for
scope review before proceeding.

Exercise the bundle pack and restore both saved heads into frozen recovery refs
in this **same** object database, without switching or maintaining another repo:

```sh
git bundle unbundle "$bundle"
git -c fetch.fsckObjects=true fetch --no-tags "$bundle" \
  refs/heads/hardware/keyvalet-v0.4.2:refs/private/keyvalet/engineering \
  refs/heads/master:refs/private/keyvalet/master
test "$(git rev-parse refs/private/keyvalet/engineering)" = "$snapshot_commit"
test "$(git rev-parse refs/private/keyvalet/master)" = "$private_master"
for checkpoint in be41564249abb3e410a2db4d57d31e7b64dd5724 \
  c5df4bcc5a49599e25da67f4aad990de40666c1e \
  7f0dcb44d1b01c36388bce5c4bfa3b7015664595; do
  git cat-file -e "$checkpoint^{commit}"
  git merge-base --is-ancestor "$checkpoint" refs/private/keyvalet/engineering
done
git diff --exit-code "$snapshot_commit" refs/private/keyvalet/engineering --
```

`unbundle` validates/unpacks the actual saved pack; `verify` must confirm no
missing prerequisite history, and the explicit fetch exercises head recovery.
This uses the existing object database, not an independent empty-database or
backup-system restore test. Inspect original files with
`git show <checkpoint>:<path>`; compare the recovered P0 package against current
bytes, including `SHA256SUMS` itself. All nine comparisons must be empty:

```sh
release_dir=releases/keyvalet-m11c-p0-closeout-be41564249ab
mapfile -t release_files < <(git ls-tree -r --name-only \
  c5df4bcc5a49599e25da67f4aad990de40666c1e -- "$release_dir/")
test "${#release_files[@]}" -eq 9
for artifact in "${release_files[@]}"; do
  git show "c5df4bcc5a49599e25da67f4aad990de40666c1e:$artifact" | cmp - "$artifact"
done
(cd "$release_dir" && sha256sum --check SHA256SUMS)
sha256sum "$release_dir/SHA256SUMS"
```

The last digest must be
`3a01e36b17693dd14d022e9ff74e24dc1e0944f17f395bccc97fe6aa655233e6`.
Keep the private handoff record with bundle path, digest, saved heads and three
checkpoint IDs. Recovery verification must succeed **before** deleting any old
head. No backup or transition operations in this section were executed in GH3.

### 3. Construct and verify the parentless public root

Use the approved public identity for both author and committer; unset inherited
Git identity/date overrides in the command subshell. No `-p` argument is allowed.
The new commit reuses exactly the reviewed tree, not the historical ancestry:

```sh
public_email='<approved-public-email>'
test "$public_email" != '<approved-public-email>'
public_root=$(
  unset GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE
  unset GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL GIT_COMMITTER_DATE
  git -c user.name='Evan W. Cash (WonkWorks)' -c user.email="$public_email" \
    commit-tree "$snapshot_tree" -m 'Initial public KeyValet source and P0 provenance'
)
git update-ref refs/heads/main "$public_root" ""
test "$(git rev-parse main^{tree})" = "$snapshot_tree"
test "$(git rev-list --count main)" = 1
test "$(git rev-list --parents -n 1 main)" = "$public_root"
git show --no-patch --format=fuller main
git diff --exit-code "$snapshot_commit" main --
```

Verify both identities/dates manually and record `$public_root`. No old commit
is reachable: `main` has exactly one commit and zero parents. Check **every**
archived commit, not just three checkpoints, for accidental shared ancestry:

```sh
test -z "$(comm -12 \
  <(git rev-list refs/private/keyvalet/engineering refs/private/keyvalet/master | LC_ALL=C sort) \
  <(git rev-list main | LC_ALL=C sort))"
git rev-list --objects main
```

Review the last command's object/path inventory against the sanitized tracked
tree. Public object closure is the new root plus that exact tree and its blobs;
old-history-only blobs cannot be reached without a parent or tree link. Unchanged
safe blobs shared with private history are intentional. Sensitive old versions
must not appear in the approved tree; a parentless commit does not sanitize a
current secret. Repeat the tracked-tree audit and all nine hash/source-input
checks on `main`; check no bundle, credentials, dumps or ignored builds appear in
`git ls-tree -r --name-only main`. Stop if any expected exclusion is missing.

### 4. Switch once; retire only the archived old heads

After all preceding gates succeed, and while still clean at the recorded
snapshot, switch the existing checkout. Exact old-value ref guards prevent
retiring a head that moved during review:

```sh
test "$(git rev-parse HEAD)" = "$snapshot_commit"
test -z "$(git status --porcelain=v1)"
git switch main
git update-ref -d refs/heads/hardware/keyvalet-v0.4.2 "$snapshot_commit"
git update-ref -d refs/heads/master "$private_master"
git for-each-ref --format='%(refname) %(objectname)' refs/heads refs/private/keyvalet
git status --short --branch
```

Require only `main` under `refs/heads/`, a clean worktree and both frozen private
refs at the recorded original heads. They retain M11C/P0/GH2 and GH3 without
being ongoing development branches. Never develop on them or push them.
The preserved-input packaging mode checks original M11C ancestry and remains a
private historical verification mode, not a public-root reproduction path. New
source builds use their honest public Git identity; accepted P0 assets stay fixed.

### 5. Separately authorized remote, initial push and normal development

Only after separate public-repository creation/publication authorization and
final source/notices review, create `WonkWorks/keyvalet` without an auto-generated
README/license/root commit. Stop if the remote already has unexpected refs.
No GitHub credential setup or authentication flow is introduced here. Then:

```sh
git remote add origin https://github.com/WonkWorks/keyvalet.git
git config remote.origin.push refs/heads/main:refs/heads/main
git -c push.followTags=false push --dry-run origin refs/heads/main:refs/heads/main
git -c push.followTags=false push --set-upstream origin refs/heads/main:refs/heads/main
```

The explicit refspec and `push.followTags=false` publish only reviewed `main`.
Do not use `--mirror`, `--all`, `--tags`, `--follow-tags`, force, wildcard refspecs,
private heads or a `.git`/workspace archive. Frozen refs and the bundle stay
private. Review remote refs and root SHA after push. Thereafter, edit/test/review
and make ordinary commits on `main` in this same checkout, with an approved
public Git identity; push the same explicit main refspec. There is no export or
synchronization workflow and no second active repository.

### 6. Separately authorized tags, firmware assets and acceptance

Create release tags only on an explicitly reviewed **public** commit, never an
old M11C/P0/GH2 private commit. For the first package, after confirming the public
commit contains the exact accepted artifacts and source record, the future
commands are (replace the placeholder deliberately):

```sh
release_commit='<approved-public-commit-sha>'
git merge-base --is-ancestor "$release_commit" main
git -c user.name='Evan W. Cash (WonkWorks)' -c user.email="$public_email" \
  tag -a keyvalet-m11c-p0-closeout-be41564249ab "$release_commit" \
  -m 'Accepted P0 bytes; original engineering provenance retained in release.json'
git -c push.followTags=false push origin refs/tags/keyvalet-m11c-p0-closeout-be41564249ab
```

Review tagger identity too; inherited identity overrides must be unset as for the
root commit. Under explicit release-publication approval, prepare a draft versioned
GitHub Release from that tag. Attach the original nine artifacts unchanged,
plus separately reviewed GPL text, adopted permission, third-party notices and
complete corresponding-source/configuration/build/install delivery materials.
The public source snapshot must include all 88 hashed repository inputs and the
exact SDK configuration; preserve original private checkpoint IDs as provenance,
not tag targets. Verify pinned upstream source/runtime downloads and sustained
source access before publication; a GitHub auto-generated project ZIP alone is
not the complete dependency-source delivery arrangement.

Do not mark this development package as operational firmware `1.0.0` or claim
physical acceptance. `1.0.0` is reserved for the first operationally complete,
accepted TowneCore/KeyValet deployment. PCB `v0.4.2`, embedded `0.5.0-m5`, and the
long release ID are different concepts. Never rename/rebuild assets to change
these labels. After explicit publication, independently verify public downloads
and hashes. Only then configure a real versioned release base URL in a separately
authorized TowneCore change; no floating `latest`, fabricated URL, GCS mirror,
GitHub download authentication or proprietary update service. Until then firmware
acquisition remains `publication-required`. Physical Chrome/Web Serial acceptance
and any firmware/certificate/hardware operations are separately authorized work.

### 7. Interrupted-transition recovery (same checkout)

Stop before any push on an unexpected result. Retain the private handoff values;
do not reset/clean/rebase, force-update refs, garbage-collect or delete archives.

- Before creating `main`: original heads are intact. Reverify the saved bundle
  and digest. Do not overwrite an existing archive or private ref when resuming.
- If `main` exists but retirement has not finished: original and/or frozen refs
  still retain history. Inspect their recorded SHAs and the parentless tree.
  Resume only missing operations after independent review, never the whole block
  blindly. A failed expected-value guard is a stop condition, not an invitation
  to remove the guard.
- If private refs are lost, recover them from the verified bundle with the exact
  `fetch --no-tags` refspecs in section 2, requiring absent destination refs.
  Recheck checkpoint ancestry and all nine P0 files. Inspect old source directly
  with `git show`; do not create another maintained development branch.
- If an authorized local rollback to the old checkout is necessary **before any
  publication**, require a clean tree and an absent hardware head, then use
  `git update-ref refs/heads/hardware/keyvalet-v0.4.2 "$snapshot_commit" ""` and
  `git switch hardware/keyvalet-v0.4.2`. Restore `master` only if absent with
  `git update-ref refs/heads/master "$private_master" ""`. Leave any public root
  untouched for investigation; never force-push private history to GitHub.
- If publication has happened, stop for incident-specific approval; local branch
  restoration does not retract published data. Do not publish private refs as a
  repair strategy. No automatic rollback, ref deletion or history rewrite is
  authorized by these instructions.

## Release gate

- The reviewed Section 7 permission is adopted. Complete final release/source
  delivery and notice review; documentation checks do not certify legal compliance.
- Accompany any eventual firmware release with the unmodified GPL text, the
  adopted permission, project copyright/third-party notices, pinned
  SDK/dependency references, corresponding-source/configuration/build/install
  materials and provenance. Keep these separate from the immutable nine-file
  package. Adoption is not authorization to publish.
- Approve the publication-history option and review the exact public tree.
- Reverify all nine artifact hashes; retain factory/replacement safety warnings.
- Only with later authorization, create the public `WonkWorks/keyvalet` repository
  and a draft versioned release. Attach all nine original artifacts and separate
  license/notices/source materials before any immutable publication.
- Do not manufacture a download URL as if it exists, publish a floating `latest`
  flashing target, or enable TowneCore factory flashing prematurely.
- Live GitHub retrieval and physical Chrome/Web Serial flashing acceptance remain
  pending. P1 is implemented/development-deployed, not physically flash-accepted.
