# Security Policy

## Supported versions

Only the latest tagged release and the current `main` branch are
supported. Please reproduce a problem against one of those before you
report it.

## Scope

This is a C99 reimplementation of a 1997 game. It reads disc images
(`.cue` sheets, `.bin` tracks and their ISO 9660 file system), the game's
pictures, fonts, sounds and track files, and its settings and best-times
files, much as the original did, with little validation.

Memory-safety problems that a malformed disc image, game file or
settings file can trigger (buffer overflows, out-of-bounds reads or
writes, integer overflows in sizes) are in scope.

Out of scope:

- Problems that only happen with the original `IGN_WIN.EXE`.
- Crashes that are not exploitable, rendering glitches and differences
  from the original. Report those as ordinary issues.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting: the repository's
**Security** tab → **Report a vulnerability**. A fix can then land before
the details are public.

If you cannot use it, email **xesfnet@gmail.com** with the steps to
reproduce the problem. A file that triggers it helps most.

You should get an answer within a few days. There is no bug bounty, but
you are credited in the fix unless you ask not to be.
