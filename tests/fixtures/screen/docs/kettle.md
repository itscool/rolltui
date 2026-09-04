A screen that exists only as files.

Nothing in the host binary knows this screen. No source file names these window ids, this menu, this action or this document, and no host was rebuilt to show it. Six files in a preset directory are the whole application.

layouts/kettle.json is the design: five windows, a find popup, and the two actions this screen emits (app.kettle and app.find).

menus/kettle.json is the menu on the right. Its "Put the kettle on" row names app.kettle, and the shortcut beside it is read from the live key table every frame, never from the menu file.

bindings/kettle.json is the keys. It is what gives app.kettle and app.find their chords.

docs/kettle.md is this text, in the file: window you are reading. docs/kettle-session.md is the document above it, in a transcript: window.

Four capabilities reach this screen through those files and through no host code at all. The transcript above marks a span with a state, and the theme decides whether that is a spinner, a breath or nothing. Its fenced diff block is coloured by the highlighter the host registered, because the fence says diff and nothing sniffed the content. The window is too small for the document, so it reports a scroll extent and the window draws a thumb in its right border column. And Ctrl-T — a chord no default binds, so the file is provably what bound it — opens a find bar that is an ordinary input in an ordinary popup this layout file declares.

Below is the key table those bindings produced. Tab moves between the windows; the arrow keys move within one.

Why "kettle"? The test's control greps every host and library source for that one word, so the screen needs a name the codebase would never use for anything else. One token covers the layout, the menu, this file, all five window ids and the action.
