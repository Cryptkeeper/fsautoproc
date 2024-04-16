# fsautoproc

`fsautoproc` is a basic C utility for when:

* you have a large directory of various files
* that when a new file with a specific file extension is added, modified, or removed,
* you want to run a series of commands (likely using the filepath as an argument)

e.g. you have a large folder of PDFs that is uploaded to a web server. When a new PDF is added, or an existing PDF is
modified, you wish to strip any unnecessary file metadata and generate a thumbnail image. If the PDF file is removed,
its thumbnail is removed as well.

`fsautoproc` does not (yet):

* re-generate broken file system state (e.g. you manually delete a generated thumbnail, `fsautoproc` will log an error
  for the missing file but does not automatically regenerate the file)

Before using, consider:

* `fsautoproc` is not a daemon. It is intended to be run as a cron job or a systemd timer.
* `fsautoproc` is not a live file system watcher. It scans for modified files on each run.
* **This is a C utility invoking shell commands. Be mindful of the serious security implications of running arbitrary
  shell commands and use restricted user accounts.**
* Like most C code you find on the internet, `fsautoproc` was built for a specific hobby purpose with arbitrary C99
  opinions and may not be reasonable for your use case.

## Building

`fsautoproc` is built with CMake. You will need a C99 compiler and CMake installed. The utility is primarily written for
use on Linux and BSD systems (including macOS). It may work on Windows with WSL or Cygwin, but this is currently
untested.
