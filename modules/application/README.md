# application

What the applications share: the workspace (where the user's files live), then scenarios,
saves and runs. It knows no command line and no screen. `apps/cli` and, later, `apps/gui`
call it.

Depends on nothing in the project yet.

Start with `application/workspace.hpp`.