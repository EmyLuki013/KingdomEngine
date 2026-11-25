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
`scenes/Main.scene.json`. The Objects window lets you create scenes and
hierarchical game objects; the Properties window edits an object's name,
active state, and transform. Scene files use versioned JSON and are saved from
the Objects window.

This initial scene system covers hierarchy, transforms, and persistence.
Rendering and general-purpose components are not connected to scenes yet.
