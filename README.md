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

Every GameObject has a Transform component. The Properties window's Add
Component menu can add a Sprite, and its texture picker opens at the project
directory for selecting an image. Enabled sprites are rendered into the live
viewport using the GameObject hierarchy and transforms. The initial component
set is limited to Transform and Sprite.
