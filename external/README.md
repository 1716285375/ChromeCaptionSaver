# Third-party dependencies

Dear ImGui is included as the `external/imgui` Git submodule.

- Upstream: https://github.com/ocornut/imgui
- Version: v1.92.5
- Pinned commit: `6d910d5487d11ca567b61c7824b0c78c569d62f0`
- License: [MIT](imgui/LICENSE.txt)

Run `git submodule update --init --recursive` after cloning. The runtime does not download this dependency.

Other dependencies are declared in `vcpkg.json`: spdlog (MIT), nlohmann/json (MIT), and GoogleTest (BSD-3-Clause, tests only). Their upstream license notices must accompany redistributed packages where required.
