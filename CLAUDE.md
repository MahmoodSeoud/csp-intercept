# CLAUDE.md

## gstack

- Use the `/browse` skill from gstack for **all** web browsing.
- **Never** use `mcp__claude-in-chrome__*` tools.

### Teammate setup

This project requires gstack (team mode). Two files are committed to enforce it:
`.claude/settings.json` registers a `PreToolUse` hook on the `Skill` tool, and
`.claude/hooks/check-gstack.sh` runs it — blocking any skill until gstack is
installed at `~/.claude/skills/gstack`.

Each developer installs gstack once ([bun](https://bun.sh) required):

```bash
git clone --depth 1 https://github.com/garrytan/gstack.git ~/.claude/skills/gstack
cd ~/.claude/skills/gstack && ./setup --team
```

### Available gstack skills

`/office-hours`, `/plan-ceo-review`, `/plan-eng-review`, `/plan-design-review`, `/design-consultation`, `/design-shotgun`, `/design-html`, `/review`, `/ship`, `/land-and-deploy`, `/canary`, `/benchmark`, `/browse`, `/connect-chrome`, `/qa`, `/qa-only`, `/design-review`, `/setup-browser-cookies`, `/setup-deploy`, `/setup-gbrain`, `/retro`, `/investigate`, `/document-release`, `/document-generate`, `/codex`, `/cso`, `/autoplan`, `/plan-devex-review`, `/devex-review`, `/careful`, `/freeze`, `/guard`, `/unfreeze`, `/gstack-upgrade`, `/learn`

## DISCO-2 payload access

For anything about reaching the DISCO-2 payload from the ground (the `mng_dipp` root
injection, echoing a `.csh` onto the box, running it via the `csh -i` pty, node
addressing), read `~/thesis/DISCO2-Payload-Playbook.md`. Treat it as sensitive.
