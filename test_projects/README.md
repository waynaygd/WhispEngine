# Standalone test projects

Open either descriptor using File > Open Project / Project Browser, or:

```
WhispEngine.exe --project test_projects/interactive_room/InteractiveRoom.whispproject
WhispEngine.exe --project test_projects/legacy_workspace/LegacyWorkspace.whispproject
```

Each directory can be copied independently, without its sibling. Assets are local;
engine-provided shaders and editor built-ins still come from the WhispEngine installation.
Interactive Room retains its original startup scene; historical resource-scene variants
live in `Scenes/variants`. Legacy Workspace retains the original version-3 PZ3 startup
and its asset library, without gameplay modernization. Both manifests also configure
Game Startup Map to their existing startup scene.

`WHISP_STAGE_TEST_PROJECTS=ON` prepares compatibility fixture keys under build/assets.
Set it OFF to build the editor without staging either project's data. Regression runs
require it ON. Engine diagnostic scene generators remain in C++ as before.
