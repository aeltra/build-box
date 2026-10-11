# Build Box NG

Build Box manages Aeltra OS build chroots, called targets. It bootstraps a
target from packages and lets unprivileged users log into it, run commands
inside it and build software there.

```
build-box create [OPTIONS] <target-name> <spec> ...
build-box login <target-name>
build-box run <target-name> -- <command> ...
build-box list
build-box delete <target-name> ...
```

Type `build-box <command> --help` for the options of each command.

## How it works

Each user's targets live below `/var/lib/build-box/users/<uid>/targets`.
Entering a target needs a few mounts: `dev`, `proc` and `sys`, and the
user's real home on `/home/<user>/RealHome`, next to a home of the target's
own. These are made by `build-box-do`, a small setuid root helper. It only
works for members of the group `build-box`, never for root, and only on the
caller's own targets.

## Requirements

- Linux 5.8 or newer is recommended. On older kernels, a target's `proc`
  shows the same processes as the host's `/proc`, instead of only the
  user's own. This concerns, for example, Debian 10, Ubuntu 20.04 without
  the HWE kernel, RHEL 8, and old LTS kernels on rolling distributions.
- The user must be a member of the group `build-box`.
- `qemu-user-binfmt`, to work in targets of a foreign architecture.

## License

MIT, see [LICENSE](LICENSE).
