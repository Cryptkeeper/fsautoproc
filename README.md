# fsautoproc

`fsautoproc` is a basic C utility for when:

- you have a large directory of various files
- that when a file matching any number of regex patterns is added, modified, removed, or left unchanged,
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

C11 is used for basic atomic boolean operations when scheduling work across threads. C99 compatibility can be achieved by providing a `stdatomic.h` compatible stub header for your platform.

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
  -l          List time spent for each command set (default: false)
  -p          Pipe subprocess stdout/stderr to files (default: false)
  -s <dir>    Search directory root (default: `.`)
  -t <#>      Number of worker threads (default: 4)
  -u          Skip processing files, only update file index
  -v          Enable verbose output
```

### Basic Configuration

`example.fsautoproc.json` provides a basic example configuration file. The configuration file is a JSON array of objects, each object representing a desired "action" at a grouping level of your choosing. Each action object has the following properties:
- `description` (string): An optional, brief string describing the action (for logging purposes when used with the `-l` flag)
- `patterns` (array of strings): An array of regex patterns to match against file paths (regex behavior may vary by platform, see `man 3 regcomp` for details), a file must match at least one pattern to trigger the action
- `commands` (array of strings): An array of commands to execute when a file matching a pattern is detected (commands are executed in order, command execution behavior may vary by platform, see `man 3 system` for details)
- `on` (array of strings): An array of file events on which to trigger the action for a file (`new` for new files, `del` for deleted files, `mod` for modified files, `nop` for unmodified files)

#### Command Execution

When executing a command (or a series of commands), the commands are executed in configured order. The parent process is forked, and the child process executes the command using `system(3)`. The parent process waits for the child process to complete before continuing. If a command fails (i.e. returns a non-zero exit status), the parent process logs the failure and continues to the next command.

The path of the file that triggered the command is available to the command as an environment variable, `FILEPATH`.

If `-p` is enabled, the child process redirects its stdout and stderr to files in the current working directory. The files are named `stdout.<thread #>.log` and `stderr.<thread #>.log`, respectively.

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
