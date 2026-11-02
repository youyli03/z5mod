# AGENTS.md - rules for working inside z5mod/

These rules apply to **every change under this directory**, by a human or by an
agent, and they override convenience. The project-level rules in the parent
handoff directory still apply; where the two differ, the stricter one wins.

## 1. Language

Everything committed to this repository is **English only**: file names, code,
comments, documents, commit subjects. No CJK characters in any tracked file.
Check before committing:

```sh
python scripts/check_repo.py
```

## 2. Never commit what we cannot ship

Tracked files must never contain:

* Nikon firmware images, in original or modified form;
* firmware decryption keys, tables, signatures or derived dumps - with the
  single exception of `firmware/src/z5mod_fw/platform/z5_150_mask.hex`,
  documented in NOTICE.md. Do not add another one without asking;
* memory dumps, extracted modules, or repackaged update files;
* `.Z5M` build products or anything under `output/`, `build/`, `dist/`.

`.gitignore` covers these patterns; verify with `git status` before committing.

### 2.1 The one exception: prebuilt example modules

An example may ship a prebuilt module so a user without a toolchain can try it.
Such a file is only accepted when all of the following hold:

* it lives at `sdk/examples/<example>/prebuilt/Z5APP.BIN` - nowhere else,
  because that is the name the loader opens on the card;
* it is **reproducible**: rebuilding that example from this repository produces
  a byte-identical file, and the commit that adds it states the hash;
* it contains nothing but our own code and (optionally) an audio asset that we
  own or generated - never third-party audio, never firmware.

A module is not firmware: it needs no version manifest. The hash lives in the
commit message and in the example readme, which is enough to check a rebuild.
Anything that cannot be reproduced from this repository stays out of git.

## 3. No absolute or machine-specific paths

No committed file may contain a home directory, user name, host name, drive
letter or any other local path. Scripts resolve their own location, for example:

```python
ROOT = Path(__file__).resolve().parents[2]
```

Documentation shows repository-relative commands only:

```sh
python firmware/patch.py --source <your stock image> --output output/EG165050-mod.BIN
```

Check before committing:

```sh
python scripts/check_repo.py
```

## 4. The tooling never touches hardware

Code in this repository:

* never downloads anything;
* never opens a camera or an SD card;
* never modifies its input file;
* refuses to overwrite existing outputs.

Any script that would violate one of these is not accepted, even as a helper.

## 5. Patch discipline

Every patch is a declaration, not a blind write:

* the input must match a manifest entry in `firmware/known/` (size and SHA-256);
* each edit declares the exact bytes it expects and aborts on mismatch;
* `--allow-mismatch` style escape hatches must not exist in released code;
* the container/module CRC is recomputed after patching;
* the build writes a JSON report with input digest, output digest, selected
  presets, expanded feature list and every edit (offset, expected, replacement).

## 6. Honesty in reports and docs

* `camera_verified` is `true` only for an exact output that was run on a camera.
* Offline checks are described as offline checks, never as hardware validation.
* Known failures stay documented (for example: silent shutter is unsolved).
* A new output hash goes into `firmware/known/` only after a camera run; until then it is
  `null` with `camera_verified: false`.

## 7. Code rules

C (in `sdk/`):

* C99, freestanding, `-nostdlib`, no libc dependency;
* position independence is provided by the build (`-fpic`, r9); never take the
  address of a global from PIC code - that yields a link-time value;
* code that will be copied into `alloc()` memory must not use r9 and must not
  branch outside its own image; the build checks this (`tools/imgcheck.py`);
* anything handed to the firmware must not point into the module image, because
  the loader frees that image as soon as the module returns.

Python (in `firmware/` and `tools/`):

* Python 3.10+, standard library only for the core patcher;
* optional dependencies (for example an assembler) are declared in
  `pyproject.toml` extras and documented as optional;
* entry points stay importable and testable without a camera.

