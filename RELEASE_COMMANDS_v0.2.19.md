# Release commands for v0.2.19

```bash
git status
git add -A
git commit -m "Add selectable irrigation pumps"
git push origin main

git tag -a v0.2.19 -F RELEASE_NOTES_v0.2.19.md
git push origin v0.2.19
```

The GitHub Actions workflow builds `firmware.bin` from the pushed version tag and uses the annotated tag message as the GitHub release notes.
