# vkToolbelt

**vkToolbelt** is a personal collection of hands-on Vulkan examples, tools, and snippets—gathered during the process of learning and working with the Vulkan API - initially derived from Sascha Williams repository, but I've been adding my own examples / POCs as needed.

This project is not a framework or SDK. It's a workshop bench: dusty, functional, and evolving. Some examples may be rough, others refined. The goal is utility, not polish.

## ✨ What This Is

- A growing set of Vulkan samples focused on clarity and self-containment.
- A place to experiment with rendering techniques, picking strategies, memory layouts, and pipeline configurations.
- A personal reference and quick-start source for common tasks.
- Occasionally useful to teammates or curious devs facing similar problems.

## ❗ What This Is Not

- A fully documented tutorial or formal training material.
- A plug-and-play graphics engine.
- A comprehensive showcase of Vulkan best practices (though care is taken where it matters).

## 📦 Project Structure

| Folder         | Contents                                    |
|----------------|---------------------------------------------|
| `/examples/`   | Small, focused demos (e.g. object picking, shadow maps, render passes) |
| `/shaders/`    | Associated GLSL/spir-v files                |
| `/utils/`      | Helper classes, math, debug UI, logging     |
| `/docs/`       | Notes, sketches, and design rationales      |

## 🛠 Example Features

- Object ID color picking (single and marquee)
- Dynamic uniform updates
- Depth peeling and transparency passes
- Pipeline setup recipes
- Wireframe + solid toggles
- GPU-side staging buffer logic
- [Add more as they grow...]

## 🔍 Why This Exists

As Vulkan becomes part of both personal and professional work, this toolbelt keeps things close at hand. It's easier to scale up complex projects when you’ve already worked out the edge cases in isolation.

Sometimes it's a cheat sheet. Sometimes it's a sandbox. Either way—it’s mine. And now maybe it’s yours, too.

## 🧳 Using This

You’ll need:
- Vulkan SDK (1.3+)
- CMake (with Ninja or MSBuild)
- A modern GPU with Vulkan 1.2+ support
- Visual Studio 2022 or WSL2 build environment (both tested)

## 🧾 License

This project is shared under the MIT license. Attribution is appreciated but not required. If you find something useful, fix something broken, or just want to swap tips—feel free to open an issue or drop a note.

---

Happy rendering,
**Bruce**