## 8. Layout

| Path | Contents |
| --- | --- |
| `firmware/` | Python patcher: platform tables, features, build, report, and `firmware/known/` hash manifests |
| `sdk/` | C SDK for `.Z5M` modules |
| `docs/` | design notes, ABI notes, per-feature documentation |

New features go into `firmware/src/<pkg>/features/<name>.py` and get a matching
document under `docs/`. Platform addresses live only in
`firmware/src/<pkg>/platform/<camera>_<version>.py`.

## 9. Commits: small, verified, and approved first

Three rules, in this order. All three apply to every single commit.

### 9.1 Nothing is committed unverified

Before a commit is created, the change must actually be exercised:

* `python scripts/check_repo.py` passes - and with the hook enabled
  (`git config core.hooksPath .githooks`) it runs automatically before every
  commit, so a failing check cannot be committed by accident;
* the layer that was touched builds and produces the expected bytes:
  * `firmware/` - the patcher runs against the known input from
    `firmware/known/` and reproduces the recorded output hash, or the new hash
    is recorded together with how it was obtained;
  * `sdk/` - the example builds, `imgcheck` reports zero violations, and the
    module loads on a camera if the change affects runtime behaviour;
* anything that changes hardware behaviour was run on a camera. If it was only
  verified offline, the commit body says so in one line - never imply hardware
  testing that did not happen.

Unverified work stays uncommitted. "It compiles" is not verification.

### 9.2 Ask the owner first

Every commit is **proposed before it is created**. The proposal contains:

* `git status --short`;
* the files and the line counts that would be staged;
* one sentence per file on what changed and why;
* what was run to verify it, and the result.

Only after an explicit go-ahead is `git commit` executed. This applies to every
commit, including one-line fixes, documentation and formatting.

### 9.3 Keep each commit small

* one logical change per commit - no mixed "patch + refactor", no mixed
  "firmware + sdk";
* if a change touches more than about five files or two hundred lines, split it
  into commits that each stand on their own;
* documentation and code for the same change may travel together, but unrelated
  cleanups do not;
* never commit generated files, firmware, keys or dumps (see section 2).

### 9.4 Commit message format (Conventional Commits, body required)

Every commit uses this shape:

```
<type>(<scope>): <description>

<what changed and why - one short paragraph per point>

Verified:
- <command> -> <result>
- <camera run / hash comparison / checker output>
```

Rules for the header:

* `type` is one of `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`,
  `build`, `ci`, `chore`, `revert`;
* `scope` is required and names the area: `repo`, `firmware`, `sdk`, `scripts`,
  `docs`;
* `description` is English, imperative, lower case, no trailing period;
* the whole header stays under 72 characters.

Rules for the body:

* the body is **mandatory**, including for `chore` and `docs` commits;
* it states what changed, why it changed, and anything a reviewer must know to
  judge the change;
* it ends with a `Verified:` block listing exactly what was run and what the
  result was; when the change was only checked offline, say so in that block;
* breaking changes use a `BREAKING CHANGE:` footer.

Examples:

```
fix(firmware): honour the expected-byte check on the loader patch

The loader feature appended its code before validating the anchor bytes, so a
wrong input could still produce an output file. Validate first, then append.

Verified:
- python firmware/patch.py --source <stock> -> aborts on a modified anchor
- python scripts/check_repo.py -> OK
```

```
chore(repo): add licence, shipping policy and ignore rules

State the licence of our own code, what the repository will never contain, and
which artefacts must stay out of git before any source lands.

Verified:
- ASCII check on the three files -> no output
- git check-ignore -> none of the three is ignored
```

## 10. When in doubt

Ask before adding anything that: writes to hardware, hides a verification step,
weakens an expected-bytes check, or adds a new dependency. Those four are the
expensive mistakes.

The commit gate in 9.2 has no exceptions: a change that is small, obvious and
verified still waits for an explicit go-ahead.
