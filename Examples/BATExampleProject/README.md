# BAT Example Project

This is a minimal host project for Blueprint Automation Toolkit. It depends on
the plugin but does not redistribute the plugin.

1. Install Blueprint Automation Toolkit for your Unreal Engine version.
2. Extract this project outside the plugin directory.
3. If needed, update `EngineAssociation` in `BATExampleProject.uproject`.
4. Open the project and enable Blueprint Automation Toolkit when prompted.
5. Configure the local server under **Editor Preferences > Plugins > Blueprint
   Automation Toolkit**.
6. Start with the server disabled, set a bearer token, then enable it for the
   current editor session.

The plugin accepts loopback connections only. See the main documentation for
endpoint examples and the recommended discover-inspect-mutate-compile-save
workflow:

https://github.com/tgameengine/blueprint-automation-toolkit
