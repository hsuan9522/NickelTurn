# NickelTurn

NickelTurn is a small NickelHook mod for the supplied Kobo firmware and
`.kepub.epub` books. It adds a NickelMenu Reader action, **直橫排互換**, which
stores the requested layout for the current book and restores it when that
book is opened again.

> [!WARNING]
> Most of this project was produced with AI assistance. It is an experimental,
> firmware-specific mod which hooks Kobo's private Nickel internals. Installing
> or using it may cause Nickel to crash, require manual recovery, or behave
> unexpectedly after a firmware update. Use it entirely at your own risk.

It targets only the included `libnickel.so.1.0.0`. A firmware mismatch aborts
during initialization and NickelHook restores the original hooks.

## Build profiles

The old proof-of-concept sequence has been retired from the public build
interface. There are only two supported profiles:

| Profile | Build argument | Purpose |
| --- | --- | --- |
| `full` | default | The verified KEPUB toggle, JSON cache, swipe direction, and delayed native bookmark restore. |
| `smoke` | `NT_BUILD=smoke` | Load/failsafe check only. It changes no book layout or state. |

Build the normal package from this directory:

```sh
docker run --platform linux/amd64 --volume="$PWD:/work" --user="$(id -u):$(id -g)" \
  --workdir=/work --env=HOME --entrypoint=make --rm \
  ghcr.io/pgaskin/nickeltc:1.0 clean all koboroot NT_BUILD=full
```

The output is `KoboRoot.tgz`. Copy it to `.kobo/` on the Kobo, safely eject,
and let the device finish its update. Do not install while a previous Kobo
update is still in progress.

## Use

NickelTurn writes a small NickelMenu configuration file at
`.adds/nm/nickelturn`; this requires NickelMenu to be installed. Open a KEPUB,
open NickelMenu's Reader menu, and choose **直橫排互換**.

The selected mode is stored in:

```text
/mnt/onboard/.adds/nickel-turn/book-modes.json
```

The JSON uses exact Kobo `ContentID` values; it intentionally does not
normalise `file://` prefixes or paths.

```json
{
  "books": {
    "file:///mnt/onboard/Books/example.kepub.epub": {
      "mode": "vertical-rl",
      "updatedAt": 1784590000
    }
  }
}
```

## Logs

All diagnostic messages are available through:

```sh
logread | grep NickelTurn | tail -n 120
```

Important initialization and menu-action events are also written to:

```text
/mnt/onboard/.adds/nickel-turn/nickel-turn.log
```

The persistent log is deliberately not written from CSS/render hooks. Those
hooks only write to `logread`; this avoids user-storage I/O during reading or
USB mass-storage sessions.

## Safe removal

While connected by USB, create an empty file:

```text
.adds/nickelturn/uninstall
```

Safely eject and allow Nickel to start once. NickelHook sees the flag before
installing hooks, deletes the plugin library, deletes the flag, and calls
NickelTurn cleanup to remove only its NickelMenu config and signal script.
Books and `book-modes.json` are preserved.

## NickelHook safety notes

- NickelHook's 20-second failsafe remains enabled. A startup failure leaves
  the library as `.failsafe`, preventing it from loading on the next boot.
- JSON is opened only once during init, parsed into a bounded in-memory POD
  cache (maximum 1 MiB / 8192 rows), and immediately closed.
- JSON writes happen only from the NickelMenu signal's Qt timer handler. They
  use write → `fsync` → close → rename; no user-storage descriptor is kept.
- CSS and reader hooks do no file I/O. Their work is bounded to active KEPUB
  calls and uses the cached modes.
- The source uses Qt/C APIs and avoids the C++ standard library.

The remaining firmware-specific reader pointers are protected by required
symbol checks and exact offsets at initialization. If any check fails, init
returns an error so NickelHook can restore the original hooks.
