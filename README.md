# KingdomEngine
A game engine!

## Prerequisites
- Windows 10/11;
- OpenGL 2/3

## Download and Build
### Windows 10/11
1. Clone the repository using `git clone https://github.com/TheLuke013/KingdomEngine.git`;
2. Go to the cloned repository folder and run the bat script `BuildWindows.bat`;
3. To run execute the bat script `RunWindows.bat`.

## Scenes
When a project is opened in KingdomEditor, the editor loads or creates
`scenes/Main.scene`. The Objects window lets you create scenes and
hierarchical game objects; the Properties window edits an object's name,
active state, and transform. Saving the project also saves the active scene and
remembers it for the next time the project is opened. Scene files use versioned
JSON and can also be saved from the Objects window. Older `.scene.json` files
are renamed to `.scene` when loaded, when possible.
Pressing Ctrl+S saves the active scene and project; if the Code Editor is
focused, its open source file is saved as well. Editor window positions and
docking layout are persisted between launches.

Every GameObject has a Transform component. The Properties window's Add
Component menu can add a Sprite, and its texture picker opens at the project
directory for selecting an image from the project folders. Texture paths are
stored relative to the project when possible. Enabled sprites are rendered into
the live viewport using the GameObject hierarchy and transforms. The initial
component set includes Transform, Sprite, and Lua Script.

## Lua scripts

Game projects can attach a Lua Script component to a GameObject. Set its path
relative to the project root, such as `scripts/Move.lua`, then build and run the
project; scripts do not execute while editing the scene. Each script can define
`on_start(self)` and `on_update(self, dt)`. The `self` handle provides
`get_name`, `get_position`/`set_position`, `get_rotation`/`set_rotation`,
`get_scale`/`set_scale`, `set_active`, and `find_object(name)`.

```lua
function on_update(self, dt)
    local x, y, z = self:get_position()
    self:set_position(x + dt, y, z)
end
```
