# fsautoproc

`fsautoproc` is a basic C utility for when:

- you have a large directory of various files
- that when a new file with a specific file extension is added, modified, or removed,
- you want to run a series of commands (likely using the filepath as an argument)

e.g. you have a large folder of PDFs that is published to a web server. When a new PDF is added, or an existing PDF is modified, you wish to strip any unnecessary file metadata and generate a thumbnail image. If the PDF file is removed, its thumbnail is removed as well.

`fsautoproc` does not:

- re-generate broken file system state (e.g. you manually delete a generated thumbnail, `fsautoproc` can detect the removal and trigger a command to re-generate it, but you must configure the commands to do so)

Before using, consider:

- `fsautoproc` is not a daemon. It is intended to be run as a cron job or a systemd timer.
- `fsautoproc` is not a live file system watcher. It scans for modified files on each run.
- **This is a C utility invoking shell commands. Be mindful of the serious security implications of running arbitrary shell commands and use restricted user accounts.**
- Like most C code you find on the internet, `fsautoproc` was built for a specific hobby purpose with arbitrary C opinions and may not be reasonable for your use case.

## Building

`fsautoproc` is built with CMake. You will need a C11 compiler and CMake installed. The utility is primarily written for use on Linux and BSD systems (including macOS). It may work on Windows with WSL or Cygwin, but this is currently untested.

C11 is used for basic atomic boolean operations. C99 compatibility can be achieved by replacing any `_Atomic` keyword usages with `volatile` and adding a mutex lock.

1. Clone the repository and its submodules: `git clone --recursive https://github.com/Cryptkeeper/fsautoproc`
2. Build the CMake project with `cmake -B build`
3. Compile the project with `cmake --build build`
4. Optionally install binary using `make install`

### Dependencies

Git submodules provide:

- [cJSON](https://github.com/DaveGamble/cJSON)

### Usage

```
$ fsautoproc -h
Usage: fsautoproc -i <file>

Options:
  -c <file>   Configuration file (default: `fsautoproc.json`)
  -i <file>   File index write path
  -j          Include ignored files in index (default: false)
  -p          Pipe subprocess stdout/stderr to files (default: false)
  -s <dir>    Search directory root (default: `.`)
  -t <#>      Number of worker threads (default: 4)
  -u          Skip processing files, only update file index
  -v          Enable verbose output
```

#### Logging Symbols

fsautoproc uses a symbol table when logging file changes and program status. This minimizes the amount of direct output and improves searchability. Symbols denote a basic file change being detected, and letters indicate program behavior status (i.e. the result of detecting those basic file changes).

| Symbol | Purpose                               |
| ------ | ------------------------------------- |
| `[+]`  | A new file was created                |
| `[*]`  | A file was modified                   |
| `[-]`  | A file was deleted/removed            |
| `[j]`  | A file was ignored/considered junk    |
| `[n]`  | A file was not detected as modified   |
| `[s]`  | A directory is being scanned          |
| `[x]`  | A system command is being invoked     |
| `[!]`  | An error has occurred                 |
