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
When a project is opened in KingdomEditor, the editor loads its main scene,
creating `scenes/Main.scene` if needed. To change the main scene, right-click a
`.scene` file in the Content Browser and choose **Set as Main Scene**. Selecting
another scene in the Objects window only opens it for editing; it does not
change which scene runs first. Scene files use versioned JSON. Older
`.scene.json` files are renamed to `.scene` when loaded, when possible.
Pressing Ctrl+S saves the active scene and project; if the Code Editor is
focused, its open source file is saved as well. Editor window positions and
docking layout are persisted between launches.

Every GameObject has a Transform component. The Properties window's Add
Component menu can add a Sprite, and its texture picker opens at the project
directory for selecting an image from the project folders. Texture paths are
stored relative to the project when possible. Enabled sprites are rendered into
the live viewport using the GameObject hierarchy and transforms. The initial
component set includes Transform, Sprite, and Lua Script.

New projects start without a generated C++ source file. Create Lua scripts from
the Content Browser context menu; the Script component's **Select Script**
picker lists Lua files in the project.

## Lua scripts

Game projects can attach a Lua Script component to a GameObject. Choose its
script from the project picker; scripts do not execute while editing the scene.
Use **Run** (F5) to play the configured main scene in the editor and **Stop**
(Shift+F5) to end play. Each script can define
`on_start(self)` and `on_update(self, dt)`. The `self` handle provides
`get_name`, `get_position`/`set_position`, `get_rotation`/`set_rotation`,
`get_scale`/`set_scale`, `set_active`, and `find_object(name)`.

```lua
function on_update(self, dt)
    local x, y, z = self:get_position()
    self:set_position(x + dt, y, z)
end
```
