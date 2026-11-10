# Contributing to Apex UI
[![GitHub](https://img.shields.io/badge/Platform-GitHub-green)](https://github.com/is-nobody/apex-ui)
[![Apex UI](https://img.shields.io/badge/Apex-UI-blue)](https://github.com/is-nobody/apex-ui)

Thank you for your interest in contributing to the Apex UI library! This guide will help you understand our development process and how to submit changes effectively. Whether you're fixing a typo or adding a major feature, your contribution matters.

Please note that by participating in this project, you agree to abide by our [Code of Conduct](code_of_conduct.md). We expect all contributors to help us maintain a welcoming and respectful community.

## Table of Contents
- [License](#license)
- [Development Setup](#development-setup)
- [Understanding the Project](#understanding-the-project)
- [Finding a Task](#finding-a-task)
- [Code Style Rules](#code-style-rules)
- [Using AI Agents](#using-ai-agents)
- [Making Changes](#making-changes)
- [Testing Your Changes](#testing-your-changes)
- [Submitting Changes](#submitting-changes)
- [Review Process](#review-process)
- [Getting Help](#getting-help)
- [What Makes a Good Contribution](#what-makes-a-good-contribution)

## License
By contributing to Apex UI, you agree that your contributions will be licensed under the [MIT License](license). You still own your code. However, you allow the project to use, modify, and distribute it forever, anywhere in the world, without excluding anyone else from using it too.

## Development Setup
### Prerequisites
- **C Compiler** - For compiling the code
- **CMake** - For bundling the project
- **Git** - For version control

### Set Up Your Workspace
Create your own copy of the project:

1. Fork the repository on GitHub
2. Clone your fork locally:

```bash
git clone https://github.com/YOUR-USERNAME/apex-ui.git
cd apex-ui
```

3. Add the original repository as upstream (to keep your fork updated):

```bash
git remote add upstream https://github.com/is-nobody/apex-ui
```

4. Create a new branch for your work:

```bash
git checkout -b feature/your-feature-name
```

Branch naming examples:
- `feature/add-new-function`
- `fix/parser-error-handling`
- `docs/update-tables-section`
- `test/add-coverage`

## Understanding the Project
### Project Structure
| Location                                    | Description                            |
|---------------------------------------------|----------------------------------------|
| [`apex_ui.h`](apex_ui.h)                    | Public C API header                    |
| [`apex_ui.c`](apex_ui.c)                    | Public C API implementation            |
| [`source/`](source/)                        | Source code                            |
| [`source/common`](source/common/)           | Value runtime, hashing, arena, tables  |
| [`source/core`](source/core/)               | Context, state, tree build, runtime    |
| [`source/widgets`](source/widgets/)         | Individual widget implementations      |
| [`source/interaction`](source/interaction/) | Hit-testing, damage tracking, events   |
| [`source/layout`](source/layout/)           | Layout engine                          |
| [`source/render`](source/render/)           | Painter, font rasterizer, renderers    |
| [`source/platform`](source/platform/)       | Windows, macOS and X11 window backends |

## Finding a Task
Before making changes, check these places:

- **Issues** - Look for issues
- **Discussions** - Ask questions or suggest ideas
- **Widgets** - Check `source/widgets/` for the current widget set

## Code Style Rules
### C Code Style
Follow the existing style throughout the codebase.

Rules to follow:

1. **Indentation:** Use 4 spaces (no tabs)
2. **Function names:** Use `snake_case` (e.g., `get_user_data`)
3. **Variable names:** Use `snake_case` (e.g., `user_score`)
4. **Operators:** Add spaces around them (`a + b`, not `a+b`)
5. **Parentheses:** No space after function name (`greet(name)`, not `greet (name)`)
6. **Comments:** Explain *why*, not only *what*

### Naming Conventions
- **Types:** `PascalCase` (`Widget`, `UiContext`, `UiFont`)
- **Functions and variables:** `snake_case` (`ui_painter_fill_rect`, `win_w`)
- **Public API prefix:** `apex_ui_` (`apex_ui_init`, `apex_ui_next_event`)
- **Internal module prefix:** `ui_` (`ui_layout`, `ui_state_get`)

### Commit Messages
Write clear, descriptive commit messages. Each commit message must include a type prefix:

| Type       | Description                  | Example                                          |
|------------|------------------------------|--------------------------------------------------|
| `feature`  | New feature or functionality | `feature: tabs widget drag-reorder`              |
| `fix`      | Bug fix                      | `fix: dropdown stays open after window blur`     |
| `docs`     | Documentation updates        | `docs: update widget reference for text_area`    |
| `refactor` | Code restructuring           | `refactor: simplify glyph cache eviction sweep`  |

Format:

```
<type>: <description in present tense>
```

Keep commits focused: one logical change per commit with its type prefix.

## Using AI Agents
### Project Policy
Apex permits the use of AI tools to accelerate development, but with restrictions.

### Usage Rules
1. **Code Review**: You bear full responsibility for the quality and correctness of AI-generated code
2. **Understanding**: You must understand and be able to explain every fragment of code you submit
3. **Testing**: AI-generated code must pass the same testing as manually written code

Remember: you are the author of the code, and AI is merely a tool.

## Making Changes
### Step-by-Step Process
1. Update your fork before starting:

```bash
git checkout main
git pull upstream main
git push origin main
git checkout -b feature/your-feature
```

2. Make focused changes - One feature or fix per branch
3. Test your changes as you go (see Testing section)
4. Update documentation if you change user-facing behavior
5. Commit your changes with a clear message

### Code Navigation Tips
- Start with `apex_ui.c` to understand the public API surface
- Look at similar widgets in `source/widgets/` before implementing new ones
- Use `fprintf(stderr, ...)` or `printf` for debugging (remove before committing)
- Read the docs listed in the README before changing behaviour

## Testing Your Changes
### Building
```bash
cmake -S . -B build/ -DCMAKE_BUILD_TYPE=Debug
cmake --build build/ --parallel
```

### Manual Testing
Apex UI does not ship an automated test suite yet, so verify your
changes by:

- Linking the library into a small demo that exercises the changed code
- Testing on every platform you can access (Win32, macOS, X11)
- Running under ASan or valgrind to catch memory errors
- Watching for new compiler warnings — the build is expected to be `-Wall -Wextra` clean

### Adding Tests
When a test suite lands, new features must include tests and bug fixes
must include regression tests.

## Submitting Changes
### 1. Save Your Changes
```bash
git add .
git commit -m "fix: description of your change"
git push origin feature/your-feature-name
```

### 2. Create a Pull Request
Go to your fork on GitHub and click **"Compare & pull request"**

Fill in the PR template completely:

```markdown
## Description
What does this PR do?

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Documentation update
- [ ] Refactor

## Testing
How did you test this?

## Related Issues
Closes #(issue number)
```

## Review Process
Maintainers will review your code:

- They may suggest changes — this is normal and helpful!
- Address feedback by updating your branch
- Push additional commits to the same branch

Once approved, your changes will be merged

### Review Timeline
- First review within 3-5 days
- Minor changes: faster
- Large features: may take longer

## Getting Help
### Before Asking
1. Read the documentation referenced in [readme](readme.md)
2. Search existing issues and discussions
3. Look at similar code in the project
4. Check `source/widgets/` for existing widget implementations

### When You Need Help
1. **Ask in Discussions** - For general questions
2. **Comment on Issues** - For specific problems
3. **Tag maintainers** - If directed in the issue

### Communication Tips
- Be clear about what you're trying to do
- Share what you've already tried
- Include error messages or screenshots
- Be patient and respectful

## What Makes a Good Contribution
### Small Contributions
- Fix typos in documentation
- Improve error messages
- Add helpful comments
- Update outdated information
- Add test coverage for existing features

### Bigger Contributions
- Fix bugs
- Add new widgets or containers
- Improve rendering and layout performance
- Add better documentation and examples
- Implement missing platform backends

### Thank You!
Your contributions make Apex UI better for everyone.

Remember:
- Everyone starts somewhere
- Questions are always welcome
- Your perspective is valuable
- You're making a difference

Happy coding!