# How to write an issue

A good report lets someone else see the same bug without asking you ten questions. Issues may be written in **English or Russian**. One problem per issue; if you found three bugs, open three issues (a short list in one issue is fine for tiny things).

Before you write: look at the [known issues](../README.md#known-issues), the [changelog](../CHANGELOG.md) and the open issues. If your problem is already there, add what you know as a comment instead of opening a new issue. Check also that you are on the [latest release](https://github.com/stxlvn/mw2_enhanced/releases/latest); a bug may be fixed already.

## Bug report: what to include

1. **Mod version and build.** The release name (for example `v0.6.0-beta`) and which archive: 64-bit, 32-bit or iw4x-sp. A nightly build: say so and give the date.
2. **How you play.** Network (over Steam or by IP: host or client) or split screen on one PC. For a network game tell who had the problem: the host, the client, or both.
3. **Where.** The mission name (as the game shows it, any language is fine) and the moment: "right after the first white screen", "when the helicopter crashes", "at the door breach in the shipyard". If you can, say how long into the mission and whether you had restarted from a checkpoint.
4. **What happened, what you expected.** Two short sentences are enough. "I walk forward on my own and cannot go back" is a good sentence. "It's broken" is not.
5. **Steps.** What you did, in order, from loading the mission. If it does not happen every time, say how often (for example "3 times out of 5").
6. **The log.** Send `mw2coop\mw2coop.log` from the game folder, **from every PC that played**, right after the problem and **before you start the game again** (the file is replaced each time the game starts). Drag the file into the issue. It shows what both machines did, and it is the thing that most often finds the cause.
7. **A video or screenshot** if the bug is something you see (a missing texture, a body in the wrong place, a stuck camera). Seconds before and after the problem help most.
8. **Your setup**, if it could matter: Windows version, graphics card, windowed or full screen, a pad or keyboard and mouse.

A crash: also say what was on screen and whether the game window closed or froze. The log of a crash holds the address of the fault; do not delete it.

## What to remove before you post

The log and screenshots are public. Look through them first:

- Your Windows user name appears in folder paths. Replace it with `<user>`.
- Do not post IP addresses, Steam IDs, tokens or passwords. Replace them with `<hidden>`. The mod never needs them in an issue.
- Do not attach game files (scripts, maps, sounds, textures) or the mod's saves of other people.

## Title

Short and specific: the mission or feature first, the symptom after. Good: `Oil Rig: hostages stay in the room and die from the C4`. Poor: `Bug`, `Help`, `Doesn't work`.

## Suggestions

Say what you want, and why (what annoys you now). One idea per issue. If you know how another game or mod does it, add a link.

## After you post

- The maintainer may ask for another log or a test build; please answer in the same issue.
- If the problem is fixed, the issue is closed with the commit or release. If you can still see it in a newer build, write so in a comment and add a new log.
- Do not add "+1" comments to old issues; use the reaction button. Add a comment when you have new facts: another mission, another build, a log.

## A short template

```
Version: v0.6.0-beta, 64-bit
Play mode: network over Steam, I am the client / host
Mission and moment: Arcadia, after the second checkpoint
What happened: we were both teleported far ahead
What I expected: to stay with the team
Steps / how often: 2 times out of 3, after restarting from the checkpoint
Attached: mw2coop.log from both PCs (user name removed)
```
