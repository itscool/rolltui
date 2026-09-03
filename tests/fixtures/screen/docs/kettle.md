A screen that exists only as files.

Nothing in the host binary knows this screen. No source file names these window ids, this menu, this action or this document, and no host was rebuilt to show it. Four files in a preset directory are the whole application.

layouts/kettle.json is the design: three windows, and the one action this screen emits (app.kettle).

menus/kettle.json is the menu on the right. Its "Put the kettle on" row names app.kettle, and the shortcut beside it is read from the live key table every frame, never from the menu file.

bindings/kettle.json is the keys. It is what gives app.kettle a chord.

docs/kettle.md is this text, in the file: window you are reading.

Below is the key table those bindings produced. Tab moves between the three windows; the arrow keys move within one.

Why "kettle"? The test's control greps every host and library source for that one word, so the screen needs a name the codebase would never use for anything else. One token covers the layout, the menu, this file, all four window ids and the action.
