# **Standardized Naming Convention Across All \[AX\] Repositories**

Keeping the on-demand release builder in its own dedicated workflow file—with identical file, job, and artifact naming patterns across every repo—means you can trigger any build from the GitHub Actions UI or with the exact same CLI command (gh workflow run build-release.yml):

| Standard Element | Convention | Example (FLAC\_AX / Piccolo-AX / Amiberry-AX) |
| :---- | :---- | :---- |
| **Workflow File** | `.github/workflows/build-release.yml` | Identical filename in every repo (`ci.yml` stays fast for push) |
| **Workflow `name:`** | `Build Release Artifacts` | Identical display title in the Actions sidebar across all repos |
| **Trigger** | `workflow_dispatch` (with optional `build_type` input: `Release`) | Manual one-click cloud compile with zero local disk usage |
| **Job IDs** | `build-linux`, `build-windows`, or `build-image` | Predictable matrix (`ubuntu-latest` / `windows-latest`) |
| **Artifact Names** | `<repo>-<os>-<arch>` | `flac-ax-linux-x86_64`, `flac-ax-windows-x86_64`, `piccolo-ax-image-x86_64`, `amiberry-ax-linux-x86_64`, `amiberry-ax-windows-x86_64` |
